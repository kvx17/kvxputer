// BLE transport for kvxkeyboard HID: HID-over-GATT peripheral on BlueZ (D-Bus).
//
// Registers a GATT application (HID 0x1812, Battery 0x180F, Device Info 0x180A),
// an LE advertisement and a NoInputNoOutput pairing agent with bluetoothd.
// Everything touching the bus runs on one thread (sd-event loop); the UI and
// the HID session post work to it through an eventfd.
//
// Host policy only ever touches devices that are HID hosts of this app: ones the
// screen marked as known (host slots) or that used our GATT service. Headphones
// and other peripherals connected to the Zero are never disconnected.

#include "hid_session.h"

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

namespace platform::hid {
namespace {

constexpr const char* kAppPath = "/kvx/hid";
constexpr const char* kAdvPath = "/kvx/adv";
constexpr const char* kAgentPath = "/kvx/agent";
constexpr const char* kNewHostOnly = "*new*";

struct Attr; // characteristic or descriptor

struct Service {
    std::string path;
    std::string uuid;
    bool primary{true};
};

struct Attr {
    class BleTransport* owner{nullptr};
    bool descriptor{false};
    std::string path;
    std::string parent; // service (chr) or characteristic (desc)
    std::string uuid;
    std::vector<std::string> flags;
    std::vector<uint8_t> value;
    std::atomic<bool> notifying{false};
    uint8_t report_id{0}; // input report characteristics only
};

struct Device {
    std::string path;
    std::string addr;
    std::string alias;
    bool paired{false};
    bool connected{false};
    bool trusted{false};
    uint64_t connected_since{0};
};

uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec / 1000000);
}

class BleTransport final : public Transport {
public:
    explicit BleTransport(std::string name) : name_(std::move(name)) {}
    ~BleTransport() override { stop(); }

    const char* name() const override { return "BLE"; }

    bool start(std::string& error) override;
    void stop() override;
    LinkState state() override;
    std::string host_label() override;
    std::string detail() override;
    bool write_report(uint8_t id, const uint8_t* data, size_t len) override;

    bool has_hosts() const override { return true; }
    std::vector<HostInfo> hosts() override;
    void set_pairing(bool open) override;
    bool pairing() const override { return pairing_; }
    void set_selected_host(const std::string& addr) override;
    bool disconnect_host() override;
    bool forget_host(const std::string& addr) override;
    bool forget_all() override;
    bool rename_host(const std::string& addr, const std::string& name) override;
    // Hosts the screen remembers (slots); they count as ours for host policy.
    void set_known_hosts(const std::vector<std::string>& addrs);

    // --- vtable callbacks (bus thread) ---
    static int prop_svc_uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int prop_svc_primary(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*,
                                sd_bus_error*);
    static int prop_uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int prop_parent(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int prop_flags(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int prop_value(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int prop_notifying(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*,
                              sd_bus_error*);
    static int m_read(sd_bus_message*, void*, sd_bus_error*);
    static int m_write(sd_bus_message*, void*, sd_bus_error*);
    static int m_start_notify(sd_bus_message*, void*, sd_bus_error*);
    static int m_stop_notify(sd_bus_message*, void*, sd_bus_error*);
    static int adv_prop(sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int adv_release(sd_bus_message*, void*, sd_bus_error*);
    static int agent_method(sd_bus_message*, void*, sd_bus_error*);

private:
    bool setup_bus(std::string& error);
    bool find_adapter(std::string& error);
    void build_gatt();
    void add_chr(const std::string& svc, const char* uuid, std::vector<std::string> flags,
                 std::vector<uint8_t> value, uint8_t report_id = 0);
    Attr* add_desc(const char* uuid, std::vector<std::string> flags, std::vector<uint8_t> value);
    void register_with_bluez();
    // Advertising is reconciled toward want_adv_ / pending_discoverable_ one step
    // at a time: BlueZ answers "Failed to complete registration" if an advert is
    // withdrawn while its registration is still in flight, and the kernel
    // refuses discoverable changes ("Busy") while an advert is live.
    void reconcile_adv();
    static int on_adv_registered(sd_bus_message*, void*, sd_bus_error*);
    static int on_adv_unregistered(sd_bus_message*, void*, sd_bus_error*);
    void refresh_devices();
    void apply_host_policy();
    void post(std::function<void()> fn);
    void run_posted();
    void note_client(sd_bus_message* options);
    bool device_is_ours(const Device& d) const;
    void call_async(const std::string& path, const char* iface, const char* method,
                    const char* what);
    void set_property(const std::string& path, const char* iface, const char* prop, char type,
                      const void* value);

    static int on_wake(sd_event_source*, int, uint32_t, void*);
    static int on_timer(sd_event_source*, uint64_t, void*);
    static int on_reply(sd_bus_message*, void*, sd_bus_error*);
    static int on_default_agent(sd_bus_message*, void*, sd_bus_error*);
    static int on_objects(sd_bus_message*, void*, sd_bus_error*);
    void loop();
    void shutdown_bluez();
    bool set_adapter_bool(const char* prop, bool value);

    std::string name_;
    std::string adapter_;
    std::string old_alias_;
    int old_pairable_{-1}; // -1 unknown
    int old_discoverable_{-1};

    sd_bus* bus_{nullptr};
    sd_event* event_{nullptr};
    int wake_fd_{-1};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};

    std::vector<std::unique_ptr<Service>> services_;
    std::vector<std::unique_ptr<Attr>> attrs_;
    Attr* report_by_id_[4]{};
    enum class Adv { Off, Registering, On, Unregistering };
    Adv adv_{Adv::Off};
    bool want_adv_{false};
    int pending_discoverable_{-1}; // -1 none, else value to apply while no advert is live

    mutable std::mutex mu_;
    std::deque<std::function<void()>> posted_;
    std::map<std::string, Device> devices_; // by object path
    std::set<std::string> hid_clients_;     // device paths that used our GATT
    std::set<std::string> known_hosts_;     // addresses from the screen's slots
    std::set<std::string> new_only_block_;  // paired addresses when "new host only" began
    std::string selected_;
    std::atomic<bool> pairing_{false};
    std::string last_error_;
    std::string pending_adv_error_;
};

// --- property / method handlers -------------------------------------------------

int BleTransport::prop_svc_uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                                void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", static_cast<Service*>(userdata)->uuid.c_str());
}

int BleTransport::prop_svc_primary(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                                   void* userdata, sd_bus_error*) {
    const int b = static_cast<Service*>(userdata)->primary ? 1 : 0;
    return sd_bus_message_append(reply, "b", b);
}

int BleTransport::prop_uuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                            void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", static_cast<Attr*>(userdata)->uuid.c_str());
}

int BleTransport::prop_parent(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                              void* userdata, sd_bus_error*) {
    return sd_bus_message_append(reply, "o", static_cast<Attr*>(userdata)->parent.c_str());
}

int BleTransport::prop_flags(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                             void* userdata, sd_bus_error*) {
    auto* a = static_cast<Attr*>(userdata);
    int r = sd_bus_message_open_container(reply, 'a', "s");
    for (const auto& f : a->flags) {
        if (r >= 0) r = sd_bus_message_append(reply, "s", f.c_str());
    }
    if (r >= 0) r = sd_bus_message_close_container(reply);
    return r;
}

int BleTransport::prop_value(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                             void* userdata, sd_bus_error*) {
    auto* a = static_cast<Attr*>(userdata);
    return sd_bus_message_append_array(reply, 'y', a->value.data(), a->value.size());
}

int BleTransport::prop_notifying(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply,
                                 void* userdata, sd_bus_error*) {
    const int b = static_cast<Attr*>(userdata)->notifying.load() ? 1 : 0;
    return sd_bus_message_append(reply, "b", b);
}

// Read a{sv} options; returns "offset" and records the "device" as a HID client.
static uint16_t parse_options(sd_bus_message* m, std::string* device_out) {
    uint16_t offset = 0;
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return 0;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(m, "s", &key) < 0) break;
        if (key && std::strcmp(key, "offset") == 0) {
            if (sd_bus_message_enter_container(m, 'v', "q") >= 0) {
                sd_bus_message_read(m, "q", &offset);
                sd_bus_message_exit_container(m);
            }
        } else if (key && std::strcmp(key, "device") == 0 && device_out) {
            if (sd_bus_message_enter_container(m, 'v', "o") >= 0) {
                const char* dev = nullptr;
                if (sd_bus_message_read(m, "o", &dev) >= 0 && dev) *device_out = dev;
                sd_bus_message_exit_container(m);
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return offset;
}

int BleTransport::m_read(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    auto* a = static_cast<Attr*>(userdata);
    std::string device;
    const uint16_t offset = parse_options(m, &device);
    if (!device.empty()) {
        std::lock_guard<std::mutex> lock(a->owner->mu_);
        a->owner->hid_clients_.insert(device);
    }
    if (offset > a->value.size()) {
        return sd_bus_error_set(err, "org.bluez.Error.InvalidOffset", "offset");
    }
    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_append_array(reply, 'y', a->value.data() + offset, a->value.size() - offset);
    if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
}

int BleTransport::m_write(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* a = static_cast<Attr*>(userdata);
    const void* data = nullptr;
    size_t len = 0;
    if (sd_bus_message_read_array(m, 'y', &data, &len) >= 0) {
        std::string device;
        parse_options(m, &device);
        if (!device.empty()) {
            std::lock_guard<std::mutex> lock(a->owner->mu_);
            a->owner->hid_clients_.insert(device);
        }
        // Protocol Mode / Control Point / LED output: keep the last value.
        if (data && len <= 64) a->value.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
    }
    return sd_bus_reply_method_return(m, "");
}

int BleTransport::m_start_notify(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* a = static_cast<Attr*>(userdata);
    a->notifying = true;
    sd_bus_emit_properties_changed(sd_bus_message_get_bus(m), a->path.c_str(), "org.bluez.GattCharacteristic1",
                                   "Notifying", nullptr);
    return sd_bus_reply_method_return(m, "");
}

int BleTransport::m_stop_notify(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* a = static_cast<Attr*>(userdata);
    a->notifying = false;
    sd_bus_emit_properties_changed(sd_bus_message_get_bus(m), a->path.c_str(), "org.bluez.GattCharacteristic1",
                                   "Notifying", nullptr);
    return sd_bus_reply_method_return(m, "");
}

int BleTransport::adv_prop(sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply,
                           void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleTransport*>(userdata);
    if (std::strcmp(property, "Type") == 0) return sd_bus_message_append(reply, "s", "peripheral");
    if (std::strcmp(property, "ServiceUUIDs") == 0) return sd_bus_message_append(reply, "as", 1, "1812");
    if (std::strcmp(property, "LocalName") == 0) return sd_bus_message_append(reply, "s", self->name_.c_str());
    if (std::strcmp(property, "Appearance") == 0) return sd_bus_message_append(reply, "q", uint16_t{0x03C1});
    return -ENOENT;
}

int BleTransport::adv_release(sd_bus_message* m, void* userdata, sd_bus_error*) {
    // bluetoothd dropped the advert itself (adapter reset …); the next host
    // policy pass (every 0.7 s) registers it again.
    static_cast<BleTransport*>(userdata)->adv_ = Adv::Off;
    return sd_bus_reply_method_return(m, "");
}

// One handler for every Agent1 method. Pairing is accepted only while the
// screen has pairing open; reconnects of bonded hosts never reach the agent.
int BleTransport::agent_method(sd_bus_message* m, void* userdata, sd_bus_error* err) {
    auto* self = static_cast<BleTransport*>(userdata);
    const char* member = sd_bus_message_get_member(m);
    const std::string name = member ? member : "";
    if (name == "Release" || name == "Cancel" || name == "DisplayPinCode" || name == "DisplayPasskey") {
        return sd_bus_reply_method_return(m, "");
    }
    if (name == "AuthorizeService") {
        return sd_bus_reply_method_return(m, "");
    }
    if (!self->pairing_) {
        return sd_bus_error_set(err, "org.bluez.Error.Rejected", "pairing closed");
    }
    if (name == "RequestPinCode") return sd_bus_reply_method_return(m, "s", "0000");
    if (name == "RequestPasskey") return sd_bus_reply_method_return(m, "u", uint32_t{0});
    // RequestConfirmation / RequestAuthorization: Just Works.
    return sd_bus_reply_method_return(m, "");
}

const sd_bus_vtable kServiceVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleTransport::prop_svc_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", BleTransport::prop_svc_primary, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable kChrVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleTransport::prop_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", BleTransport::prop_parent, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", BleTransport::prop_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", BleTransport::prop_value, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Notifying", "b", BleTransport::prop_notifying, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", BleTransport::m_read, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", BleTransport::m_write, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartNotify", "", "", BleTransport::m_start_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", BleTransport::m_stop_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable kDescVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", BleTransport::prop_uuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Characteristic", "o", BleTransport::prop_parent, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", BleTransport::prop_flags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", BleTransport::m_read, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", BleTransport::m_write, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable kAdvVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Type", "s", BleTransport::adv_prop, 0, 0),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", BleTransport::adv_prop, 0, 0),
    SD_BUS_PROPERTY("LocalName", "s", BleTransport::adv_prop, 0, 0),
    SD_BUS_PROPERTY("Appearance", "q", BleTransport::adv_prop, 0, 0),
    SD_BUS_METHOD("Release", "", "", BleTransport::adv_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable kAgentVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPinCode", "o", "s", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPinCode", "os", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestPasskey", "o", "u", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestAuthorization", "o", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AuthorizeService", "os", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Cancel", "", "", BleTransport::agent_method, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

// --- setup ----------------------------------------------------------------------

void BleTransport::add_chr(const std::string& svc, const char* uuid, std::vector<std::string> flags,
                           std::vector<uint8_t> value, uint8_t report_id) {
    auto a = std::make_unique<Attr>();
    a->owner = this;
    a->parent = svc;
    a->uuid = uuid;
    a->flags = std::move(flags);
    a->value = std::move(value);
    a->report_id = report_id;
    int n = 0;
    for (const auto& x : attrs_) {
        if (!x->descriptor && x->parent == svc) ++n;
    }
    a->path = svc + "/char" + std::to_string(n);
    if (report_id > 0 && report_id < 4) report_by_id_[report_id] = a.get();
    attrs_.push_back(std::move(a));
}

Attr* BleTransport::add_desc(const char* uuid, std::vector<std::string> flags, std::vector<uint8_t> value) {
    Attr* chr = nullptr;
    for (auto it = attrs_.rbegin(); it != attrs_.rend(); ++it) {
        if (!(*it)->descriptor) {
            chr = it->get();
            break;
        }
    }
    if (!chr) return nullptr;
    auto a = std::make_unique<Attr>();
    a->owner = this;
    a->descriptor = true;
    a->parent = chr->path;
    a->uuid = uuid;
    a->flags = std::move(flags);
    a->value = std::move(value);
    int n = 0;
    for (const auto& x : attrs_) {
        if (x->descriptor && x->parent == chr->path) ++n;
    }
    a->path = chr->path + "/desc" + std::to_string(n);
    attrs_.push_back(std::move(a));
    return attrs_.back().get();
}

void BleTransport::build_gatt() {
    using namespace tools::hid;
    auto svc = [&](const char* uuid) {
        auto s = std::make_unique<Service>();
        s->uuid = uuid;
        s->path = std::string(kAppPath) + "/service" + std::to_string(services_.size());
        services_.push_back(std::move(s));
        return services_.back()->path;
    };

    // HID service. Report Map and input reports require an encrypted link,
    // which is what makes hosts pair (bond) before using the keyboard.
    const std::string hid = svc("00001812-0000-1000-8000-00805f9b34fb");
    add_chr(hid, "00002a4a-0000-1000-8000-00805f9b34fb", {"read"}, {0x11, 0x01, 0x00, 0x02}); // HID Info
    add_chr(hid, "00002a4b-0000-1000-8000-00805f9b34fb", {"encrypt-read"}, report_descriptor());
    add_chr(hid, "00002a4c-0000-1000-8000-00805f9b34fb", {"write-without-response"}, {0x00}); // Control Pt
    add_chr(hid, "00002a4e-0000-1000-8000-00805f9b34fb", {"read", "write-without-response"}, {0x01});
    add_chr(hid, "00002a4d-0000-1000-8000-00805f9b34fb", {"encrypt-read", "notify"},
            std::vector<uint8_t>(kKeyboardReportLen, 0), kReportKeyboard);
    add_desc("00002908-0000-1000-8000-00805f9b34fb", {"read"}, {kReportKeyboard, 0x01});
    add_chr(hid, "00002a4d-0000-1000-8000-00805f9b34fb", {"encrypt-read", "encrypt-write", "write-without-response"},
            {0x00}); // LED output
    add_desc("00002908-0000-1000-8000-00805f9b34fb", {"read"}, {kReportKeyboard, 0x02});
    add_chr(hid, "00002a4d-0000-1000-8000-00805f9b34fb", {"encrypt-read", "notify"},
            std::vector<uint8_t>(kConsumerReportLen, 0), kReportConsumer);
    add_desc("00002908-0000-1000-8000-00805f9b34fb", {"read"}, {kReportConsumer, 0x01});
    add_chr(hid, "00002a4d-0000-1000-8000-00805f9b34fb", {"encrypt-read", "notify"},
            std::vector<uint8_t>(kMouseReportLen, 0), kReportMouse);
    add_desc("00002908-0000-1000-8000-00805f9b34fb", {"read"}, {kReportMouse, 0x01});

    const std::string bat = svc("0000180f-0000-1000-8000-00805f9b34fb");
    add_chr(bat, "00002a19-0000-1000-8000-00805f9b34fb", {"read", "notify"}, {100});

    // PnP ID: USB-IF vendor source, pid.codes 0x1209 / 0x0001, version 1.0.
    const std::string dis = svc("0000180a-0000-1000-8000-00805f9b34fb");
    add_chr(dis, "00002a50-0000-1000-8000-00805f9b34fb", {"read"}, {0x02, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01});
}

bool BleTransport::find_adapter(std::string& error) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(bus_, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                               &err, &reply, "");
    if (r < 0) {
        error = std::string("Bluetooth service not reachable: ") + (err.message ? err.message : std::strerror(-r));
        sd_bus_error_free(&err);
        return false;
    }
    // a{oa{sa{sv}}}: pick the first object exposing both GattManager1 and LEAdvertisingManager1.
    std::string found;
    if (sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read(reply, "o", &path);
            bool gatt = false, adv = false;
            if (sd_bus_message_enter_container(reply, 'a', "{sa{sv}}") >= 0) {
                while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
                    const char* iface = nullptr;
                    sd_bus_message_read(reply, "s", &iface);
                    if (iface && std::strcmp(iface, "org.bluez.GattManager1") == 0) gatt = true;
                    if (iface && std::strcmp(iface, "org.bluez.LEAdvertisingManager1") == 0) adv = true;
                    sd_bus_message_skip(reply, "a{sv}");
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            if (gatt && adv && found.empty() && path) found = path;
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    if (found.empty()) {
        error = "No Bluetooth LE adapter";
        return false;
    }
    adapter_ = found;
    return true;
}

void BleTransport::set_property(const std::string& path, const char* iface, const char* prop, char type,
                                const void* value) {
    sd_bus_message* m = nullptr;
    if (sd_bus_message_new_method_call(bus_, &m, "org.bluez", path.c_str(), "org.freedesktop.DBus.Properties",
                                       "Set") < 0) {
        return;
    }
    const char sig[2] = {type, 0};
    sd_bus_message_append(m, "ss", iface, prop);
    sd_bus_message_open_container(m, 'v', sig);
    if (type == 'b') sd_bus_message_append_basic(m, 'b', value);
    else if (type == 's') sd_bus_message_append_basic(m, 's', value);
    sd_bus_message_close_container(m);
    sd_bus_call_async(bus_, nullptr, m, on_reply, this, 0);
    sd_bus_message_unref(m);
}

bool BleTransport::setup_bus(std::string& error) {
    int r = sd_bus_open_system(&bus_);
    if (r < 0) {
        error = std::string("D-Bus: ") + std::strerror(-r);
        return false;
    }
    if (!find_adapter(error)) return false;

    // Keep the adapter alias in step with the HID name (GAP Device Name), and
    // remember the old one so stop() can put it back.
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        char* alias = nullptr;
        if (sd_bus_get_property_string(bus_, "org.bluez", adapter_.c_str(), "org.bluez.Adapter1", "Alias", &err,
                                       &alias) >= 0 &&
            alias) {
            old_alias_ = alias;
        }
        std::free(alias);
        sd_bus_error_free(&err);
        int pairable = 0;
        err = SD_BUS_ERROR_NULL;
        if (sd_bus_get_property_trivial(bus_, "org.bluez", adapter_.c_str(), "org.bluez.Adapter1", "Pairable", &err,
                                        'b', &pairable) >= 0) {
            old_pairable_ = pairable ? 1 : 0;
        }
        sd_bus_error_free(&err);
        int discoverable = 0;
        err = SD_BUS_ERROR_NULL;
        if (sd_bus_get_property_trivial(bus_, "org.bluez", adapter_.c_str(), "org.bluez.Adapter1", "Discoverable",
                                        &err, 'b', &discoverable) >= 0) {
            old_discoverable_ = discoverable ? 1 : 0;
        }
        sd_bus_error_free(&err);
    }

    build_gatt();
    r = sd_bus_add_object_manager(bus_, nullptr, kAppPath);
    for (auto& s : services_) {
        if (r >= 0) {
            r = sd_bus_add_object_vtable(bus_, nullptr, s->path.c_str(), "org.bluez.GattService1", kServiceVtable,
                                         s.get());
        }
    }
    for (auto& a : attrs_) {
        if (r < 0) break;
        if (a->descriptor) {
            r = sd_bus_add_object_vtable(bus_, nullptr, a->path.c_str(), "org.bluez.GattDescriptor1", kDescVtable,
                                         a.get());
        } else {
            r = sd_bus_add_object_vtable(bus_, nullptr, a->path.c_str(), "org.bluez.GattCharacteristic1",
                                         kChrVtable, a.get());
        }
    }
    if (r >= 0) {
        r = sd_bus_add_object_vtable(bus_, nullptr, kAdvPath, "org.bluez.LEAdvertisement1", kAdvVtable, this);
    }
    if (r >= 0) {
        r = sd_bus_add_object_vtable(bus_, nullptr, kAgentPath, "org.bluez.Agent1", kAgentVtable, this);
    }
    if (r < 0) {
        error = std::string("GATT export: ") + std::strerror(-r);
        return false;
    }

    r = sd_event_new(&event_);
    if (r >= 0) r = sd_bus_attach_event(bus_, event_, SD_EVENT_PRIORITY_NORMAL);
    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (r >= 0 && wake_fd_ >= 0) r = sd_event_add_io(event_, nullptr, wake_fd_, EPOLLIN, on_wake, this);
    if (r >= 0) {
        r = sd_event_add_time_relative(event_, nullptr, CLOCK_MONOTONIC, 50 * 1000, 0, on_timer, this);
    }
    if (r < 0 || wake_fd_ < 0) {
        error = "event loop setup failed";
        return false;
    }
    return true;
}

int BleTransport::on_default_agent(sd_bus_message* m, void* userdata, sd_bus_error* e) {
    auto* self = static_cast<BleTransport*>(userdata);
    on_reply(m, userdata, e);
    self->set_adapter_bool("Pairable", self->pairing_);
    return 0;
}

int BleTransport::on_reply(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleTransport*>(userdata);
    const sd_bus_error* e = sd_bus_message_get_error(m);
    if (e && e->name) {
        std::lock_guard<std::mutex> lock(self->mu_);
        // AlreadyExists from a stale registration is harmless.
        if (std::strstr(e->name, "AlreadyExists") == nullptr) {
            self->last_error_ = std::string("Bluetooth: ") + (e->message ? e->message : e->name);
        }
    }
    return 0;
}

void BleTransport::call_async(const std::string& path, const char* iface, const char* method, const char* what) {
    (void)what;
    sd_bus_call_method_async(bus_, nullptr, "org.bluez", path.c_str(), iface, method, on_reply, this, "");
}

// Adapter mode changes go through the kernel one at a time; bluetoothd answers
// "Busy" while another (or an advert update) is in flight. Do them in order,
// waiting for each reply, and retry a busy controller briefly.
bool BleTransport::set_adapter_bool(const char* prop, bool value) {
    for (int attempt = 0; attempt < 15; ++attempt) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        const int r = sd_bus_call_method(bus_, "org.bluez", adapter_.c_str(), "org.freedesktop.DBus.Properties", "Set",
                                         &err, nullptr, "ssv", "org.bluez.Adapter1", prop, "b", value ? 1 : 0);
        // bluetoothd reports it as org.bluez.Error.Failed / "Busy" (or Error.Busy).
        const bool busy = r < 0 && ((err.name && std::strstr(err.name, "Busy")) ||
                                    (err.message && std::strstr(err.message, "Busy")));
        if (r >= 0) {
            sd_bus_error_free(&err);
            return true;
        }
        if (!busy) {
            std::lock_guard<std::mutex> lock(mu_);
            last_error_ = std::string("Bluetooth: ") + prop + ": " + (err.message ? err.message : std::strerror(-r));
            sd_bus_error_free(&err);
            return false;
        }
        sd_bus_error_free(&err);
        ::usleep(150 * 1000);
    }
    std::lock_guard<std::mutex> lock(mu_);
    last_error_ = std::string("Bluetooth: ") + prop + ": adapter busy";
    return false;
}

int BleTransport::on_adv_registered(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleTransport*>(userdata);
    const sd_bus_error* e = sd_bus_message_get_error(m);
    if (e && e->name) {
        self->adv_ = Adv::Off;
        std::lock_guard<std::mutex> lock(self->mu_);
        self->last_error_ = std::string("Bluetooth advert: ") + (e->message ? e->message : e->name);
    } else {
        self->adv_ = Adv::On;
    }
    self->reconcile_adv();
    return 0;
}

int BleTransport::on_adv_unregistered(sd_bus_message*, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleTransport*>(userdata);
    self->adv_ = Adv::Off; // an error here means BlueZ no longer has it either
    self->reconcile_adv();
    return 0;
}

void BleTransport::reconcile_adv() {
    if (!bus_ || adv_ == Adv::Registering || adv_ == Adv::Unregistering) return; // wait for the reply
    if (pending_discoverable_ >= 0) {
        if (adv_ == Adv::On) {
            adv_ = Adv::Unregistering;
            sd_bus_call_method_async(bus_, nullptr, "org.bluez", adapter_.c_str(), "org.bluez.LEAdvertisingManager1",
                                     "UnregisterAdvertisement", on_adv_unregistered, this, "o", kAdvPath);
            return;
        }
        set_adapter_bool("Discoverable", pending_discoverable_ == 1);
        pending_discoverable_ = -1;
    }
    if (want_adv_ && adv_ == Adv::Off) {
        adv_ = Adv::Registering;
        // Async: bluetoothd reads the advert's properties back from us.
        sd_bus_call_method_async(bus_, nullptr, "org.bluez", adapter_.c_str(), "org.bluez.LEAdvertisingManager1",
                                 "RegisterAdvertisement", on_adv_registered, this, "oa{sv}", kAdvPath, 0);
    } else if (!want_adv_ && adv_ == Adv::On) {
        adv_ = Adv::Unregistering;
        sd_bus_call_method_async(bus_, nullptr, "org.bluez", adapter_.c_str(), "org.bluez.LEAdvertisingManager1",
                                 "UnregisterAdvertisement", on_adv_unregistered, this, "o", kAdvPath);
    }
}

void BleTransport::register_with_bluez() {
    set_adapter_bool("Powered", true);
    // Bonding is only allowed while the screen has pairing open. bluetoothd may
    // finish Just Works pairing without asking the agent, so Pairable is the gate.
    set_adapter_bool("Pairable", pairing_);
    // Discoverability lives only on the adapter: the advert takes its "general
    // discoverable" flag from it. Applied by reconcile_adv() while no advert is live.
    pending_discoverable_ = pairing_ ? 1 : 0;
    const char* alias = name_.c_str();
    set_property(adapter_, "org.bluez.Adapter1", "Alias", 's', alias);

    sd_bus_call_method_async(bus_, nullptr, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                             on_reply, this, "os", kAgentPath, "NoInputNoOutput");
    // Becoming the default agent makes bluetoothd switch Pairable back on, so the
    // pairing gate is applied again once that reply arrives.
    sd_bus_call_method_async(bus_, nullptr, "org.bluez", "/org/bluez", "org.bluez.AgentManager1",
                             "RequestDefaultAgent", on_default_agent, this, "o", kAgentPath);
    // RegisterApplication makes bluetoothd call back into GetManagedObjects,
    // so it must be async (a blocking call here would deadlock the loop).
    sd_bus_call_method_async(bus_, nullptr, "org.bluez", adapter_.c_str(), "org.bluez.GattManager1",
                             "RegisterApplication", on_reply, this, "oa{sv}", kAppPath, 0);
    want_adv_ = true;
    reconcile_adv();
}

// --- device tracking & host policy ------------------------------------------------

int BleTransport::on_objects(sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<BleTransport*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr)) return 0;
    std::map<std::string, Device> found;
    if (sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read(reply, "o", &path);
            Device d;
            bool is_dev = false;
            if (sd_bus_message_enter_container(reply, 'a', "{sa{sv}}") >= 0) {
                while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
                    const char* iface = nullptr;
                    sd_bus_message_read(reply, "s", &iface);
                    if (iface && std::strcmp(iface, "org.bluez.Device1") == 0 && path &&
                        std::strncmp(path, self->adapter_.c_str(), self->adapter_.size()) == 0) {
                        is_dev = true;
                        sd_bus_message_enter_container(reply, 'a', "{sv}");
                        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
                            const char* key = nullptr;
                            sd_bus_message_read(reply, "s", &key);
                            const std::string k = key ? key : "";
                            if (k == "Address" || k == "Alias") {
                                const char* s = nullptr;
                                sd_bus_message_enter_container(reply, 'v', "s");
                                sd_bus_message_read(reply, "s", &s);
                                sd_bus_message_exit_container(reply);
                                (k == "Address" ? d.addr : d.alias) = s ? s : "";
                            } else if (k == "Paired" || k == "Connected" || k == "Trusted" || k == "Bonded") {
                                int b = 0;
                                sd_bus_message_enter_container(reply, 'v', "b");
                                sd_bus_message_read(reply, "b", &b);
                                sd_bus_message_exit_container(reply);
                                if (k == "Paired" || k == "Bonded") d.paired = d.paired || b;
                                else if (k == "Connected") d.connected = b;
                                else d.trusted = b;
                            } else {
                                sd_bus_message_skip(reply, "v");
                            }
                            sd_bus_message_exit_container(reply);
                        }
                        sd_bus_message_exit_container(reply);
                    } else {
                        sd_bus_message_skip(reply, "a{sv}");
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            if (is_dev && path) {
                d.path = path;
                found[d.path] = d;
            }
        }
        sd_bus_message_exit_container(reply);
    }
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        const uint64_t now = now_ms();
        for (auto& [path, d] : found) {
            const auto it = self->devices_.find(path);
            if (d.connected) {
                d.connected_since =
                    (it != self->devices_.end() && it->second.connected) ? it->second.connected_since : now;
            }
        }
        self->devices_ = std::move(found);
        // A client that disconnected must subscribe again before reports count.
        for (auto it = self->hid_clients_.begin(); it != self->hid_clients_.end();) {
            const auto d = self->devices_.find(*it);
            if (d == self->devices_.end()) it = self->hid_clients_.erase(it);
            else ++it;
        }
    }
    self->apply_host_policy();
    return 0;
}

bool BleTransport::device_is_ours(const Device& d) const {
    return known_hosts_.count(d.addr) > 0 || hid_clients_.count(d.path) > 0;
}

void BleTransport::apply_host_policy() {
    std::vector<std::string> drop;
    std::vector<std::string> trust;
    bool any_ours_connected = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const uint64_t now = now_ms();
        for (const auto& [path, d] : devices_) {
            if (!d.connected || !device_is_ours(d)) continue;
            bool reject = false;
            if (selected_ == kNewHostOnly) {
                reject = new_only_block_.count(d.addr) > 0;
            } else if (!selected_.empty()) {
                reject = d.addr != selected_;
            }
            // Unknown host poking the HID service while pairing is closed.
            if (!reject && !pairing_ && !d.paired && known_hosts_.count(d.addr) == 0 &&
                now - d.connected_since > 2500) {
                reject = true;
            }
            if (reject) {
                drop.push_back(path);
                continue;
            }
            any_ours_connected = true;
            if (d.paired && !d.trusted) trust.push_back(path);
        }
    }
    for (const auto& p : drop) call_async(p, "org.bluez.Device1", "Disconnect", "drop");
    const int on = 1;
    for (const auto& p : trust) set_property(p, "org.bluez.Device1", "Trusted", 'b', &on);
    // Advertise only while nobody is linked, so a second host cannot race in.
    want_adv_ = !any_ours_connected;
    reconcile_adv();
}

void BleTransport::refresh_devices() {
    sd_bus_call_method_async(bus_, nullptr, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                             "GetManagedObjects", on_objects, this, "");
}

// --- loop -----------------------------------------------------------------------

void BleTransport::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        posted_.push_back(std::move(fn));
    }
    const uint64_t one = 1;
    if (wake_fd_ >= 0) (void)!::write(wake_fd_, &one, sizeof(one));
}

void BleTransport::run_posted() {
    std::deque<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lock(mu_);
        work.swap(posted_);
    }
    for (auto& fn : work) fn();
}

int BleTransport::on_wake(sd_event_source*, int fd, uint32_t, void* userdata) {
    uint64_t v = 0;
    (void)!::read(fd, &v, sizeof(v));
    auto* self = static_cast<BleTransport*>(userdata);
    self->run_posted();
    if (self->stop_) {
        // Tear down while the bus is still open: sd-bus closes the connection
        // as soon as the event loop exits, so later calls would all fail.
        self->shutdown_bluez();
        sd_event_exit(self->event_, 0);
    }
    return 0;
}

int BleTransport::on_timer(sd_event_source* src, uint64_t, void* userdata) {
    auto* self = static_cast<BleTransport*>(userdata);
    self->refresh_devices();
    sd_event_source_set_time_relative(src, 700 * 1000);
    sd_event_source_set_enabled(src, SD_EVENT_ON);
    return 0;
}

void BleTransport::loop() {
    register_with_bluez();
    sd_event_loop(event_);
}

// Bus thread, before the event loop exits: drop our hosts, withdraw everything,
// and put the adapter's name / pairable / discoverable back.
void BleTransport::shutdown_bluez() {
    std::vector<std::string> linked;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [path, d] : devices_) {
            if (d.connected && device_is_ours(d)) linked.push_back(path);
        }
    }
    for (const auto& p : linked) {
        sd_bus_call_method(bus_, "org.bluez", p.c_str(), "org.bluez.Device1", "Disconnect", nullptr, nullptr, "");
    }
    if (adv_ != Adv::Off) {
        // Also covers a registration still in flight; an error just means it is gone.
        sd_bus_call_method(bus_, "org.bluez", adapter_.c_str(), "org.bluez.LEAdvertisingManager1",
                           "UnregisterAdvertisement", nullptr, nullptr, "o", kAdvPath);
        adv_ = Adv::Off;
    }
    sd_bus_call_method(bus_, "org.bluez", adapter_.c_str(), "org.bluez.GattManager1", "UnregisterApplication",
                       nullptr, nullptr, "o", kAppPath);
    sd_bus_call_method(bus_, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent", nullptr,
                       nullptr, "o", kAgentPath);
    if (!old_alias_.empty()) {
        sd_bus_call_method(bus_, "org.bluez", adapter_.c_str(), "org.freedesktop.DBus.Properties", "Set", nullptr,
                           nullptr, "ssv", "org.bluez.Adapter1", "Alias", "s", old_alias_.c_str());
    }
    if (old_discoverable_ >= 0) set_adapter_bool("Discoverable", old_discoverable_ == 1);
    if (old_pairable_ >= 0) set_adapter_bool("Pairable", old_pairable_ == 1);
    sd_bus_flush(bus_);
}

bool BleTransport::start(std::string& error) {
    if (running_) return true;
    stop_ = false;
    if (!setup_bus(error)) {
        stop();
        return false;
    }
    running_ = true;
    thread_ = std::thread([this]() { loop(); });
    return true;
}

void BleTransport::stop() {
    if (thread_.joinable()) {
        stop_ = true;
        post([]() {});
        thread_.join();
    }
    running_ = false;
    if (bus_) {
        sd_bus_detach_event(bus_);
        bus_ = sd_bus_flush_close_unref(bus_);
    }
    if (event_) event_ = sd_event_unref(event_);
    if (wake_fd_ >= 0) {
        ::close(wake_fd_);
        wake_fd_ = -1;
    }
    services_.clear();
    attrs_.clear();
    for (auto& r : report_by_id_) r = nullptr;
    adv_ = Adv::Off;
    want_adv_ = false;
    pending_discoverable_ = -1;
}

// --- public API (any thread) --------------------------------------------------------

LinkState BleTransport::state() {
    if (!running_) return LinkState::Down;
    std::lock_guard<std::mutex> lock(mu_);
    bool linked = false;
    for (const auto& [path, d] : devices_) {
        if (d.connected && device_is_ours(d)) linked = true;
    }
    if (!linked) return LinkState::Waiting;
    const Attr* kbd = report_by_id_[tools::hid::kReportKeyboard];
    if (kbd && kbd->notifying) {
        last_error_.clear(); // a working link supersedes earlier registration noise
        return LinkState::Ready;
    }
    return LinkState::Handshake;
}

std::string BleTransport::host_label() {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& [path, d] : devices_) {
        if (d.connected && device_is_ours(d)) return d.alias.empty() ? d.addr : d.alias;
    }
    return {};
}

std::string BleTransport::detail() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!last_error_.empty()) return last_error_;
    }
    switch (state()) {
    case LinkState::Ready: return "Connected: " + host_label();
    case LinkState::Handshake: return "Securing link with " + host_label() + "...";
    case LinkState::Waiting:
        return pairing_ ? "Pair from the host: " + name_ : "Waiting for a paired host";
    default: return "Bluetooth stopped";
    }
}

bool BleTransport::write_report(uint8_t id, const uint8_t* data, size_t len) {
    if (!running_ || id == 0 || id >= 4) return false;
    std::vector<uint8_t> v(data, data + len);
    {
        std::lock_guard<std::mutex> lock(mu_);
        const Attr* a = report_by_id_[id];
        if (!a || !a->notifying) return false;
    }
    post([this, id, v = std::move(v)]() {
        Attr* a = report_by_id_[id];
        if (!a || !bus_) return;
        a->value = v;
        sd_bus_emit_properties_changed(bus_, a->path.c_str(), "org.bluez.GattCharacteristic1", "Value", nullptr);
    });
    return true;
}

std::vector<HostInfo> BleTransport::hosts() {
    std::vector<HostInfo> out;
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& [path, d] : devices_) {
        if (!d.paired && !d.connected) continue;
        HostInfo h;
        h.addr = d.addr;
        h.name = d.alias.empty() ? d.addr : d.alias;
        h.paired = d.paired;
        // Only report our own HID link as connected.
        h.connected = d.connected && device_is_ours(d);
        // Paired devices that never used our HID service (headphones...) are not hosts.
        if (!device_is_ours(d)) continue;
        out.push_back(std::move(h));
    }
    return out;
}

void BleTransport::set_known_hosts(const std::vector<std::string>& addrs) {
    std::lock_guard<std::mutex> lock(mu_);
    known_hosts_.clear();
    for (const auto& a : addrs) {
        if (!a.empty()) known_hosts_.insert(a);
    }
}

void BleTransport::set_pairing(bool open) {
    if (pairing_ == open) return;
    pairing_ = open;
    post([this, open]() {
        set_adapter_bool("Pairable", open);
        // Withdraw the advert, switch discoverable, advertise again (reconcile_adv).
        pending_discoverable_ = open ? 1 : 0;
        reconcile_adv();
    });
}

void BleTransport::set_selected_host(const std::string& addr) {
    std::lock_guard<std::mutex> lock(mu_);
    selected_ = addr;
    new_only_block_.clear();
    if (addr == kNewHostOnly) {
        for (const auto& [path, d] : devices_) {
            if (d.paired) new_only_block_.insert(d.addr);
        }
    }
}

bool BleTransport::disconnect_host() {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [path, d] : devices_) {
            if (d.connected && device_is_ours(d)) paths.push_back(path);
        }
    }
    if (paths.empty()) return false;
    post([this, paths]() {
        for (const auto& p : paths) call_async(p, "org.bluez.Device1", "Disconnect", "disconnect");
    });
    return true;
}

bool BleTransport::forget_host(const std::string& addr) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [p, d] : devices_) {
            if (d.addr == addr) path = p;
        }
        known_hosts_.erase(addr);
    }
    if (path.empty()) return false;
    post([this, path]() {
        sd_bus_call_method_async(bus_, nullptr, "org.bluez", adapter_.c_str(), "org.bluez.Adapter1", "RemoveDevice",
                                 on_reply, this, "o", path.c_str());
    });
    return true;
}

bool BleTransport::forget_all() {
    std::vector<std::string> addrs;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [p, d] : devices_) {
            if (device_is_ours(d)) addrs.push_back(d.addr);
        }
    }
    bool any = false;
    for (const auto& a : addrs) any = forget_host(a) || any;
    return any;
}

bool BleTransport::rename_host(const std::string& addr, const std::string& name) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& [p, d] : devices_) {
            if (d.addr == addr) path = p;
        }
    }
    if (path.empty()) return false;
    post([this, path, name]() { set_property(path, "org.bluez.Device1", "Alias", 's', name.c_str()); });
    return true;
}

} // namespace

std::unique_ptr<Transport> make_ble_transport(const std::string& device_name) {
    return std::make_unique<BleTransport>(device_name.empty() ? tools::hid::kDefaultBleName : device_name);
}

void set_ble_known_hosts(Transport& t, const std::vector<std::string>& addrs) {
    if (std::strcmp(t.name(), "BLE") != 0) return;
    static_cast<BleTransport&>(t).set_known_hosts(addrs);
}

} // namespace platform::hid

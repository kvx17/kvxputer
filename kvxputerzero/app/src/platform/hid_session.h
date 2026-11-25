#pragma once

// kvxkeyboard HID transports (USB gadget, BLE HID-over-GATT, desktop simulator)
// and the session that serializes report sequences on a worker thread.

#include "hid_lib.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform::hid {

enum class LinkState {
    Down,       // transport not running
    Waiting,    // running, no host
    Handshake,  // host present, not ready for reports yet (BLE pairing / CCCD)
    Ready,      // host is accepting reports
};

const char* link_state_label(LinkState s);

struct HostInfo {
    std::string addr;
    std::string name;
    bool paired{false};
    bool connected{false};
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual const char* name() const = 0; // "USB" / "BLE" / "SIM"
    virtual bool start(std::string& error) = 0;
    virtual void stop() = 0;
    virtual LinkState state() = 0;
    // Connected host name when known ("USB host", BLE alias …).
    virtual std::string host_label() = 0;
    // One short line explaining the current state (shown under the header).
    virtual std::string detail() = 0;
    // `data` excludes the report ID. Returns false when the host is not reading.
    virtual bool write_report(uint8_t report_id, const uint8_t* data, size_t len) = 0;

    // --- BLE host management; USB and the simulator have a single fixed host. ---
    virtual bool has_hosts() const { return false; }
    virtual std::vector<HostInfo> hosts() { return {}; }
    // Open: accept a new host to pair. Closed: only bonded hosts may connect.
    virtual void set_pairing(bool /*open*/) {}
    virtual bool pairing() const { return false; }
    // Non-empty: any other connecting host is dropped. Empty: any bonded host.
    virtual void set_selected_host(const std::string& /*addr*/) {}
    virtual bool disconnect_host() { return false; }
    virtual bool forget_host(const std::string& /*addr*/) { return false; }
    virtual bool forget_all() { return false; }
    virtual bool rename_host(const std::string& /*addr*/, const std::string& /*name*/) { return false; }
};

std::unique_ptr<Transport> make_usb_transport();
std::unique_ptr<Transport> make_ble_transport(const std::string& device_name);
std::unique_ptr<Transport> make_sim_transport();
// BLE only: addresses the user keeps in host slots. Host policy (drop / accept)
// only ever applies to these and to devices that used our HID service.
void set_ble_known_hosts(Transport& t, const std::vector<std::string>& addrs);
// USB gadget helper path, or empty when not installed.
std::string find_hidgadget_helper();

// Run kvxputerzero-hidgadget <verb>. Uses sudo when the helper is not setuid
// and a password was accepted this session.
bool run_hidgadget(const std::string& helper, const char* verb, std::string& output, std::string& error);
// The Cardputer Zero Host/Slave switch is physical. True while it is on Host
// (the internal hub and its devices are on the CM0 USB bus).
bool usb_switch_on_host();
// Unblock the Bluetooth radio, start bluetoothd and grant this user BlueZ access
// (via the privileged helper). Idempotent. `note` gets a short result line.
bool prepare_bluetooth(std::string& note);

// Serializes HID traffic. Every call returns immediately; reports go out in
// order on a worker thread with the make/break gaps hosts need.
class Session {
public:
    explicit Session(std::unique_ptr<Transport> transport);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Transport& transport() { return *transport_; }
    bool start(std::string& error);
    void stop();
    bool running() const { return running_; }

    // Press and release one key (mods + usage). hold_ms is the make/break gap.
    void tap(uint8_t mods, uint8_t usage, int hold_ms = 25);
    void tap(const tools::hid::Stroke& s, int hold_ms = 25) { tap(s.mods, s.usage, hold_ms); }
    // Function keys and arrows need a longer make window on some Linux hosts.
    void tap_slow(uint8_t mods, uint8_t usage) { tap(mods, usage, 160); }
    // Held keys (push-to-talk). key_up() releases everything.
    void key_down(uint8_t mods, uint8_t usage);
    void key_up();
    void consumer(uint16_t usage);
    void mouse_move(int dx, int dy, int wheel = 0);
    void mouse_click(uint8_t buttons);
    void release_all();
    // Drop anything not yet sent (mode exit).
    void flush();

    // Total reports the host rejected (not reading / not subscribed).
    uint32_t dropped() const { return dropped_.load(); }
    uint32_t sent() const { return sent_.load(); }

private:
    struct Op {
        uint8_t report_id{0};
        uint8_t data[tools::hid::kKeyboardReportLen]{};
        uint8_t len{0};
        int delay_after_ms{0};
    };

    void push(std::initializer_list<Op> ops);
    void worker_loop();
    static Op keyboard_op(uint8_t mods, uint8_t usage, int delay);
    static Op consumer_op(uint16_t usage, int delay);
    static Op mouse_op(uint8_t buttons, int dx, int dy, int wheel, int delay);

    std::unique_ptr<Transport> transport_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Op> q_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> dropped_{0};
    std::atomic<uint32_t> sent_{0};
    std::thread worker_;
};

} // namespace platform::hid

#include "hid_session.h"

#include "elevate.h"
#include "shell_util.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>

namespace platform::hid {

const char* link_state_label(LinkState s) {
    switch (s) {
    case LinkState::Down: return "Stopped";
    case LinkState::Waiting: return "Waiting for host";
    case LinkState::Handshake: return "Linking";
    case LinkState::Ready: return "Connected";
    }
    return "?";
}

std::string find_hidgadget_helper() {
    const char* candidates[] = {
        "/usr/lib/kvxputerzero/kvxputerzero-hidgadget",
        "/usr/libexec/kvxputerzero/kvxputerzero-hidgadget",
        "/usr/bin/kvxputerzero-hidgadget",
    };
    for (const char* c : candidates) {
        if (::access(c, X_OK) == 0) return c;
    }
    // Build tree: helper sits next to the app binary.
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        const auto sibling = self.parent_path() / "kvxputerzero-hidgadget";
        if (::access(sibling.c_str(), X_OK) == 0) return sibling.string();
    }
    return {};
}

bool run_hidgadget(const std::string& helper, const char* verb, std::string& output, std::string& error) {
    output.clear();
    error.clear();
    if (helper.empty() || !verb) {
        error = "kvxputerzero-hidgadget missing";
        return false;
    }
    struct stat st {};
    const bool setuid = ::stat(helper.c_str(), &st) == 0 && (st.st_mode & S_ISUID);
    if (::geteuid() == 0 || setuid) {
        std::string quoted = "'";
        for (char c : helper) {
            if (c == '\'') quoted += "'\\''";
            else quoted += c;
        }
        quoted += "' ";
        quoted += verb;
        quoted += " 2>&1";
        platform::run_shell(quoted, output);
        return true;
    }
    if (!platform::sudo_exec({helper, verb}, output, error)) return false;
    return true;
}

bool usb_switch_on_host() {
#if KVX_DESKTOP_BUILD
    return false;
#else
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/bus/usb/devices", ec)) {
        const std::string name = e.path().filename().string();
        // "1-1", "1-1.3" are devices behind a port; "usb1" is a root hub and
        // "1-1:1.0" an interface.
        if (name.find('-') != std::string::npos && name.find(':') == std::string::npos) return true;
    }
    return false;
#endif
}

bool prepare_bluetooth(std::string& note) {
#if KVX_DESKTOP_BUILD
    note = "desktop: using the system Bluetooth setup";
    return true;
#else
    const std::string helper = find_hidgadget_helper();
    if (helper.empty()) {
        note = "kvxputerzero-hidgadget missing (reinstall the app)";
        return false;
    }
    std::string out;
    std::string run_err;
    if (!run_hidgadget(helper, "ble-prep", out, run_err)) {
        note = run_err.empty() ? "Bluetooth setup failed" : run_err;
        return false;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (out.rfind("OK", 0) == 0) {
        note = out.size() > 3 ? out.substr(3) : "ready";
        return true;
    }
    note = out.rfind("ERR ", 0) == 0 ? out.substr(4) : (out.empty() ? "Bluetooth setup failed" : out);
    return false;
#endif
}

// --- Desktop simulator -----------------------------------------------------------

namespace {

class SimTransport final : public Transport {
public:
    const char* name() const override { return "SIM"; }
    bool start(std::string& /*error*/) override {
        running_ = true;
        return true;
    }
    void stop() override { running_ = false; }
    LinkState state() override { return running_ ? LinkState::Ready : LinkState::Down; }
    std::string host_label() override { return "Simulator"; }
    std::string detail() override { return "Simulator: reports go to stderr"; }
    bool write_report(uint8_t id, const uint8_t* data, size_t len) override {
        if (!running_) return false;
        char buf[64];
        int n = std::snprintf(buf, sizeof(buf), "id%u:", id);
        for (size_t i = 0; i < len && n < static_cast<int>(sizeof(buf)) - 4; ++i) {
            n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), " %02X", data[i]);
        }
        std::lock_guard<std::mutex> lock(mu_);
        last_ = buf;
        std::fprintf(stderr, "[hid-sim] %s\n", buf);
        return true;
    }

    // Simulated BLE-style hosts so the slot / pairing UI can be tried on a desktop:
    // opening pairing "pairs" a new host after ~1.2 s; selecting one connects it.
    bool has_hosts() const override { return true; }
    std::vector<HostInfo> hosts() override {
        std::lock_guard<std::mutex> lock(mu_);
        if (pairing_ && std::chrono::steady_clock::now() >= pair_at_) {
            HostInfo h;
            h.addr = "SIM:00:00:00:00:" + std::to_string(10 + hosts_.size());
            h.name = "Sim host " + std::to_string(hosts_.size() + 1);
            h.paired = true;
            for (auto& o : hosts_) o.connected = false;
            h.connected = true;
            hosts_.push_back(h);
            pairing_ = false;
        }
        return hosts_;
    }
    void set_pairing(bool open) override {
        std::lock_guard<std::mutex> lock(mu_);
        pairing_ = open;
        pair_at_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    }
    bool pairing() const override { return pairing_; }
    void set_selected_host(const std::string& addr) override {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& h : hosts_) h.connected = !addr.empty() && h.addr == addr;
    }
    bool disconnect_host() override {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& h : hosts_) h.connected = false;
        return true;
    }
    bool forget_host(const std::string& addr) override {
        std::lock_guard<std::mutex> lock(mu_);
        hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(), [&](const HostInfo& h) { return h.addr == addr; }),
                     hosts_.end());
        return true;
    }
    bool forget_all() override {
        std::lock_guard<std::mutex> lock(mu_);
        hosts_.clear();
        return true;
    }
    bool rename_host(const std::string& addr, const std::string& name) override {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& h : hosts_) {
            if (h.addr == addr) h.name = name;
        }
        return true;
    }

private:
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string last_;
    std::vector<HostInfo> hosts_;
    std::atomic<bool> pairing_{false};
    std::chrono::steady_clock::time_point pair_at_{};
};

} // namespace

std::unique_ptr<Transport> make_sim_transport() {
    return std::make_unique<SimTransport>();
}

// --- Session -----------------------------------------------------------------------

Session::Session(std::unique_ptr<Transport> transport) : transport_(std::move(transport)) {}

Session::~Session() {
    stop();
}

bool Session::start(std::string& error) {
    if (running_) return true;
    if (!transport_ || !transport_->start(error)) return false;
    stop_ = false;
    running_ = true;
    worker_ = std::thread([this]() { worker_loop(); });
    return true;
}

void Session::stop() {
    if (worker_.joinable()) {
        // Never leave a key or button held on the host.
        release_all();
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }
    if (running_ && transport_) transport_->stop();
    running_ = false;
}

Session::Op Session::keyboard_op(uint8_t mods, uint8_t usage, int delay) {
    Op op;
    op.report_id = tools::hid::kReportKeyboard;
    op.len = tools::hid::kKeyboardReportLen;
    op.data[0] = mods;
    op.data[2] = usage;
    op.delay_after_ms = delay;
    return op;
}

Session::Op Session::consumer_op(uint16_t usage, int delay) {
    Op op;
    op.report_id = tools::hid::kReportConsumer;
    op.len = tools::hid::kConsumerReportLen;
    op.data[0] = static_cast<uint8_t>(usage & 0xFF);
    op.data[1] = static_cast<uint8_t>(usage >> 8);
    op.delay_after_ms = delay;
    return op;
}

Session::Op Session::mouse_op(uint8_t buttons, int dx, int dy, int wheel, int delay) {
    Op op;
    op.report_id = tools::hid::kReportMouse;
    op.len = tools::hid::kMouseReportLen;
    op.data[0] = buttons;
    op.data[1] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(dx, -127, 127)));
    op.data[2] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(dy, -127, 127)));
    op.data[3] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(wheel, -127, 127)));
    op.delay_after_ms = delay;
    return op;
}

void Session::push(std::initializer_list<Op> ops) {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        // A stalled host must not grow the queue without bound.
        if (q_.size() > 256) return;
        for (const auto& op : ops) q_.push_back(op);
    }
    cv_.notify_one();
}

void Session::tap(uint8_t mods, uint8_t usage, int hold_ms) {
    push({keyboard_op(mods, usage, hold_ms), keyboard_op(0, 0, 12)});
}

void Session::key_down(uint8_t mods, uint8_t usage) {
    push({keyboard_op(mods, usage, 0)});
}

void Session::key_up() {
    push({keyboard_op(0, 0, 0)});
}

void Session::consumer(uint16_t usage) {
    push({consumer_op(usage, 30), consumer_op(0, 12)});
}

void Session::mouse_move(int dx, int dy, int wheel) {
    // Split large moves so each report stays inside the int8 range.
    while (dx != 0 || dy != 0 || wheel != 0) {
        const int sx = std::clamp(dx, -127, 127);
        const int sy = std::clamp(dy, -127, 127);
        const int sw = std::clamp(wheel, -127, 127);
        push({mouse_op(0, sx, sy, sw, 4)});
        dx -= sx;
        dy -= sy;
        wheel -= sw;
    }
}

void Session::mouse_click(uint8_t buttons) {
    push({mouse_op(buttons, 0, 0, 0, 30), mouse_op(0, 0, 0, 0, 12)});
}

void Session::release_all() {
    push({keyboard_op(0, 0, 0), consumer_op(0, 0), mouse_op(0, 0, 0, 0, 0)});
}

void Session::flush() {
    std::lock_guard<std::mutex> lock(mu_);
    q_.clear();
}

void Session::worker_loop() {
    for (;;) {
        Op op;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]() { return stop_ || !q_.empty(); });
            // Drain the release reports queued by stop() before leaving.
            if (q_.empty()) return;
            op = q_.front();
            q_.pop_front();
        }
        if (transport_->write_report(op.report_id, op.data, op.len)) {
            ++sent_;
        } else {
            ++dropped_;
        }
        if (op.delay_after_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(op.delay_after_ms));
        }
    }
}

} // namespace platform::hid

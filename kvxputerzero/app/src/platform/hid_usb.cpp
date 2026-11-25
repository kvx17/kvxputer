// USB transport for kvxkeyboard HID: configfs gadget via kvxputerzero-hidgadget.

#include "hid_session.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unistd.h>

namespace platform::hid {
namespace {

std::string read_line(const std::string& path) {
    std::ifstream in(path);
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

class UsbTransport final : public Transport {
public:
    ~UsbTransport() override { stop(); }

    const char* name() const override { return "USB"; }

    bool start(std::string& error) override {
        if (fd_ >= 0) return true;
        helper_ = find_hidgadget_helper();
        if (helper_.empty()) {
            error = "kvxputerzero-hidgadget missing (reinstall the package)";
            return false;
        }
        std::string out;
        std::string run_err;
        if (!run_hidgadget(helper_, "up", out, run_err)) {
            error = run_err.empty() ? "USB gadget failed" : run_err;
            return false;
        }
        std::istringstream ss(out);
        std::string tag, udc, node;
        ss >> tag;
        if (tag != "OK") {
            const auto sp = out.find(' ');
            error = sp == std::string::npos ? "USB gadget failed" : out.substr(sp + 1);
            while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
            if (error == "SWITCH_HOST") error = "Slide the USB switch (left side) to Slave";
            return false;
        }
        ss >> udc >> node;
        fd_ = ::open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            error = "open " + node + ": " + std::strerror(errno);
            stop();
            return false;
        }
        udc_ = udc;
        node_ = node;
        up_ = true;
        return true;
    }

    void stop() override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (up_ && !helper_.empty()) {
            std::string out;
            std::string run_err;
            (void)run_hidgadget(helper_, "down", out, run_err);
        }
        up_ = false;
    }

    LinkState state() override {
        if (fd_ < 0) return LinkState::Down;
        const std::string st = read_line("/sys/class/udc/" + udc_ + "/state");
        if (st == "configured") return LinkState::Ready;
        if (st == "addressed" || st == "default") return LinkState::Handshake;
        return LinkState::Waiting;
    }

    std::string host_label() override { return state() == LinkState::Ready ? "USB host" : ""; }

    std::string detail() override {
        switch (state()) {
        case LinkState::Ready: return "Enumerated on " + node_;
        case LinkState::Handshake: return "Host is enumerating...";
        case LinkState::Waiting: return "Plug USB-C2 into the host (switch on Slave)";
        default: return "USB gadget stopped";
        }
    }

    bool write_report(uint8_t id, const uint8_t* data, size_t len) override {
        if (fd_ < 0 || len + 1 > sizeof(buf_)) return false;
        std::lock_guard<std::mutex> lock(mu_);
        buf_[0] = id;
        std::memcpy(buf_ + 1, data, len);
        // Non-blocking: EAGAIN / ESHUTDOWN mean the host is not polling.
        const ssize_t n = ::write(fd_, buf_, len + 1);
        return n == static_cast<ssize_t>(len + 1);
    }

private:
    std::string helper_;
    std::string udc_;
    std::string node_;
    int fd_{-1};
    bool up_{false};
    std::mutex mu_;
    uint8_t buf_[tools::hid::kMaxReportLen + 1]{};
};

} // namespace

std::unique_ptr<Transport> make_usb_transport() {
    return std::make_unique<UsbTransport>();
}

} // namespace platform::hid

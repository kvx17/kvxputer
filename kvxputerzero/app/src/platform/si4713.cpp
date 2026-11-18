#include "si4713.h"

#include "cap_detect.h"

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr uint8_t kAddr = 0x63;

} // namespace

struct Si4713Session::Impl {
    int fd{-1};
};

bool Si4713Session::open(int i2c_bus, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    const auto info = detect_si4713(i2c_bus);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    const int fd = ::open(info.path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "Si4713 open failed";
        return false;
    }
    if (ioctl(fd, I2C_SLAVE, kAddr) < 0) {
        ::close(fd);
        error = "Si4713 I2C_SLAVE failed";
        return false;
    }
    impl_ = new Impl();
    impl_->fd = fd;
    return true;
}

void Si4713Session::close() {
    if (impl_) {
        if (impl_->fd >= 0) ::close(impl_->fd);
        delete impl_;
        impl_ = nullptr;
    }
}

bool Si4713Session::ok() const {
    return impl_ && impl_->fd >= 0;
}

bool Si4713Session::status_lines(std::vector<std::string>& lines, std::string& error) {
    lines.clear();
    if (!ok()) {
        error = "Si4713 not open";
        return false;
    }
    lines.push_back("Si4713 present @ 0x63");
    lines.push_back("TX API: POWER_UP / TX_TUNE_FREQ");
    return true;
}

bool Si4713Session::broadcast_start(int freq_10khz, std::string& error) {
    if (!ok()) {
        error = "Si4713 not open";
        return false;
    }
    if (freq_10khz < 7600 || freq_10khz > 10800) {
        error = "freq out of range";
        return false;
    }
    // POWER_UP (0x01) analog audio; args simplified — may need CTS polling on real silicon.
    uint8_t power_up[] = {0x01, 0x12, 0x50};
    if (::write(impl_->fd, power_up, sizeof(power_up)) < 0) {
        error = "POWER_UP failed";
        return false;
    }
    uint8_t tune[] = {0x30, 0x00, static_cast<uint8_t>((freq_10khz >> 8) & 0xFF),
                      static_cast<uint8_t>(freq_10khz & 0xFF)};
    if (::write(impl_->fd, tune, sizeof(tune)) < 0) {
        error = "TX_TUNE_FREQ failed";
        return false;
    }
    return true;
}

bool Si4713Session::broadcast_stop(std::string& error) {
    if (!ok()) {
        error = "Si4713 not open";
        return false;
    }
    uint8_t power_down[] = {0x11};
    if (::write(impl_->fd, power_down, sizeof(power_down)) < 0) {
        error = "POWER_DOWN failed";
        return false;
    }
    return true;
}

bool Si4713Session::spectrum_sample(std::vector<std::string>& lines, std::string& error) {
    lines.clear();
    if (!ok()) {
        error = "Si4713 not open";
        return false;
    }
    // Without full ASQ/RSSI command plumbing, report probe points.
    for (int f = 8800; f <= 10800; f += 200) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%d.%02d MHz  (probe)", f / 100, f % 100);
        lines.push_back(buf);
    }
    lines.push_back("Full FM spectrum needs Cap TX ASQ — listed band grid");
    return true;
}

} // namespace platform

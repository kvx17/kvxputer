#include "pn532_bus.h"

#include "cap_detect.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr uint8_t kAddr = 0x24;

} // namespace

struct Pn532Session::Impl {
    int fd{-1};
    std::string path;

    bool write_frame(const uint8_t* data, size_t len, std::string& error) {
        // PN532 I2C: READY poll then host->PN532 frame
        std::vector<uint8_t> frame;
        frame.push_back(0x00); // preamble
        frame.push_back(0x00);
        frame.push_back(0xFF);
        frame.push_back(static_cast<uint8_t>(len + 1));
        frame.push_back(static_cast<uint8_t>(~frame[3] + 1));
        frame.push_back(0xD4); // TFI host to PN532
        frame.insert(frame.end(), data, data + len);
        uint8_t sum = 0xD4;
        for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
        frame.push_back(static_cast<uint8_t>(~sum + 1));
        frame.push_back(0x00);
        if (::write(fd, frame.data(), frame.size()) < 0) {
            error = "PN532 write failed";
            return false;
        }
        return true;
    }

    bool wait_ready(int timeout_ms, std::string& error, bool* cancel) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            uint8_t b = 0;
            if (::read(fd, &b, 1) == 1 && (b & 1)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        error = "PN532 timeout";
        return false;
    }
};

bool Pn532Session::open(int i2c_bus, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    const auto info = detect_pn532(i2c_bus);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    const int fd = ::open(info.path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "PN532 open failed";
        return false;
    }
    if (ioctl(fd, I2C_SLAVE, kAddr) < 0) {
        ::close(fd);
        error = "PN532 I2C_SLAVE failed";
        return false;
    }
    impl_ = new Impl();
    impl_->fd = fd;
    impl_->path = info.path;
    return true;
}

void Pn532Session::close() {
    if (impl_) {
        if (impl_->fd >= 0) ::close(impl_->fd);
        delete impl_;
        impl_ = nullptr;
    }
}

bool Pn532Session::ok() const {
    return impl_ && impl_->fd >= 0;
}

bool Pn532Session::get_firmware(std::string& out, std::string& error) {
    out.clear();
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    const uint8_t cmd[] = {0x02}; // GetFirmwareVersion
    if (!impl_->write_frame(cmd, 1, error)) return false;
    if (!impl_->wait_ready(500, error, nullptr)) return false;
    uint8_t buf[32] = {0};
    const ssize_t n = ::read(impl_->fd, buf, sizeof(buf));
    if (n < 10) {
        error = "PN532 short reply";
        return false;
    }
    // Best-effort parse: look for 0xD5 0x03 then IC/ver/rev/support
    for (ssize_t i = 0; i + 5 < n; ++i) {
        if (buf[i] == 0xD5 && buf[i + 1] == 0x03) {
            char line[64];
            std::snprintf(line, sizeof(line), "IC=0x%02X ver=%u.%u", buf[i + 2], buf[i + 3], buf[i + 4]);
            out = line;
            return true;
        }
    }
    out = "firmware reply ok";
    return true;
}

bool Pn532Session::read_uid(std::string& uid_hex, std::string& error, bool* cancel, int timeout_ms) {
    uid_hex.clear();
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            return false;
        }
        // InListPassiveTarget: max 1 target, 106kbps type A
        const uint8_t cmd[] = {0x4A, 0x01, 0x00};
        if (!impl_->write_frame(cmd, 3, error)) return false;
        if (!impl_->wait_ready(300, error, cancel)) {
            if (error == "cancelled") return false;
            error.clear();
            continue;
        }
        uint8_t buf[64] = {0};
        const ssize_t n = ::read(impl_->fd, buf, sizeof(buf));
        if (n < 12) continue;
        for (ssize_t i = 0; i + 8 < n; ++i) {
            if (buf[i] == 0xD5 && buf[i + 1] == 0x4B && buf[i + 2] >= 1) {
                const uint8_t uidlen = buf[i + 7];
                if (uidlen == 0 || i + 8 + uidlen > n) continue;
                char hex[40];
                size_t p = 0;
                for (uint8_t u = 0; u < uidlen && p + 3 < sizeof(hex); ++u) {
                    p += static_cast<size_t>(
                        std::snprintf(hex + p, sizeof(hex) - p, "%02X", buf[i + 8 + u]));
                }
                uid_hex = hex;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    error = "no tag";
    return false;
}

bool Pn532Session::save_dump(const std::string& path, const std::string& uid_hex, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: kvxputerzero NFC dump\nUID: " << uid_hex << "\n";
    return true;
}

} // namespace platform

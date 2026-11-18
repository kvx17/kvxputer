#include "cc1101.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "spi_device.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <thread>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

} // namespace

struct Cc1101Session::Impl {
    std::unique_ptr<SpiDevice> spi;
};

bool Cc1101Session::open(const std::string& spidev, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    if (!enable_cap_power(CapKindHint::Cc1101, error)) return false;
    const auto info = detect_cc1101(spidev);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    SpiConfig cfg;
    cfg.path = info.path;
    cfg.speed_hz = 500000;
    auto spi = SpiDevice::open(cfg, error);
    if (!spi) return false;
    impl_ = new Impl();
    impl_->spi = std::move(spi);
    return soft_reset(error);
}

void Cc1101Session::close() {
    if (impl_) {
        delete impl_;
        impl_ = nullptr;
    }
}

bool Cc1101Session::ok() const {
    return impl_ && impl_->spi && impl_->spi->ok();
}

bool Cc1101Session::soft_reset(std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    uint8_t tx[1] = {0x30}; // SRES
    uint8_t rx[1] = {0};
    return impl_->spi->transfer(tx, rx, 1, error);
}

bool Cc1101Session::set_frequency_hz(uint32_t hz, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    // FREQ = f_carrier * 2^16 / f_xosc ; assume 26 MHz crystal.
    constexpr double fxosc = 26000000.0;
    const uint32_t freq = static_cast<uint32_t>(std::lround((static_cast<double>(hz) * 65536.0) / fxosc));
    const uint8_t f2 = static_cast<uint8_t>((freq >> 16) & 0xFF);
    const uint8_t f1 = static_cast<uint8_t>((freq >> 8) & 0xFF);
    const uint8_t f0 = static_cast<uint8_t>(freq & 0xFF);
    auto write_reg = [&](uint8_t reg, uint8_t val) -> bool {
        uint8_t tx[2] = {reg, val};
        uint8_t rx[2] = {0, 0};
        return impl_->spi->transfer(tx, rx, 2, error);
    };
    return write_reg(0x0D, f2) && write_reg(0x0E, f1) && write_reg(0x0F, f0);
}

bool Cc1101Session::read_rssi_dbm(int& dbm, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    // SRX
    uint8_t cmd[1] = {0x34};
    uint8_t crx[1] = {0};
    if (!impl_->spi->transfer(cmd, crx, 1, error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    // RSSI is a status register — must use 0xC0 | addr (not 0x80).
    uint8_t tx[2] = {static_cast<uint8_t>(0xC0 | 0x34), 0x00};

    uint8_t rx[2] = {0, 0};
    if (!impl_->spi->transfer(tx, rx, 2, error)) return false;
    const int8_t raw = static_cast<int8_t>(rx[1]);
    dbm = static_cast<int>(raw) / 2 - 74;
    // Idle
    uint8_t idle[1] = {0x36}; // SIDLE
    uint8_t irx[1] = {0};
    (void)impl_->spi->transfer(idle, irx, 1, error);
    return true;
}

bool Cc1101Session::spectrum_scan(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz,
                                  std::vector<std::string>& lines, std::string& error, bool* cancel) {
    lines.clear();
    if (step_hz == 0) step_hz = 100000;
    if (stop_hz < start_hz) {
        error = "bad range";
        return false;
    }
    for (uint32_t f = start_hz; f <= stop_hz; f += step_hz) {
        if (cancel && *cancel) break;
        if (!set_frequency_hz(f, error)) return false;
        int dbm = -120;
        if (!read_rssi_dbm(dbm, error)) return false;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%u,%d", f, dbm);
        lines.push_back(buf);
        if (lines.size() >= 200) break;
    }
    return true;
}

bool Cc1101Session::listen_rssi(int samples, int delay_ms, std::vector<int>& out, std::string& error,
                                bool* cancel) {
    out.clear();
    if (samples <= 0) samples = 40;
    if (delay_ms < 1) delay_ms = 5;
    for (int i = 0; i < samples; ++i) {
        if (cancel && *cancel) break;
        int dbm = -120;
        if (!read_rssi_dbm(dbm, error)) return false;
        out.push_back(dbm);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return true;
}

bool Cc1101Session::send_test_packet(const uint8_t* payload, size_t len, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    if (!payload || len == 0 || len > 60) {
        error = "bad payload";
        return false;
    }
    // Flush TX FIFO, write length + payload via burst 0x7F, STX 0x35
    uint8_t flush[1] = {0x3B}; // SFTX
    uint8_t rx1[1] = {0};
    if (!impl_->spi->transfer(flush, rx1, 1, error)) return false;
    std::vector<uint8_t> tx(2 + len);
    tx[0] = 0x7F; // burst TXFIFO
    tx[1] = static_cast<uint8_t>(len);
    std::memcpy(tx.data() + 2, payload, len);
    std::vector<uint8_t> rx(tx.size(), 0);
    if (!impl_->spi->transfer(tx.data(), rx.data(), tx.size(), error)) return false;
    uint8_t stx[1] = {0x35};
    uint8_t rx2[1] = {0};
    if (!impl_->spi->transfer(stx, rx2, 1, error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    uint8_t idle[1] = {0x36};
    uint8_t rx3[1] = {0};
    return impl_->spi->transfer(idle, rx3, 1, error);
}

bool write_sub_raw(const std::string& path, uint32_t freq_hz, const std::vector<int>& rssi_samples,
                   std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: Flipper SubGhz RAW File\n";
    out << "Version: 1\n";
    out << "Frequency: " << freq_hz << "\n";
    out << "Preset: FuriHalSubGhzPresetOok650Async\n";
    out << "Protocol: RAW\n";
    out << "RAW_Data:";
    // Encode RSSI samples as fake pulse widths for archival (not bit-perfect OOK).
    for (int v : rssi_samples) {
        const int mark = (v > -70) ? 400 : 100;
        const int space = 200;
        out << " " << mark << " -" << space;
    }
    out << "\n";
    return true;
}

bool list_sub_files(const std::string& dir, std::vector<std::string>& names, std::string& error) {
    names.clear();
    DIR* d = opendir(dir.c_str());
    if (!d) {
        error = "cannot open dir";
        return false;
    }
    while (dirent* ent = readdir(d)) {
        const char* n = ent->d_name;
        const size_t len = std::strlen(n);
        if (len > 4 && std::strcmp(n + len - 4, ".sub") == 0) names.push_back(n);
    }
    closedir(d);
    return true;
}

} // namespace platform

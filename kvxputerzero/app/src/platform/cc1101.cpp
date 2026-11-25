#include "cc1101.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "rf_settings.h"
#include "spi_device.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <time.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr double kFxosc = 26000000.0;

// Strobe commands
constexpr uint8_t SRES = 0x30;
constexpr uint8_t SFSTXON = 0x31;
constexpr uint8_t SXOFF = 0x32;
constexpr uint8_t SCAL = 0x33;
constexpr uint8_t SRX = 0x34;
constexpr uint8_t STX = 0x35;
constexpr uint8_t SIDLE = 0x36;
constexpr uint8_t SFTX = 0x3B;
constexpr uint8_t SFRX = 0x3A;

// Config regs
constexpr uint8_t IOCFG2 = 0x00;
constexpr uint8_t IOCFG1 = 0x01;
constexpr uint8_t IOCFG0 = 0x02;
constexpr uint8_t FIFOTHR = 0x03;
constexpr uint8_t SYNC1 = 0x04;
constexpr uint8_t SYNC0 = 0x05;
constexpr uint8_t PKTLEN = 0x06;
constexpr uint8_t PKTCTRL1 = 0x07;
constexpr uint8_t PKTCTRL0 = 0x08;
constexpr uint8_t ADDR = 0x09;
constexpr uint8_t CHANNR = 0x0A;
constexpr uint8_t FSCTRL1 = 0x0B;
constexpr uint8_t FSCTRL0 = 0x0C;
constexpr uint8_t FREQ2 = 0x0D;
constexpr uint8_t FREQ1 = 0x0E;
constexpr uint8_t FREQ0 = 0x0F;
constexpr uint8_t MDMCFG4 = 0x10;
constexpr uint8_t MDMCFG3 = 0x11;
constexpr uint8_t MDMCFG2 = 0x12;
constexpr uint8_t MDMCFG1 = 0x13;
constexpr uint8_t MDMCFG0 = 0x14;
constexpr uint8_t DEVIATN = 0x15;
constexpr uint8_t MCSM2 = 0x16;
constexpr uint8_t MCSM1 = 0x17;
constexpr uint8_t MCSM0 = 0x18;
constexpr uint8_t FOCCFG = 0x19;
constexpr uint8_t BSCFG = 0x1A;
constexpr uint8_t AGCCTRL2 = 0x1B;
constexpr uint8_t AGCCTRL1 = 0x1C;
constexpr uint8_t AGCCTRL0 = 0x1D;
constexpr uint8_t FREND1 = 0x21;
constexpr uint8_t FREND0 = 0x22;
constexpr uint8_t FSCAL3 = 0x23;
constexpr uint8_t FSCAL2 = 0x24;
constexpr uint8_t FSCAL1 = 0x25;
constexpr uint8_t FSCAL0 = 0x26;
constexpr uint8_t TEST2 = 0x2C;
constexpr uint8_t TEST1 = 0x2D;
constexpr uint8_t TEST0 = 0x2E;
constexpr uint8_t PATABLE = 0x3E;

// Status
constexpr uint8_t RSSI = 0x34;
constexpr uint8_t MARCSTATE = 0x35;
constexpr uint8_t PARTNUM = 0x30;
constexpr uint8_t VERSION = 0x31;

// TI CC1101 datasheet PATABLE columns (Min/Low/Mid/Max ≈ −30 / 0 / +5 / +7 dBm).
// Max (+10) is never returned: rf_clamp_pa blocks Cc1101PaLevel::Max at the TX gate.
uint8_t pa_byte(Cc1101PaLevel level, int band_idx) {
    // Columns: 315, 433, 868, 915 (868/915 share synthesizer band index 2 → pick by hz later).
    static constexpr uint8_t kTab[4][4] = {
        // Min   Low   Mid   Max(+7, not +10)
        {0x12, 0x60, 0x85, 0xC8}, // 315
        {0x12, 0x60, 0x85, 0xC8}, // 433 (0xC0 is +10; keep Mid/Max ≤ +7 as 0xC8)
        {0x03, 0x50, 0x85, 0xC8}, // 868
        {0x03, 0x50, 0x85, 0xC8}, // 915
    };
    int col = 1;
    if (band_idx == 0) col = 0;
    else if (band_idx == 1) col = 1;
    else col = 2; // 779–928 → 868 table; 915 uses same column
    const int row = std::clamp(static_cast<int>(level), 0, 3);
    return kTab[col][row];
}

int rssi_offset_for_band(int band_idx) {
    (void)band_idx;
    // Datasheet convert: RSSI_dBm = (RSSI_raw / 2) − offset. Nominal 74 for 26 MHz.
    return 74;
}

int dwell_us_for_bw(float bw_khz) {
    if (bw_khz < 1.f) bw_khz = 650.f;
    // ~8 / RX_BW seconds → µs; clamp so sweeps stay responsive.
    const int us = static_cast<int>(std::lround(8.0e6 / (bw_khz * 1000.0)));
    return std::clamp(us, 400, 2500);
}

void sleep_until_abs(const timespec& deadline) {
    for (;;) {
        const int rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
        if (rc == 0) return;
        if (rc != EINTR) return;
    }
}

timespec add_us(timespec t, int64_t us) {
    t.tv_nsec += us * 1000;
    while (t.tv_nsec >= 1000000000L) {
        t.tv_nsec -= 1000000000L;
        t.tv_sec += 1;
    }
    return t;
}

uint8_t mdmcfg4_for_bw(float khz) {
    // Discrete BW set at 26 MHz: pick nearest from common SmartRF values.
    struct Entry {
        float khz;
        uint8_t chanbw_e;
        uint8_t chanbw_m;
    };
    static constexpr Entry kTab[] = {
        {58.1f, 3, 3},  {67.8f, 3, 2},  {81.4f, 3, 1},  {101.6f, 3, 0}, {116.1f, 2, 3},
        {135.6f, 2, 2}, {162.8f, 2, 1}, {203.1f, 2, 0}, {232.2f, 1, 3}, {270.8f, 1, 2},
        {325.5f, 1, 1}, {406.3f, 1, 0}, {464.3f, 0, 3}, {541.7f, 0, 2}, {650.0f, 0, 1},
        {812.5f, 0, 0},
    };
    float best = 1e9f;
    uint8_t e = 0, m = 2;
    for (const auto& t : kTab) {
        const float d = std::fabs(t.khz - khz);
        if (d < best) {
            best = d;
            e = t.chanbw_e;
            m = t.chanbw_m;
        }
    }
    // Keep DRATE bits from reset-ish mid value (MDMCFG4 low nibble = DRATE_E).
    return static_cast<uint8_t>(((e & 3) << 6) | ((m & 3) << 4) | 0x0C);
}

} // namespace

bool cc1101_freq_supported(uint32_t hz) {
    return cc1101_band_index(hz) >= 0;
}

int cc1101_band_index(uint32_t hz) {
    for (size_t i = 0; i < kCc1101BandCount; ++i) {
        if (hz >= kCc1101Bands[i].start_hz && hz <= kCc1101Bands[i].stop_hz) return static_cast<int>(i);
    }
    return -1;
}

std::vector<std::pair<uint32_t, uint32_t>> cc1101_unsupported_gaps(uint32_t start_hz, uint32_t stop_hz) {
    std::vector<std::pair<uint32_t, uint32_t>> gaps;
    if (stop_hz < start_hz) return gaps;
    uint32_t cursor = start_hz;
    for (size_t i = 0; i < kCc1101BandCount; ++i) {
        const auto& b = kCc1101Bands[i];
        if (b.stop_hz < start_hz) continue;
        if (b.start_hz > stop_hz) break;
        if (cursor < b.start_hz) {
            gaps.emplace_back(cursor, std::min(b.start_hz - 1, stop_hz));
        }
        cursor = std::max(cursor, b.stop_hz + 1);
        if (cursor > stop_hz) break;
    }
    if (cursor <= stop_hz) gaps.emplace_back(cursor, stop_hz);
    return gaps;
}

bool cc1101_plan_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t& step_hz, uint32_t target_bins,
                       std::vector<uint32_t>& freqs_out, std::string& error) {
    freqs_out.clear();
    if (stop_hz < start_hz) {
        error = "bad range";
        return false;
    }
    if (target_bins < 8) target_bins = 8;
    if (target_bins > 160) target_bins = 160;
    if (step_hz < kCc1101TuneQuantumHz) step_hz = kCc1101TuneQuantumHz;

    // Count supported span.
    uint64_t supported = 0;
    for (size_t i = 0; i < kCc1101BandCount; ++i) {
        const uint32_t a = std::max(start_hz, kCc1101Bands[i].start_hz);
        const uint32_t b = std::min(stop_hz, kCc1101Bands[i].stop_hz);
        if (b >= a) supported += (b - a);
    }
    if (supported == 0) {
        error = "range outside CC1101 bands";
        return false;
    }
    const uint32_t adaptive = static_cast<uint32_t>(
        std::max<uint64_t>(kCc1101TuneQuantumHz, supported / std::max<uint32_t>(1, target_bins)));
    if (adaptive > step_hz) step_hz = adaptive;
    // Round step to tune quantum.
    step_hz = ((step_hz + kCc1101TuneQuantumHz / 2) / kCc1101TuneQuantumHz) * kCc1101TuneQuantumHz;
    if (step_hz == 0) step_hz = kCc1101TuneQuantumHz;

    for (size_t i = 0; i < kCc1101BandCount; ++i) {
        const uint32_t a = std::max(start_hz, kCc1101Bands[i].start_hz);
        const uint32_t b = std::min(stop_hz, kCc1101Bands[i].stop_hz);
        if (b < a) continue;
        for (uint32_t f = a; f <= b; f += step_hz) {
            freqs_out.push_back(f);
            if (freqs_out.size() >= target_bins + 20) break;
        }
        if (freqs_out.size() >= target_bins + 20) break;
    }
    if (freqs_out.empty()) {
        error = "no tune points";
        return false;
    }
    return true;
}

struct Cc1101Session::Impl {
    std::unique_ptr<SpiDevice> spi;
};

bool Cc1101Session::strobe(uint8_t cmd, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    uint8_t tx[1] = {cmd};
    uint8_t rx[1] = {0};
    return impl_->spi->transfer(tx, rx, 1, error);
}

bool Cc1101Session::write_reg(uint8_t reg, uint8_t val, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    uint8_t tx[2] = {reg, val};
    uint8_t rx[2] = {0, 0};
    return impl_->spi->transfer(tx, rx, 2, error);
}

bool Cc1101Session::read_status(uint8_t reg, uint8_t& val, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    uint8_t tx[2] = {static_cast<uint8_t>(0xC0 | (reg & 0x3F)), 0x00};
    uint8_t rx[2] = {0, 0};
    if (!impl_->spi->transfer(tx, rx, 2, error)) return false;
    val = rx[1];
    return true;
}

bool Cc1101Session::write_burst(uint8_t addr, const uint8_t* data, size_t len, std::string& error) {
    if (!ok() || !data || len == 0) {
        error = "bad burst";
        return false;
    }
    std::vector<uint8_t> tx(1 + len);
    tx[0] = static_cast<uint8_t>(addr | 0x40);
    std::memcpy(tx.data() + 1, data, len);
    std::vector<uint8_t> rx(tx.size(), 0);
    return impl_->spi->transfer(tx.data(), rx.data(), tx.size(), error);
}

bool Cc1101Session::wait_idle(std::string& error) {
    for (int i = 0; i < 200; ++i) {
        uint8_t marc = 0;
        if (!read_status(MARCSTATE, marc, error)) return false;
        if ((marc & 0x1F) == 0x01) return true;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    error = "CC1101 idle timeout";
    return false;
}

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
    rssi_cal_db_ = load_rf_settings().rssi_cal_db;
    if (!soft_reset(error)) return false;
    return apply_preset(Cc1101Preset::SpectrumWide, error);
}

void Cc1101Session::close() {
    if (impl_) {
        std::string e;
        (void)enter_idle(e);
        delete impl_;
        impl_ = nullptr;
    }
    rx_active_ = false;
}

bool Cc1101Session::ok() const {
    return impl_ && impl_->spi && impl_->spi->ok();
}

bool Cc1101Session::soft_reset(std::string& error) {
    if (!strobe(SRES, error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    rx_active_ = false;
    return true;
}

bool Cc1101Session::apply_preset(Cc1101Preset preset, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    (void)strobe(SIDLE, error);
    wait_idle(error);
    error.clear();

    auto w = [&](uint8_t r, uint8_t v) { return write_reg(r, v, error); };

    // Shared base. IOCFG2 is rewritten per frequency for Cap RF_SW1 (GDO2).
    if (!w(IOCFG2, cc1101_iocfg2_for_sw1(true)) || !w(IOCFG1, 0x2E)) return false;
    if (!w(FIFOTHR, 0x47) || !w(PKTCTRL1, 0x04) || !w(PKTCTRL0, 0x00)) return false;
    if (!w(ADDR, 0x00) || !w(CHANNR, 0x00)) return false;
    if (!w(FSCTRL1, 0x06) || !w(FSCTRL0, 0x00)) return false;
    if (!w(MCSM0, 0x18) || !w(FOCCFG, 0x16) || !w(BSCFG, 0x6C)) return false;
    if (!w(AGCCTRL2, 0x43) || !w(AGCCTRL1, 0x40) || !w(AGCCTRL0, 0x91)) return false;
    if (!w(FREND1, 0x56) || !w(FSCAL3, 0xE9) || !w(FSCAL2, 0x2A) || !w(FSCAL1, 0x00) ||
        !w(FSCAL0, 0x1F))
        return false;
    if (!w(TEST2, 0x81) || !w(TEST1, 0x35) || !w(TEST0, 0x09)) return false;

    switch (preset) {
        case Cc1101Preset::SpectrumWide:
            // ASK/OOK, no sync, asynchronous serial, wide BW ~650 kHz, GDO0 = serial data out.
            if (!w(IOCFG0, 0x0D)) return false;
            if (!w(MDMCFG4, mdmcfg4_for_bw(650.f)) || !w(MDMCFG3, 0x83)) return false;
            if (!w(MDMCFG2, 0x30) || !w(MDMCFG1, 0x22) || !w(MDMCFG0, 0xF8)) return false;
            if (!w(DEVIATN, 0x00) || !w(FREND0, 0x11) || !w(MCSM1, 0x0C)) return false;
            rx_bw_khz_ = 650.f;
            break;
        case Cc1101Preset::OokAsync:
            if (!w(IOCFG0, 0x0D)) return false;
            if (!w(MDMCFG4, mdmcfg4_for_bw(270.f)) || !w(MDMCFG3, 0x93)) return false;
            if (!w(MDMCFG2, 0x30) || !w(MDMCFG1, 0x22) || !w(MDMCFG0, 0xF8)) return false;
            if (!w(DEVIATN, 0x00) || !w(FREND0, 0x11) || !w(MCSM1, 0x0C)) return false;
            if (!w(PKTCTRL0, 0x32)) return false;
            rx_bw_khz_ = 270.f;
            break;
        case Cc1101Preset::Fsk2:
            if (!w(IOCFG0, 0x06)) return false;
            if (!w(MDMCFG4, mdmcfg4_for_bw(203.f)) || !w(MDMCFG3, 0x83)) return false;
            if (!w(MDMCFG2, 0x03) || !w(MDMCFG1, 0x22) || !w(MDMCFG0, 0xF8)) return false;
            if (!w(DEVIATN, 0x40) || !w(FREND0, 0x10) || !w(MCSM1, 0x3C)) return false;
            if (!w(PKTCTRL0, 0x05)) return false;
            rx_bw_khz_ = 203.f;
            break;
    }

    if (!set_pa_level(pa_, error)) return false;
    if (!apply_rf_sw1(error)) return false;
    preset_ = preset;
    rx_active_ = false;
    return true;
}

bool Cc1101Session::apply_rf_sw1(std::string& error) {
    bool sw0 = false, sw1 = true;
    cc1101_rf_switch_levels(freq_hz_, sw0, sw1);
    (void)sw0;
    return write_reg(IOCFG2, cc1101_iocfg2_for_sw1(sw1), error);
}

bool Cc1101Session::set_pa_level(Cc1101PaLevel level, std::string& error) {
    pa_ = level;
    const int band = cc1101_band_index(freq_hz_);
    const uint8_t v = pa_byte(level, band < 0 ? 1 : band);
    return write_burst(PATABLE, &v, 1, error);
}

bool Cc1101Session::set_rx_bw_khz(float khz, std::string& error) {
    rx_bw_khz_ = khz;
    return write_reg(MDMCFG4, mdmcfg4_for_bw(khz), error);
}

bool Cc1101Session::set_frequency_hz(uint32_t hz, std::string& error, bool enter_rx_flag) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    if (!cc1101_freq_supported(hz)) {
        error = "frequency outside CC1101 bands";
        return false;
    }
    (void)strobe(SIDLE, error);
    wait_idle(error);
    error.clear();

    if (!set_cc1101_rf_path(hz, error)) {
        // Soft-fail path switch on desktop/missing gpio; continue if SPI still works.
        error.clear();
    }

    freq_hz_ = hz;
    if (!apply_rf_sw1(error)) {
        error.clear();
    }
    if (!set_pa_level(pa_, error)) {
        error.clear();
    }

    const uint32_t freq = static_cast<uint32_t>(std::lround((static_cast<double>(hz) * 65536.0) / kFxosc));
    if (!write_reg(FREQ2, static_cast<uint8_t>((freq >> 16) & 0xFF), error)) return false;
    if (!write_reg(FREQ1, static_cast<uint8_t>((freq >> 8) & 0xFF), error)) return false;
    if (!write_reg(FREQ0, static_cast<uint8_t>(freq & 0xFF), error)) return false;

    if (!strobe(SCAL, error)) return false;
    wait_idle(error);
    error.clear();

    rx_active_ = false;
    if (enter_rx_flag) return enter_rx(error);
    return true;
}

bool Cc1101Session::enter_rx(std::string& error) {
    if (!strobe(SRX, error)) return false;
    rx_active_ = true;
    std::this_thread::sleep_for(std::chrono::microseconds(dwell_us_for_bw(rx_bw_khz_)));
    return true;
}

bool Cc1101Session::enter_idle(std::string& error) {
    const bool ok_strobe = strobe(SIDLE, error);
    rx_active_ = false;
    return ok_strobe;
}

bool Cc1101Session::read_rssi_dbm(int& dbm, std::string& error) {
    if (!ok()) {
        error = "CC1101 not open";
        return false;
    }
    if (!rx_active_) {
        if (!enter_rx(error)) return false;
    }
    const int band = cc1101_band_index(freq_hz_);
    const int offset = rssi_offset_for_band(band < 0 ? 1 : band);
    int sum = 0;
    for (int i = 0; i < 2; ++i) {
        uint8_t raw = 0;
        if (!read_status(RSSI, raw, error)) return false;
        const int8_t s = static_cast<int8_t>(raw);
        sum += static_cast<int>(s) / 2 - offset + rssi_cal_db_;
        if (i == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    dbm = sum / 2;
    return true;
}

bool Cc1101Session::read_chip_id(uint8_t& partnum, uint8_t& version, std::string& error) {
    if (!read_status(PARTNUM, partnum, error)) return false;
    return read_status(VERSION, version, error);
}

bool Cc1101Session::spectrum_sweep(const std::vector<uint32_t>& freqs, std::vector<int>& rssi_out,
                                   std::string& error, std::atomic<bool>* cancel) {
    rssi_out.clear();
    rssi_out.reserve(freqs.size());
    if (freqs.empty()) return true;
    if (!apply_preset(Cc1101Preset::SpectrumWide, error)) return false;

    for (uint32_t f : freqs) {
        if (cancel && cancel->load()) break;
        // set_frequency_hz updates RF_SW0 + IOCFG2 (RF_SW1) per hop.
        if (!set_frequency_hz(f, error, true)) {
            rssi_out.push_back(-120);
            error.clear();
            continue;
        }
        int dbm = -120;
        if (!read_rssi_dbm(dbm, error)) {
            rssi_out.push_back(-120);
            error.clear();
            continue;
        }
        rssi_out.push_back(dbm);
    }
    (void)enter_idle(error);
    error.clear();
    return true;
}

bool Cc1101Session::spectrum_scan(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz,
                                  std::vector<std::string>& lines, std::string& error,
                                  std::atomic<bool>* cancel) {
    lines.clear();
    std::vector<uint32_t> freqs;
    if (!cc1101_plan_sweep(start_hz, stop_hz, step_hz, 100, freqs, error)) return false;
    std::vector<int> rssi;
    if (!spectrum_sweep(freqs, rssi, error, cancel)) return false;
    for (size_t i = 0; i < freqs.size() && i < rssi.size(); ++i) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%u,%d", freqs[i], rssi[i]);
        lines.push_back(buf);
    }
    return true;
}

bool Cc1101Session::listen_rssi(int samples, int delay_ms, std::vector<int>& out, std::string& error,
                                std::atomic<bool>* cancel) {
    out.clear();
    if (samples <= 0) samples = 40;
    if (delay_ms < 0) delay_ms = 0;
    if (!enter_rx(error)) return false;
    for (int i = 0; i < samples; ++i) {
        if (cancel && cancel->load()) break;
        int dbm = -120;
        if (!read_rssi_dbm(dbm, error)) return false;
        out.push_back(dbm);
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return true;
}

bool Cc1101Session::stop_tx(std::string& error) {
    (void)strobe(SIDLE, error);
    (void)strobe(SFTX, error);
    rx_active_ = false;
    return true;
}

bool Cc1101Session::tx_carrier(uint32_t hz, Cc1101PaLevel level, int duration_ms, std::string& error,
                               std::atomic<bool>* cancel) {
    if (duration_ms < 1) duration_ms = 1;
    if (duration_ms > 2000) duration_ms = 2000;
    if (!apply_preset(Cc1101Preset::OokAsync, error)) return false;
    if (!set_pa_level(level, error)) return false;
    if (!set_frequency_hz(hz, error, false)) return false;
    // Async serial TX: GDO0 is data in; hold high for continuous carrier.
    if (!write_reg(IOCFG0, 0x2E, error)) return false;
    if (!write_reg(PKTCTRL0, 0x32, error)) return false;
    if (!apply_rf_sw1(error)) return false;
    if (!request_cc1101_gdo0_output(true, error)) return false;
    if (!strobe(STX, error)) {
        release_cc1101_gdo0();
        return false;
    }

    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cancel && cancel->load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool ok_stop = stop_tx(error);
    release_cc1101_gdo0();
    return ok_stop;
}

bool Cc1101Session::tx_raw_timings(uint32_t hz, Cc1101PaLevel level, const std::vector<int>& timings_us,
                                   int repeats, int delay_ms, std::string& error, std::atomic<bool>* cancel) {
    if (timings_us.empty()) {
        error = "empty timings";
        return false;
    }
    if (repeats < 1) repeats = 1;
    if (delay_ms < 0) delay_ms = 0;
    if (!apply_preset(Cc1101Preset::OokAsync, error)) return false;
    if (!set_pa_level(level, error)) return false;
    if (!set_frequency_hz(hz, error, false)) return false;
    // Async serial OOK: STX once, bit-bang GDO0 with absolute deadlines (no SPI per edge).
    if (!write_reg(IOCFG0, 0x2E, error)) return false;
    if (!write_reg(PKTCTRL0, 0x32, error)) return false;
    if (!apply_rf_sw1(error)) return false;
    if (!request_cc1101_gdo0_output(false, error)) return false;

    auto cleanup = [&]() {
        (void)stop_tx(error);
        release_cc1101_gdo0();
    };

    if (!strobe(STX, error)) {
        cleanup();
        return false;
    }

    int64_t max_jitter_ns = 0;
    for (int r = 0; r < repeats; ++r) {
        if (cancel && cancel->load()) break;

        timespec start{};
        ::clock_gettime(CLOCK_MONOTONIC, &start);
        // Warm-up carrier ~1.5 ms before first element.
        timespec deadline = add_us(start, 1500);
        if (!set_cc1101_gdo0(true, error)) {
            cleanup();
            return false;
        }
        sleep_until_abs(deadline);

        for (int t : timings_us) {
            if (cancel && cancel->load()) break;
            const int us = std::abs(t);
            if (us <= 0) continue;
            const bool mark = t > 0;
            if (!set_cc1101_gdo0(mark, error)) {
                cleanup();
                return false;
            }
            deadline = add_us(deadline, us);
            timespec now{};
            ::clock_gettime(CLOCK_MONOTONIC, &now);
            const int64_t late_ns =
                (now.tv_sec - deadline.tv_sec) * 1000000000LL + (now.tv_nsec - deadline.tv_nsec);
            if (late_ns > max_jitter_ns) max_jitter_ns = late_ns;
            sleep_until_abs(deadline);
        }
        (void)set_cc1101_gdo0(false, error);
        if (delay_ms > 0 && r + 1 < repeats) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    if (max_jitter_ns > 0) {
        std::fprintf(stderr, "cc1101 tx_raw_timings max_jitter_us=%lld\n",
                     static_cast<long long>(max_jitter_ns / 1000));
    }
    cleanup();
    error.clear();
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
    if (!apply_preset(Cc1101Preset::Fsk2, error)) return false;
    if (!set_frequency_hz(freq_hz_, error, false)) return false;
    uint8_t flush[1] = {SFTX};
    uint8_t rx1[1] = {0};
    if (!impl_->spi->transfer(flush, rx1, 1, error)) return false;
    std::vector<uint8_t> tx(2 + len);
    tx[0] = 0x7F;
    tx[1] = static_cast<uint8_t>(len);
    std::memcpy(tx.data() + 2, payload, len);
    std::vector<uint8_t> rx(tx.size(), 0);
    if (!impl_->spi->transfer(tx.data(), rx.data(), tx.size(), error)) return false;
    if (!strobe(STX, error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return stop_tx(error);
}

bool Cc1101Session::read_gdo0(bool& high, std::string& error) {
    return read_cc1101_gdo0(high, error);
}

bool write_sub_raw(const std::string& path, uint32_t freq_hz, const std::vector<int>& timings_us,
                   std::string& error, const std::string& preset_name, const std::string& comment_block) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: Flipper SubGhz RAW File\n";
    out << "Version: 1\n";
    out << "Frequency: " << freq_hz << "\n";
    out << "Preset: " << preset_name << "\n";
    out << "Protocol: RAW\n";
    if (!comment_block.empty()) out << comment_block;
    out << "RAW_Data:";
    int col = 0;
    for (int v : timings_us) {
        if (col == 0) out << "\n";
        else out << " ";
        out << v;
        if (++col >= 512) col = 0;
    }
    out << "\n";
    return true;
}

bool write_sub_raw_from_rssi(const std::string& path, uint32_t freq_hz, const std::vector<int>& rssi_samples,
                             std::string& error) {
    std::vector<int> timings;
    timings.reserve(rssi_samples.size() * 2);
    for (int v : rssi_samples) {
        timings.push_back((v > -70) ? 400 : 100);
        timings.push_back(-200);
    }
    return write_sub_raw(path, freq_hz, timings, error, "FuriHalSubGhzPresetOok650Async",
                         "# Note: archival RSSI-derived timings\n");
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
    std::sort(names.begin(), names.end());
    return true;
}

bool parse_sub_file(const std::string& path, SubFileInfo& out, std::string& error) {
    out = {};
    out.path = path;
    std::ifstream in(path);
    if (!in) {
        error = "open failed";
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (!out.comment.empty()) out.comment.push_back('\n');
            out.comment += line;
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
        if (key == "Frequency") out.freq_hz = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "Preset") out.preset = val;
        else if (key == "Protocol") out.protocol = val;
        else if (key == "Bit") out.bit_count = std::atoi(val.c_str());
        else if (key == "TE") out.te_us = std::atoi(val.c_str());
        else if (key == "Key") out.key_hex = val;
        else if (key == "RAW_Data") {
            std::istringstream iss(val);
            int v = 0;
            while (iss >> v) out.timings_us.push_back(v);
            // Continuation lines until next key-like line.
            std::streampos pos;
            while (true) {
                pos = in.tellg();
                if (!std::getline(in, line)) break;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line.find(':') != std::string::npos && line[0] != '-' && line[0] != ' ' &&
                    !(line[0] >= '0' && line[0] <= '9') && line[0] != '+') {
                    in.seekg(pos);
                    break;
                }
                std::istringstream cont(line);
                while (cont >> v) out.timings_us.push_back(v);
            }
        }
    }
    if (out.freq_hz == 0 && out.timings_us.empty() && out.protocol.empty()) {
        error = "empty sub file";
        return false;
    }
    return true;
}

} // namespace platform

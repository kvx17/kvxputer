#include "nrf24_radio.h"

#include "cap_detect.h"
#include "spi_device.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr uint8_t R_REGISTER = 0x00;
constexpr uint8_t W_REGISTER = 0x20;
constexpr uint8_t R_RX_PAYLOAD = 0x61;
constexpr uint8_t W_TX_PAYLOAD = 0xA0;
constexpr uint8_t FLUSH_RX = 0xE2;
constexpr uint8_t FLUSH_TX = 0xE1;
constexpr uint8_t NOP = 0xFF;

} // namespace

struct Nrf24Session::Impl {
    std::unique_ptr<SpiDevice> spi;

    bool read_reg(uint8_t reg, uint8_t& val, std::string& error) {
        uint8_t tx[2] = {static_cast<uint8_t>(R_REGISTER | (reg & 0x1F)), 0xFF};
        uint8_t rx[2] = {0, 0};
        if (!spi->transfer(tx, rx, 2, error)) return false;
        val = rx[1];
        return true;
    }
    bool write_reg(uint8_t reg, uint8_t val, std::string& error) {
        uint8_t tx[2] = {static_cast<uint8_t>(W_REGISTER | (reg & 0x1F)), val};
        uint8_t rx[2] = {0, 0};
        return spi->transfer(tx, rx, 2, error);
    }
    bool cmd(uint8_t c, std::string& error) {
        uint8_t tx[1] = {c};
        uint8_t rx[1] = {0};
        return spi->transfer(tx, rx, 1, error);
    }
};

bool Nrf24Session::open(const std::string& spidev, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    const auto info = detect_nrf24(spidev);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    SpiConfig cfg;
    cfg.path = info.path;
    auto spi = SpiDevice::open(cfg, error);
    if (!spi) return false;
    impl_ = new Impl();
    impl_->spi = std::move(spi);
    // Power up, PRX, 2-byte CRC
    if (!impl_->write_reg(0x00, 0x0F, error)) return false; // CONFIG
    if (!impl_->write_reg(0x01, 0x00, error)) return false; // EN_AA off for promiscuous-ish
    if (!impl_->write_reg(0x02, 0x01, error)) return false; // EN_RXADDR pipe0
    if (!impl_->write_reg(0x03, 0x03, error)) return false; // SETUP_AW 5 bytes
    if (!impl_->write_reg(0x05, 0x02, error)) return false; // RF_CH 2
    if (!impl_->write_reg(0x06, 0x07, error)) return false; // RF_SETUP 1Mbps 0dBm
    impl_->cmd(FLUSH_RX, error);
    impl_->cmd(FLUSH_TX, error);
    return true;
}

void Nrf24Session::close() {
    if (impl_) {
        delete impl_;
        impl_ = nullptr;
    }
}

bool Nrf24Session::ok() const {
    return impl_ && impl_->spi && impl_->spi->ok();
}

bool Nrf24Session::spectrum_rpd(int samples, std::vector<std::string>& lines, std::string& error, bool* cancel) {
    lines.clear();
    if (!ok()) {
        error = "nRF24 not open";
        return false;
    }
    if (samples <= 0) samples = 16;
    for (int ch = 0; ch < samples && ch < 80; ++ch) {
        if (cancel && *cancel) break;
        if (!impl_->write_reg(0x05, static_cast<uint8_t>(ch), error)) return false;
        // PRX on
        uint8_t cfg = 0;
        impl_->read_reg(0x00, cfg, error);
        impl_->write_reg(0x00, static_cast<uint8_t>(cfg | 0x01 | 0x02), error);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        uint8_t rpd = 0;
        impl_->read_reg(0x09, rpd, error); // RPD
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ch %d RPD=%u", ch, rpd & 1u);
        lines.push_back(buf);
    }
    return true;
}

bool Nrf24Session::listen_payloads(int seconds, std::vector<std::string>& lines, std::string& error,
                                   bool* cancel) {
    lines.clear();
    if (!ok()) {
        error = "nRF24 not open";
        return false;
    }
    if (seconds <= 0) seconds = 5;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) break;
        uint8_t status = 0;
        uint8_t tx[1] = {NOP};
        uint8_t rx[1] = {0};
        if (!impl_->spi->transfer(tx, rx, 1, error)) return false;
        status = rx[0];
        if (status & 0x40) { // RX_DR
            uint8_t ptx[33] = {R_RX_PAYLOAD};
            uint8_t prx[33] = {0};
            if (!impl_->spi->transfer(ptx, prx, 33, error)) return false;
            char hex[96];
            size_t n = 0;
            for (int i = 1; i < 33 && n + 3 < sizeof(hex); ++i) {
                n += static_cast<size_t>(std::snprintf(hex + n, sizeof(hex) - n, "%02X", prx[i]));
            }
            lines.push_back(std::string(hex));
            impl_->write_reg(0x07, 0x40, error); // clear RX_DR
            if (lines.size() >= 40) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (lines.empty()) lines.push_back("(no payloads — CE may need GPIO)");
    return true;
}

bool Nrf24Session::send_payload(const uint8_t* data, size_t len, std::string& error) {
    if (!ok()) {
        error = "nRF24 not open";
        return false;
    }
    if (!data || len == 0 || len > 32) {
        error = "bad payload";
        return false;
    }
    // Without CE GPIO pulse, TX stays in FIFO. Still accept write for lab wiring checks.
    impl_->cmd(FLUSH_TX, error);
    std::vector<uint8_t> tx(1 + len);
    tx[0] = W_TX_PAYLOAD;
    std::memcpy(tx.data() + 1, data, len);
    std::vector<uint8_t> rx(tx.size(), 0);
    if (!impl_->spi->transfer(tx.data(), rx.data(), tx.size(), error)) return false;
    // PTX mode
    uint8_t cfg = 0;
    impl_->read_reg(0x00, cfg, error);
    impl_->write_reg(0x00, static_cast<uint8_t>((cfg | 0x02) & ~0x01), error);
    error.clear();
    // Note for UI: CE pulse required on Cap GPIO for air TX.
    return true;
}

} // namespace platform

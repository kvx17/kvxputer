#include "sx1262.h"

#include "cap_detect.h"
#include "cap_power.h"
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

} // namespace

struct Sx1262Session::Impl {
    std::unique_ptr<SpiDevice> spi;

    bool cmd(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error) {
        (void)wait_sx1262_not_busy(200, error);
        error.clear();
        rx.assign(tx.size(), 0);
        if (!spi->transfer(tx.data(), rx.data(), tx.size(), error)) return false;
        (void)wait_sx1262_not_busy(200, error);
        error.clear();
        return true;
    }
};

bool Sx1262Session::open(const std::string& spidev, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    if (!enable_cap_power(CapKindHint::Sx1262, error)) return false;
    const auto info = detect_sx1262(spidev);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    SpiConfig cfg;
    cfg.path = info.path;
    cfg.speed_hz = 1000000;
    auto spi = SpiDevice::open(cfg, error);
    if (!spi) return false;
    impl_ = new Impl();
    impl_->spi = std::move(spi);
    return true;
}

void Sx1262Session::close() {
    if (impl_) {
        delete impl_;
        impl_ = nullptr;
    }
}

bool Sx1262Session::ok() const {
    return impl_ && impl_->spi && impl_->spi->ok();
}

bool Sx1262Session::read_rssi(int& dbm, std::string& error) {
    if (!ok()) {
        error = "SX1262 not open";
        return false;
    }
    // GetRssiInst 0x15 — returns rssi_inst
    std::vector<uint8_t> tx = {0x15, 0x00, 0x00};
    std::vector<uint8_t> rx;
    if (!impl_->cmd(tx, rx, error)) return false;
    const uint8_t raw = rx.size() > 2 ? rx[2] : rx.back();
    dbm = -static_cast<int>(raw) / 2;
    return true;
}

bool Sx1262Session::antenna_check(std::vector<std::string>& lines, std::string& error) {
    lines.clear();
    if (!ok()) {
        error = "SX1262 not open";
        return false;
    }
    std::vector<uint8_t> tx = {0xC0, 0x00};
    std::vector<uint8_t> rx;
    if (!impl_->cmd(tx, rx, error)) return false;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "GetStatus=0x%02X", rx.empty() ? 0 : rx[0]);
    lines.push_back(buf);
    int dbm = -120;
    if (read_rssi(dbm, error)) {
        std::snprintf(buf, sizeof(buf), "RSSI %d dBm", dbm);
        lines.push_back(buf);
    } else {
        lines.push_back("RSSI unavailable (BUSY/DIO wiring?)");
        error.clear();
    }
    lines.push_back("Cap-LoRa-1262 SPI OK");
    return true;
}

bool Sx1262Session::send_text(const std::string& text, std::string& error) {
    if (!ok()) {
        error = "SX1262 not open";
        return false;
    }
    if (text.empty() || text.size() > 200) {
        error = "bad message";
        return false;
    }
    // WriteBuffer 0x0E offset 0 + payload; SetTx 0x83 — may need BUSY wait on Cap.
    std::vector<uint8_t> wr;
    wr.push_back(0x0E);
    wr.push_back(0x00);
    wr.insert(wr.end(), text.begin(), text.end());
    std::vector<uint8_t> rx;
    if (!impl_->cmd(wr, rx, error)) return false;
    std::vector<uint8_t> txcmd = {0x83, 0x00, 0x00, 0x00}; // SetTx timeout
    if (!impl_->cmd(txcmd, rx, error)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

bool Sx1262Session::receive_text(int timeout_ms, std::string& out, std::string& error, bool* cancel) {
    out.clear();
    if (!ok()) {
        error = "SX1262 not open";
        return false;
    }
    std::vector<uint8_t> setrx = {0x82, 0x00, 0x00, 0x00}; // SetRx continuous-ish
    std::vector<uint8_t> rx;
    if (!impl_->cmd(setrx, rx, error)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // GetIrqStatus 0x12
        std::vector<uint8_t> irq = {0x12, 0x00, 0x00, 0x00};
        if (!impl_->cmd(irq, rx, error)) return false;
        const uint16_t st = rx.size() >= 4 ? static_cast<uint16_t>((rx[2] << 8) | rx[3]) : 0;
        if (st & 0x0002) { // RxDone
            // GetRxBufferStatus = 0x13; ReadBuffer = 0x1E
            std::vector<uint8_t> gstat = {0x13, 0x00, 0x00, 0x00};
            if (!impl_->cmd(gstat, rx, error)) return false;
            const uint8_t plen = rx.size() > 2 ? rx[2] : 0;
            const uint8_t poff = rx.size() > 3 ? rx[3] : 0;
            if (plen == 0 || plen > 200) {
                error = "empty RX";
                return false;
            }
            std::vector<uint8_t> rbuf(3 + plen, 0);
            rbuf[0] = 0x1E;
            rbuf[1] = poff;
            if (!impl_->cmd(rbuf, rx, error)) return false;
            out.assign(reinterpret_cast<char*>(rx.data() + 3), reinterpret_cast<char*>(rx.data() + 3 + plen));
            return true;
        }
    }
    error = "no packet";
    return false;
}

} // namespace platform

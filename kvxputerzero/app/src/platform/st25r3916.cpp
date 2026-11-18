#include "st25r3916.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "gpio_line.h"
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

// Cap-CC1101 NFC_CS on Zero EXT (CS2 / GPIO22). Soft-CS; use SPI_NO_CS so CE1
// does not also select the CC1101 on the shared bus.
constexpr unsigned kNfcCsGpio = 22;
constexpr uint8_t kRegOpControl = 0x02;
constexpr uint8_t kRegMode = 0x03;
constexpr uint8_t kRegIcIdentity = 0x3F;
constexpr uint8_t kCmdRead = 0x40; // RFAL: (addr & 0x3F) | 0x40
constexpr uint8_t kCmdSetDefault = 0xC1;
constexpr uint8_t kFifoLoad = 0x80;
constexpr uint8_t kFifoRead = 0x9F;
constexpr uint8_t kCmdTransmit = 0xC5; // Transmit without CRC (REQA is 7-bit; best-effort)
constexpr uint8_t kRegFifoStatus1 = 0x1E;
constexpr uint8_t kRegNumTxBytes1 = 0x22;
constexpr uint8_t kRegNumTxBytes2 = 0x23;

bool ic_identity_ok(uint8_t id) {
    // ST25R3916 type=5, ST25R3916B type=6 (bits [7:3]); rev in [2:0]. Typical 0x2A.
    const uint8_t type = (id >> 3) & 0x1F;
    return type == 5 || type == 6;
}

} // namespace

struct St25Session::Impl {
    std::unique_ptr<SpiDevice> spi;
    GpioLine cs{"/dev/gpiochip0", kNfcCsGpio};

    void cs_low(std::string& error) { (void)cs.set_value(false, error); }
    void cs_high(std::string& error) { (void)cs.set_value(true, error); }

    bool xfer(const uint8_t* tx, uint8_t* rx, size_t len, std::string& error) {
        cs_low(error);
        // Small settle so ST25 sees CS before first clock (mode 1).
        std::this_thread::sleep_for(std::chrono::microseconds(5));
        const bool ok = spi->transfer(tx, rx, len, error);
        cs_high(error);
        return ok;
    }

    bool write_reg(uint8_t addr, uint8_t val, std::string& error) {
        uint8_t tx[2] = {static_cast<uint8_t>(addr & 0x3F), val};
        uint8_t rx[2] = {};
        return xfer(tx, rx, 2, error);
    }

    bool read_reg(uint8_t addr, uint8_t& val, std::string& error) {
        uint8_t tx[2] = {static_cast<uint8_t>((addr & 0x3F) | kCmdRead), 0x00};
        uint8_t rx[2] = {};
        if (!xfer(tx, rx, 2, error)) return false;
        val = rx[1];
        return true;
    }

    bool direct_cmd(uint8_t cmd_byte, std::string& error) {
        uint8_t tx[1] = {cmd_byte};
        uint8_t rx[1] = {};
        return xfer(tx, rx, 1, error);
    }
};

CapInfo detect_st25r3916(const std::string& spidev) {
    CapInfo miss;
    miss.kind = CapKind::St25r3916;
    miss.present = false;
    miss.name = "ST25R3916";
    if (kDesktop) {
        miss.error = "Cap not present";
        return miss;
    }
    // Free GPIO22 if a prior SX1262 probe claimed BUSY.
    release_cap_power(true);

    std::string err;
    if (!enable_cap_power(CapKindHint::Cc1101, err)) {
        miss.error = err.empty() ? "Cap not present" : err;
        return miss;
    }
    St25Session s;
    if (!s.open(spidev, err)) {
        miss.error = err.empty() ? "Cap not present" : err;
        return miss;
    }
    std::string id;
    if (!s.identity(id, err)) {
        miss.error = err.empty() ? "Cap not present" : err;
        s.close();
        return miss;
    }
    CapInfo ok;
    ok.kind = CapKind::St25r3916;
    ok.present = true;
    ok.name = "ST25R3916";
    ok.path = spidev.empty() ? default_cap_spidev_path() : spidev;
    ok.detail = id + " CS=GPIO22 mode1";
    s.close();
    return ok;
}

bool St25Session::open(const std::string& spidev, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    if (!enable_cap_power(CapKindHint::Cc1101, error)) return false;

    impl_ = new Impl();
    if (!impl_->cs.request_output(true, error)) {
        close();
        return false;
    }
    SpiConfig cfg;
    cfg.path = spidev.empty() ? default_cap_spidev_path() : spidev;
    cfg.speed_hz = 1000000;
    cfg.mode = 1; // Cap ST25: SPI mode 1 (CPOL=0, CPHA=1)
    cfg.no_chip_select = true;
    auto spi = SpiDevice::open(cfg, error);
    if (!spi) {
        close();
        return false;
    }
    impl_->spi = std::move(spi);

    (void)impl_->direct_cmd(kCmdSetDefault, error);
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // Enable oscillator (en bit). Full RFAL bring-up is out of scope for Cap Probe.
    (void)impl_->write_reg(kRegOpControl, 0x80, error);
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return true;
}

void St25Session::close() {
    if (!impl_) return;
    std::string e;
    if (impl_->spi && impl_->cs.ok()) {
        (void)impl_->write_reg(kRegOpControl, 0x00, e);
        impl_->cs_high(e);
    }
    impl_->cs.release();
    delete impl_;
    impl_ = nullptr;
}

bool St25Session::ok() const {
    return impl_ && impl_->spi && impl_->spi->ok() && impl_->cs.ok();
}

bool St25Session::identity(std::string& line, std::string& error) {
    line.clear();
    if (!ok()) {
        error = "ST25 not open";
        return false;
    }
    uint8_t id = 0;
    if (!impl_->read_reg(kRegIcIdentity, id, error)) return false;
    if (!ic_identity_ok(id)) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "bad IC_IDENTITY=0x%02X", id);
        error = buf;
        return false;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "ic_identity=0x%02X type=%u", id, (id >> 3) & 0x1Fu);
    line = buf;
    return true;
}

bool St25Session::read_uid(std::string& uid_hex, std::string& error, bool* cancel) {
    uid_hex.clear();
    if (!ok()) {
        error = "ST25 not open";
        return false;
    }
    // Best-effort field-on + REQA. Full anticollision stays with Cap-CC1101-NFC / RFAL.
    (void)impl_->write_reg(kRegMode, 0x08, error); // om ≈ ISO14443A
    error.clear();
    (void)impl_->write_reg(kRegOpControl, 0xC8, error); // en + tx/rx path bits
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    (void)impl_->write_reg(kRegNumTxBytes1, 0x00, error);
    (void)impl_->write_reg(kRegNumTxBytes2, 0x07, error); // 7-bit REQA
    error.clear();
    {
        uint8_t tx[2] = {kFifoLoad, 0x26};
        uint8_t rx[2] = {};
        if (!impl_->xfer(tx, rx, 2, error)) return false;
    }
    if (!impl_->direct_cmd(kCmdTransmit, error)) return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            return false;
        }
        uint8_t st1 = 0;
        if (!impl_->read_reg(kRegFifoStatus1, st1, error)) return false;
        const uint8_t nbytes = st1 & 0x1F;
        if (nbytes >= 2) {
            std::vector<uint8_t> rtx(1 + nbytes, 0);
            rtx[0] = kFifoRead;
            std::vector<uint8_t> rrx(rtx.size(), 0);
            if (!impl_->xfer(rtx.data(), rrx.data(), rtx.size(), error)) return false;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "ATQA %02X%02X", rrx.size() > 1 ? rrx[1] : 0,
                          rrx.size() > 2 ? rrx[2] : 0);
            uid_hex = buf;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::string id;
    if (identity(id, error)) {
        uid_hex = "no card in field; " + id;
        error.clear();
        return true;
    }
    error = "no NFC response";
    return false;
}

} // namespace platform

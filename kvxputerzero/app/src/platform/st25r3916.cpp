#include "st25r3916.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "gpio_line.h"
#include "nfc_ndef.h"
#include "spi_device.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
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

// Cap-CC1101 Cap-Bus NFC_CS on Cardputer Zero EXT pin 5 = BCM GPIO22.
// (Adv docs list NFC_CS=G6 / NFC_IRQ=G4 — those ESP32 pins are Adv-only.)
// Cap Cap-Bus NFC_IRQ mates Zero HAT_P1 = BCM GPIO23; we still poll IRQ regs.
constexpr unsigned kNfcCsGpio = 22;
constexpr unsigned kNfcIrqGpio = 23;
constexpr uint8_t kRegOpControl = 0x02;
constexpr uint8_t kRegMode = 0x03;
constexpr uint8_t kRegBitrate = 0x04;
constexpr uint8_t kRegIso14443a = 0x05;
constexpr uint8_t kRegPassiveTarget = 0x08;
constexpr uint8_t kRegAux = 0x0A;
constexpr uint8_t kRegRxConf1 = 0x0B;
constexpr uint8_t kRegRxConf2 = 0x0C;
constexpr uint8_t kRegRxConf3 = 0x0D;
constexpr uint8_t kRegRxConf4 = 0x0E;
constexpr uint8_t kRegMaskRxTimer = 0x0F;
constexpr uint8_t kRegNoRespTimer1 = 0x10;
constexpr uint8_t kRegNoRespTimer2 = 0x11;
constexpr uint8_t kRegTimerEmv = 0x12;
constexpr uint8_t kRegPtaDisplay = 0x21;
constexpr uint8_t kRegAntTuneA = 0x26;
constexpr uint8_t kRegAntTuneB = 0x27;
constexpr uint8_t kRegTxDriver = 0x28;
constexpr uint8_t kRegIoConf1 = 0x00;
constexpr uint8_t kRegIoConf2 = 0x01;
constexpr uint8_t kRegPtMod = 0x29;
constexpr uint8_t kRegFieldThrActv = 0x2A;
constexpr uint8_t kRegFieldThrDeactv = 0x2B;
constexpr uint8_t kRegAuxDisplay = 0x31;
constexpr uint8_t kRegWakeupTimer = 0x32;
constexpr uint8_t kRegIrqMaskMain = 0x16;
constexpr uint8_t kRegIrqMain = 0x1A;
constexpr uint8_t kRegIcIdentity = 0x3F;
// Pi SPI0 CE1 — Cap CC1101 CS. Hold high while soft-CS'ing ST25 on GPIO22.
constexpr unsigned kSpiCe1Gpio = 7;
constexpr uint8_t kCmdRead = 0x40;
constexpr uint8_t kCmdSetDefault = 0xC1;
constexpr uint8_t kCmdStop = 0xC2;
constexpr uint8_t kCmdTransmit = 0xC4; // with CRC
constexpr uint8_t kCmdTransmitNoCrc = 0xC5;
constexpr uint8_t kCmdTransmitReqa = 0xC6;
constexpr uint8_t kCmdTransmitWupa = 0xC7;
constexpr uint8_t kCmdNfcInitialFieldOn = 0xC8;
constexpr uint8_t kCmdGotoSense = 0xCD;
constexpr uint8_t kCmdGotoSleep = 0xCE;
constexpr uint8_t kCmdUnmaskRx = 0xD1;
constexpr uint8_t kCmdResetRxGain = 0xD5;
constexpr uint8_t kCmdAdjustRegulators = 0xD6;
constexpr uint8_t kCmdClearFifo = 0xDB;
constexpr uint8_t kCmdSpaceB = 0xFB;
constexpr uint8_t kCmdTestAccess = 0xFC;
// Space-B registers (M5 configure_nfc_a / emulation setup).
constexpr uint8_t kRegBCorrConf1 = 0x0C;
constexpr uint8_t kRegBCorrConf2 = 0x0D;
constexpr uint8_t kRegBOvershoot1 = 0x30;
constexpr uint8_t kRegBOvershoot2 = 0x31;
constexpr uint8_t kRegBUndershoot1 = 0x32;
constexpr uint8_t kRegBUndershoot2 = 0x33;
// Datasheet §4.1 / RFAL ChangeTestRegisterBits(0x04, 0x10, 0x10) — disable false overheat trip.
constexpr uint8_t kTestOverheatReg = 0x04;
constexpr uint8_t kTestOverheatBit = 0x10;
constexpr uint8_t kFifoLoad = 0x80;
constexpr uint8_t kFifoRead = 0x9F;
constexpr uint8_t kPtAConfigLoad = 0xA0;
constexpr uint8_t kRegFifoStatus1 = 0x1E;
constexpr uint8_t kRegNumTxBytes1 = 0x22;
constexpr uint8_t kRegNumTxBytes2 = 0x23;

// Cap ST25 is 3.3 V (JW5712 buck). M5 CapST25R3916 defaults vdd_voltage_5V=false → sup3v.
constexpr uint8_t kIoSup3v = 0x80;
constexpr uint8_t kIoAatEn = 0x20;
constexpr uint8_t kIoMisoPd2 = 0x10;
constexpr uint8_t kIoMisoPd1 = 0x08;
constexpr uint8_t kIoDrvLvl = 0x04;
constexpr uint8_t kIoConf2Spi = kIoMisoPd1 | kIoMisoPd2 | kIoDrvLvl | kIoSup3v | kIoAatEn; // 0xBC
constexpr uint8_t kOpEn = 0x80;
constexpr uint8_t kOpRxEn = 0x40;
constexpr uint8_t kOpTxEn = 0x08;
constexpr uint8_t kOpEnFd = 0x03;
constexpr uint8_t kIsoAntcl = 0x01; // anticollision bit framing for REQA/AC
constexpr uint8_t kAuxNoCrcRx = 0x80;

// Target MODE: M5 Cap NFC-A emulate starts in bitrate-detect (0xC8), then 0x88 after PTA.
constexpr uint8_t kModeListenNfca = 0x88;      // targ | ISO14443A
constexpr uint8_t kModeBitrateDetect = 0xC8;   // targ | ISO14443A bit-rate detect

// IRQ regs 0x1A..0x1D packed as MAIN | NFC<<8 | ERR<<16 | PTA<<24 (poll; Cap NFC_IRQ=GPIO23).
constexpr uint32_t kIrqRxe = 0x00000010u;      // MAIN I_rxe
constexpr uint32_t kIrqTxe = 0x00000008u;      // MAIN I_txe
constexpr uint32_t kIrqEon = 0x00001000u;      // NFC  I_eon (external field)
constexpr uint32_t kIrqEof = 0x00000800u;      // NFC  I_eof
constexpr uint32_t kIrqNfct = 0x00000100u;     // NFC  I_nfct (bitrate recognized)
constexpr uint32_t kIrqWuA = 0x01000000u;      // PTA  I_wu_a
constexpr uint32_t kIrqWuAx = 0x02000000u;     // PTA  I_wu_ax
constexpr uint32_t kIrqRxePta = 0x10000000u;   // PTA  I_rxe_pta
constexpr uint32_t kListenIrqMask =
    kIrqRxe | kIrqTxe | kIrqEon | kIrqEof | kIrqNfct | kIrqWuA | kIrqWuAx | kIrqRxePta;

bool nfc_trace_on() {
    static const bool on = std::getenv("KVX_NFC_TRACE") != nullptr;
    return on;
}

void trace(const char* fmt, ...) {
    if (!nfc_trace_on()) return;
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[st25] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

std::string run_capture(const std::string& cmd) {
    std::string out;
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[256];
    while (std::fgets(buf, sizeof(buf), p)) out += buf;
    ::pclose(p);
    return out;
}

// `pinctrl get 7` → " 7: a0    pn | hi // GPIO7 = SPI0_CE1_N" → "a0".
std::string pinctrl_func(unsigned gpio) {
    const std::string out = run_capture("pinctrl get " + std::to_string(gpio) + " 2>/dev/null");
    const auto colon = out.find(':');
    if (colon == std::string::npos) return {};
    std::istringstream is(out.substr(colon + 1));
    std::string func;
    is >> func;
    return func;
}

bool ic_identity_ok(uint8_t id) {
    const uint8_t type = (id >> 3) & 0x1F;
    return type == 5 || type == 6;
}

std::string hex_bytes(const uint8_t* p, size_t n, char sep = 0) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02X", p[i]);
        if (sep && i) out.push_back(sep);
        out += b;
    }
    return out;
}

} // namespace

struct St25Session::Impl {
    std::unique_ptr<SpiDevice> spi;
    GpioLine cs{"/dev/gpiochip0", kNfcCsGpio};
    GpioLine ce1{"/dev/gpiochip0", kSpiCe1Gpio}; // keep CC1101 deselected
    std::string ce1_saved_func; // set when CE1 was remuxed via pinctrl; restored on close
    uint32_t seen_irqs{0};
    uint32_t pending_irqs{0};
    uint8_t uid[10]{};
    uint8_t uid_len{0};
    uint8_t atqa[2]{};
    uint8_t sak{0};

    void cs_low(std::string& error) { (void)cs.set_value(false, error); }
    void cs_high(std::string& error) { (void)cs.set_value(true, error); }

    bool xfer(const uint8_t* tx, uint8_t* rx, size_t len, std::string& error) {
        // Ensure CC1101 CE1 stays high for every ST25 transaction.
        if (ce1.ok()) (void)ce1.set_value(true, error);
        error.clear();
        cs_low(error);
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

    // RFAL st25r3916ReadTestRegister / WriteTestRegister (CS-low FC + addr[+READ] + data).
    bool read_test_reg(uint8_t addr, uint8_t& val, std::string& error) {
        uint8_t tx[3] = {kCmdTestAccess, static_cast<uint8_t>((addr & 0x3F) | kCmdRead), 0x00};
        uint8_t rx[3] = {};
        if (!xfer(tx, rx, 3, error)) return false;
        val = rx[2];
        return true;
    }

    bool write_test_reg(uint8_t addr, uint8_t val, std::string& error) {
        uint8_t tx[3] = {kCmdTestAccess, static_cast<uint8_t>(addr & 0x3F), val};
        uint8_t rx[3] = {};
        return xfer(tx, rx, 3, error);
    }

    // Shared-bus SPI glitches occasionally corrupt one test-reg read; retry before failing.
    bool unlock_overheat(std::string& error) {
        for (int i = 0; i < 3; ++i) {
            if (unlock_overheat_once(error)) return true;
            trace("unlock attempt %d: %s", i + 1, error.c_str());
            error.clear();
        }
        error = "ST25 overheat unlock did not stick (test reg 0x04)";
        return false;
    }

    // RFAL ChangeTestRegisterBits(0x04, 0x10, 0x10).
    bool unlock_overheat_once(std::string& error) {
        uint8_t cur = 0;
        if (!read_test_reg(kTestOverheatReg, cur, error)) {
            // Fall back to absolute M5 one-shot if test-read framing fails.
            error.clear();
            uint8_t tx[3] = {kCmdTestAccess, kTestOverheatReg, kTestOverheatBit};
            uint8_t rx[3] = {};
            return xfer(tx, rx, 3, error);
        }
        const uint8_t next = static_cast<uint8_t>((cur & ~kTestOverheatBit) | kTestOverheatBit);
        if (!write_test_reg(kTestOverheatReg, next, error)) return false;
        uint8_t verify = 0;
        if (!read_test_reg(kTestOverheatReg, verify, error)) return false;
        if ((verify & kTestOverheatBit) == 0) {
            error = "ST25 overheat unlock did not stick (test reg 0x04)";
            return false;
        }
        return true;
    }

    bool write_ntx(uint16_t bytes, uint8_t bits, std::string& error) {
        // M5: ((bytes & 0x1FF) << 3) | (bits & 7), written BE to NTX1/NTX2.
        const uint16_t ntx =
            static_cast<uint16_t>(((bytes & 0x01FFu) << 3) | static_cast<uint16_t>(bits & 0x07u));
        if (!write_reg(kRegNumTxBytes1, static_cast<uint8_t>((ntx >> 8) & 0xFF), error)) return false;
        return write_reg(kRegNumTxBytes2, static_cast<uint8_t>(ntx & 0xFF), error);
    }

    // Cap / M5 UnitST25R3916::begin essentials (3.3 V SPI Cap).
    bool chip_init(std::string& error) {
        (void)direct_cmd(kCmdStop, error);
        error.clear();
        (void)modify_reg(kRegOpControl, kOpTxEn | kOpRxEn, 0x00, error);
        error.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        if (!direct_cmd(kCmdSetDefault, error)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        // Disable false internal overheat trip (datasheet §4.1). Soft-fail: Passive Target
        // load-mod still works without it, and shared-SPI glitches falsely trip verify.
        if (!unlock_overheat(error)) {
            trace("chip_init: %s (continuing)", error.c_str());
            error.clear();
        }

        (void)write_reg(kRegIoConf1, 0x07, error); // MCU_CLK off
        error.clear();
        // Cap is 3.3 V: must set sup3v or TX/regulators are wrong (M5 Cap default).
        if (!write_reg(kRegIoConf2, kIoConf2Spi, error)) return false;
        (void)write_reg(kRegTxDriver, 0xD0, error); // M5 default tx_am_modulation=13, d_res=0
        error.clear();
        (void)write_reg(kRegFieldThrActv, 0x13, error);
        error.clear();
        (void)write_reg(kRegFieldThrDeactv, 0x02, error);
        error.clear();
        (void)write_reg(kRegPassiveTarget, 0x50, error); // FDT nibble like M5 (0x05<<4)
        error.clear();
        (void)write_reg(kRegPtMod, 0x5F, error);
        error.clear();
        (void)write_reg(kRegAntTuneA, 0x82, error);
        error.clear();
        (void)write_reg(kRegAntTuneB, 0x82, error);
        error.clear();
        (void)modify_reg(kRegOpControl, 0x00, kOpEnFd, error); // auto EFD
        error.clear();

        if (!modify_reg(kRegOpControl, 0x00, kOpEn, error)) return false; // oscillator
        bool osc = false;
        for (int i = 0; i < 50; ++i) {
            uint8_t aux = 0;
            if (read_reg(kRegAuxDisplay, aux, error) && (aux & 0x10)) {
                osc = true;
                break;
            }
            error.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!osc) {
            error = "ST25 oscillator not ready";
            return false;
        }
        (void)direct_cmd(kCmdAdjustRegulators, error);
        error.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    }

    // M5 configure_nfc_a + nfc_initial_field_on.
    bool configure_poller_nfca(std::string& error) {
        if (!chip_init(error)) return false;

        // Initiator ISO14443A + nfc_ar auto (0x08 | 0x01).
        if (!write_reg(kRegMode, 0x09, error)) return false;
        if (!write_reg(kRegBitrate, 0x00, error)) return false; // 106/106
        if (!write_reg(kRegIso14443a, 0x00, error)) return false;
        (void)modify_reg(kRegAux, 0x04, 0x00, error); // clear dis_corr
        error.clear();

        // Stability-focused RX (M5 Cap configure_nfc_a).
        (void)write_reg(kRegRxConf1, 0x08, error); // z_600k
        error.clear();
        // sqm_dyn(0x20)|agc_en(0x08)|agc_m(0x04)|agc6_3(0x01) = 0x2D
        (void)write_reg(kRegRxConf2, 0x2D, error);
        error.clear();
        (void)write_reg(kRegRxConf3, 0xD8, error);
        error.clear();
        (void)write_reg(kRegRxConf4, 0x22, error);
        error.clear();
        // M5 configure_nfc_a: TX pulse shaping (tags must decode our REQA) + correlator.
        (void)write_reg_b(kRegBOvershoot1, 0x40, error);
        (void)write_reg_b(kRegBOvershoot2, 0x03, error);
        (void)write_reg_b(kRegBUndershoot1, 0x40, error);
        (void)write_reg_b(kRegBUndershoot2, 0x03, error);
        (void)write_reg_b(kRegBCorrConf1, 0x47, error);
        (void)write_reg_b(kRegBCorrConf2, 0x00, error);
        error.clear();
        (void)direct_cmd(kCmdResetRxGain, error);
        error.clear();

        // Guard / no-response timer ~5 ms for REQA.
        (void)write_reg(kRegNoRespTimer1, 0x00, error);
        error.clear();
        (void)write_reg(kRegNoRespTimer2, 0x80, error);
        error.clear();

        uint8_t op = 0;
        if (!read_reg(kRegOpControl, op, error)) return false;
        if (op & kOpTxEn) {
            (void)modify_reg(kRegOpControl, kOpTxEn | kOpRxEn, 0x00, error);
            error.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // Staged Ready → collision-avoid field → TX → RX (M5 Cap).
        (void)modify_reg(kRegOpControl, 0x00, kOpEn | kOpEnFd, error);
        error.clear();
        if (!direct_cmd(kCmdNfcInitialFieldOn, error)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!write_reg(kRegOpControl, static_cast<uint8_t>(kOpEn | kOpTxEn), error)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!write_reg(kRegOpControl, static_cast<uint8_t>(kOpEn | kOpRxEn | kOpTxEn | kOpEnFd), error))
            return false;
        // ISO14443-3 guard time is >= 5 ms; Flipper emulation and weakly coupled tags need longer.
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        if (nfc_trace_on()) {
            uint8_t op_r = 0, mode_r = 0, txd = 0, aux = 0, io2 = 0;
            std::string e;
            (void)read_reg(kRegOpControl, op_r, e);
            (void)read_reg(kRegMode, mode_r, e);
            (void)read_reg(kRegTxDriver, txd, e);
            (void)read_reg(kRegAuxDisplay, aux, e);
            (void)read_reg(kRegIoConf2, io2, e);
            const uint32_t irqs = read_irqs(e);
            trace("field on: OP=0x%02X MODE=0x%02X TXDRV=0x%02X IO2=0x%02X AUX=0x%02X irqs=0x%08X", op_r, mode_r,
                  txd, io2, aux, irqs);
        }
        // AUXDISP.tx_on only flags an in-flight frame, not field presence — don't check it here.
        return true;
    }

    bool field_on(std::string& error) { return configure_poller_nfca(error); }

    bool tx_bits(const uint8_t* data, size_t nbytes, uint8_t bitlen_low, bool with_crc,
                 std::string& error) {
        if (!write_ntx(static_cast<uint16_t>(nbytes), bitlen_low, error)) return false;
        std::vector<uint8_t> tx(1 + nbytes);
        tx[0] = kFifoLoad;
        std::memcpy(tx.data() + 1, data, nbytes);
        std::vector<uint8_t> rx(tx.size());
        if (!xfer(tx.data(), rx.data(), tx.size(), error)) return false;
        return direct_cmd(with_crc ? kCmdTransmit : kCmdTransmitNoCrc, error);
    }

    uint16_t fifo_count(std::string& error) {
        uint8_t st1 = 0;
        uint8_t st2 = 0;
        if (!read_reg(kRegFifoStatus1, st1, error)) return 0;
        if (!read_reg(static_cast<uint8_t>(kRegFifoStatus1 + 1), st2, error)) return st1;
        return static_cast<uint16_t>(st1) | (static_cast<uint16_t>((st2 >> 6) & 0x03) << 8);
    }

    bool wait_fifo(uint8_t min_bytes, std::vector<uint8_t>& out, std::string& error, bool* cancel,
                   int timeout_ms) {
        out.clear();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            seen_irqs |= read_irqs(error);
            error.clear();
            const uint16_t nbytes = fifo_count(error);
            error.clear();
            if (nbytes >= min_bytes) {
                const uint8_t n = static_cast<uint8_t>(std::min<uint16_t>(nbytes, 64));
                std::vector<uint8_t> rtx(1 + n, 0);
                rtx[0] = kFifoRead;
                std::vector<uint8_t> rrx(rtx.size(), 0);
                if (!xfer(rtx.data(), rrx.data(), rtx.size(), error)) return false;
                out.assign(rrx.begin() + 1, rrx.end());
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        error = "no NFC response";
        return false;
    }

    // M5 nfca_request_wakeup — CMD_TRANSMIT_REQA/WUPA, not FIFO 0x26/0x52.
    // Cap coupling is marginal: a single request often gets no ATQA, so retry and
    // alternate with WUPA (also wakes HALTed tags).
    bool reqa(std::string& error, bool* cancel) {
        constexpr int kAttempts = 8;
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            (void)write_reg(kRegIso14443a, kIsoAntcl, error);
            error.clear();
            (void)modify_reg(kRegAux, 0x00, kAuxNoCrcRx, error);
            error.clear();
            (void)read_irqs(error);
            error.clear();
            (void)direct_cmd(kCmdClearFifo, error);
            error.clear();
            const uint8_t cmd = (attempt & 1) ? kCmdTransmitWupa : kCmdTransmitReqa;
            if (!direct_cmd(cmd, error)) return false;
            std::vector<uint8_t> rx;
            seen_irqs = 0;
            if (wait_fifo(2, rx, error, cancel, 15)) {
                atqa[0] = rx[0];
                atqa[1] = rx.size() > 1 ? rx[1] : 0;
                trace("%s #%d ATQA=%02X %02X irqs=0x%08X", (attempt & 1) ? "WUPA" : "REQA", attempt + 1,
                      atqa[0], atqa[1], seen_irqs);
                error.clear();
                return true;
            }
            trace("%s #%d no ATQA irqs=0x%08X (main txe=%d rxs=%d nre=%d)", (attempt & 1) ? "WUPA" : "REQA",
                  attempt + 1, seen_irqs, !!(seen_irqs & 0x08), !!(seen_irqs & 0x20), !!(seen_irqs & 0x4000));
            if (error == "cancelled") return false;
            error.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        error = "no NFC response";
        return false;
    }

    // ISO14443-A anticollision cascade level 1 (4-byte UID path).
    bool anticoll_select(std::string& error, bool* cancel) {
        uid_len = 0;
        sak = 0;
        atqa[0] = atqa[1] = 0;
        if (!field_on(error)) return false;
        if (!reqa(error, cancel)) return false;
        std::vector<uint8_t> rx;

        auto ac_frame = [&](const uint8_t* frame, size_t n) -> bool {
            (void)write_reg(kRegIso14443a, kIsoAntcl, error);
            error.clear();
            (void)modify_reg(kRegAux, 0x00, kAuxNoCrcRx, error);
            error.clear();
            (void)direct_cmd(kCmdClearFifo, error);
            error.clear();
            return tx_bits(frame, n, 0, false, error);
        };
        auto sel_frame = [&](const uint8_t* frame, size_t n) -> bool {
            (void)write_reg(kRegIso14443a, 0x00, error);
            error.clear();
            (void)modify_reg(kRegAux, kAuxNoCrcRx, 0x00, error);
            error.clear();
            (void)direct_cmd(kCmdClearFifo, error);
            error.clear();
            return tx_bits(frame, n, 0, true, error);
        };

        // Anticollision CL1
        uint8_t ac[] = {0x93, 0x20};
        if (!ac_frame(ac, 2)) return false;
        if (!wait_fifo(5, rx, error, cancel, 50)) return false;
        if (rx.size() < 5) {
            error = "anticoll short";
            return false;
        }
        const bool cascaded = (rx[0] == 0x88); // CT — 7-byte UID
        std::memcpy(uid, rx.data(), 4);
        uid_len = 4;
        const uint8_t bcc1 = rx[4];
        // SELECT CL1
        uint8_t sel[7] = {0x93, 0x70, uid[0], uid[1], uid[2], uid[3], bcc1};
        if (!sel_frame(sel, 7)) return false;
        if (!wait_fifo(1, rx, error, cancel, 50)) return false;
        sak = rx[0];
        // Cascade bit or CT → CL2 (ATQA 44 00 NTAG path)
        if ((sak & 0x04) || cascaded) {
            uint8_t ac2[] = {0x95, 0x20};
            if (!ac_frame(ac2, 2)) return false;
            if (!wait_fifo(5, rx, error, cancel, 50)) return false;
            if (rx.size() < 5) {
                error = "anticoll CL2 short";
                return false;
            }
            // Drop CT (0x88); keep UID0..UID2 from CL1, append UID3..UID6 from CL2.
            uid[0] = uid[1];
            uid[1] = uid[2];
            uid[2] = uid[3];
            std::memcpy(uid + 3, rx.data(), 4);
            uid_len = 7;
            uint8_t sel2[7] = {0x95, 0x70, rx[0], rx[1], rx[2], rx[3], rx[4]};
            if (!sel_frame(sel2, 7)) return false;
            if (!wait_fifo(1, rx, error, cancel, 50)) return false;
            sak = rx[0];
        }
        return true;
    }

    // M5 nfcaReadBlock waits for 16 payload bytes (CRC checked by HW, not required in FIFO count).
    bool t2t_read(uint8_t page, uint8_t out[16], std::string& error, bool* cancel) {
        for (int attempt = 0; attempt < 5; ++attempt) {
            (void)write_reg(kRegIso14443a, 0x00, error); // standard framing
            error.clear();
            (void)modify_reg(kRegAux, kAuxNoCrcRx, 0x00, error); // expect CRC on RX
            error.clear();
            (void)read_irqs(error);
            error.clear();
            (void)direct_cmd(kCmdClearFifo, error);
            error.clear();
            uint8_t cmd[2] = {0x30, page};
            if (!tx_bits(cmd, 2, 0, true, error)) return false;
            std::vector<uint8_t> rx;
            if (wait_fifo(16, rx, error, cancel, 50) && rx.size() >= 16) {
                std::memcpy(out, rx.data(), 16);
                return true;
            }
            if (error == "cancelled") return false;
            trace("t2t_read page %u attempt %d: %s (%zu bytes)", page, attempt + 1,
                  error.empty() ? "short" : error.c_str(), rx.size());
            error.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        error = "page read failed";
        return false;
    }

    bool t2t_write(uint8_t page, const uint8_t data[4], std::string& error, bool* cancel) {
        uint8_t cmd[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
        if (!tx_bits(cmd, 6, 0, true, error)) return false;
        std::vector<uint8_t> rx;
        (void)wait_fifo(1, rx, error, cancel, 50); // ACK
        error.clear();
        return true;
    }

    bool modify_reg(uint8_t addr, uint8_t clr, uint8_t set, std::string& error) {
        uint8_t v = 0;
        if (!read_reg(addr, v, error)) return false;
        v = static_cast<uint8_t>((v & ~clr) | set);
        return write_reg(addr, v, error);
    }

    bool write_pt_mem_a(const uint8_t* values, size_t length, std::string& error) {
        if (length == 0 || length > 15) {
            error = "PT mem length";
            return false;
        }
        std::vector<uint8_t> tx(1 + length);
        tx[0] = kPtAConfigLoad;
        std::memcpy(tx.data() + 1, values, length);
        std::vector<uint8_t> rx(tx.size());
        return xfer(tx.data(), rx.data(), tx.size(), error);
    }

    uint32_t read_irqs(std::string& error) {
        // Reading IRQ_MAIN..IRQ_TARGET clears latched status.
        uint8_t tx[5] = {static_cast<uint8_t>(kRegIrqMain | kCmdRead), 0, 0, 0, 0};
        uint8_t rx[5] = {};
        if (!xfer(tx, rx, 5, error)) return 0;
        return static_cast<uint32_t>(rx[1]) | (static_cast<uint32_t>(rx[2]) << 8) |
               (static_cast<uint32_t>(rx[3]) << 16) | (static_cast<uint32_t>(rx[4]) << 24);
    }

    void irq_mask_set(uint32_t enable_mask, std::string& error) {
        // Chip mask regs: 1 = masked. Software enable_mask bits should be unmasked (0).
        // Always keep Error/wakeup (byte 2) fully masked — I_wam/I_wph are noise for NFC-A listen.
        const uint32_t chip_mask = (~enable_mask) | 0x00FF0000u;
        for (int i = 0; i < 4; ++i) {
            (void)write_reg(static_cast<uint8_t>(kRegIrqMaskMain + i),
                            static_cast<uint8_t>((chip_mask >> (8 * i)) & 0xFF), error);
            error.clear();
        }
    }

    uint16_t fifo_bytes(std::string& error) {
        uint8_t st1 = 0;
        uint8_t st2 = 0;
        if (!read_reg(kRegFifoStatus1, st1, error)) return 0;
        if (!read_reg(static_cast<uint8_t>(kRegFifoStatus1 + 1), st2, error)) return st1;
        // STATUS2 fifo_b[9:8] live in bits [7:6].
        return static_cast<uint16_t>(st1) | (static_cast<uint16_t>((st2 >> 6) & 0x03) << 8);
    }

    bool read_fifo(uint8_t* out, uint8_t n, std::string& error) {
        if (n == 0) return true;
        std::vector<uint8_t> tx(1 + n, 0);
        tx[0] = kFifoRead;
        std::vector<uint8_t> rx(tx.size(), 0);
        if (!xfer(tx.data(), rx.data(), tx.size(), error)) return false;
        std::memcpy(out, rx.data() + 1, n);
        return true;
    }

    bool listen_respond(const uint8_t* resp, uint16_t len, std::string& error) {
        (void)direct_cmd(kCmdClearFifo, error);
        error.clear();
        if (!write_ntx(len, 0, error)) return false;
        std::vector<uint8_t> tx(1 + len);
        tx[0] = kFifoLoad;
        std::memcpy(tx.data() + 1, resp, len);
        std::vector<uint8_t> rx(tx.size());
        if (!xfer(tx.data(), rx.data(), tx.size(), error)) return false;
        if (!direct_cmd(kCmdTransmit, error)) return false;
        return wait_txe(error);
    }

    // T2T 4-bit NAK (0x0), no CRC.
    bool listen_nak(std::string& error) {
        (void)direct_cmd(kCmdClearFifo, error);
        error.clear();
        if (!write_ntx(0, 4, error)) return false;
        uint8_t tx[2] = {kFifoLoad, 0x00};
        uint8_t rx[2] = {};
        if (!xfer(tx, rx, 2, error)) return false;
        if (!direct_cmd(kCmdTransmitNoCrc, error)) return false;
        return wait_txe(error);
    }

    // Reading IRQs clears them, so keep everything except our own txe for the listen loop.
    bool wait_txe(std::string& error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            const uint32_t irqs = read_irqs(error);
            error.clear();
            pending_irqs |= irqs & ~kIrqTxe;
            if (irqs & kIrqTxe) return true;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return true;
    }

    uint32_t take_pending_irqs() {
        const uint32_t v = pending_irqs;
        pending_irqs = 0;
        return v;
    }

    bool write_reg_b(uint8_t addr, uint8_t val, std::string& error) {
        uint8_t tx[3] = {kCmdSpaceB, static_cast<uint8_t>(addr & 0x3F), val};
        uint8_t rx[3] = {};
        return xfer(tx, rx, 3, error);
    }

    bool chip_init_listen(std::string& error) {
        // Same Cap 3.3 V / SPI bring-up as poller, then M5 emulate antenna.
        if (!chip_init(error)) return false;
        // Kill wake-up mode leftovers — otherwise I_wam/I_wph flood the IRQ regs.
        (void)modify_reg(kRegOpControl, 0x04, 0x00, error); // wu off
        error.clear();
        (void)write_reg(kRegWakeupTimer, 0x00, error);
        error.clear();
        // Most sensitive peer-detect thresholds so a phone's field trips I_eon on the Cap coil.
        (void)write_reg(kRegFieldThrActv, 0x00, error);
        error.clear();
        (void)write_reg(kRegFieldThrDeactv, 0x00, error);
        error.clear();
        // M5 emulate antenna (load-mod). AAT DAC off so it doesn't fight fixed tune.
        (void)modify_reg(kRegIoConf2, kIoAatEn, 0x00, error);
        error.clear();
        (void)write_reg(kRegAntTuneA, 0x00, error);
        error.clear();
        (void)write_reg(kRegAntTuneB, 0xFF, error);
        error.clear();
        (void)write_reg(kRegRxConf1, 0x08, error);
        error.clear();
        (void)write_reg(kRegRxConf2, 0x2D, error);
        error.clear();
        return true;
    }

    bool setup_listen_nfca(const uint8_t* uid_buf, uint8_t uid_len, const uint8_t atqa[2], uint8_t sak,
                           std::string& error) {
        if (uid_len != 4 && uid_len != 7) {
            error = "UID must be 4 or 7 bytes";
            return false;
        }
        if (!chip_init_listen(error)) return false;

        // M5 configure_emulation_a + ListenerST25R3916ForA::start_emulation / goto_off.
        (void)direct_cmd(kCmdStop, error);
        error.clear();

        // AUX nfc_id first so PT mem matches UID length.
        (void)modify_reg(kRegAux, 0x30, (uid_len == 7) ? 0x10 : 0x00, error);
        error.clear();

        uint8_t pt[15] = {};
        std::memcpy(pt, uid_buf, uid_len);
        pt[10] = atqa[0];
        pt[11] = atqa[1];
        // M5: cascade-incomplete SAK on CL1 for 7-byte UIDs (sak | 0x04).
        if (uid_len == 4) {
            pt[12] = static_cast<uint8_t>(sak & ~0x04);
            pt[13] = static_cast<uint8_t>(sak & ~0x04);
            pt[14] = static_cast<uint8_t>(sak & ~0x04);
        } else {
            pt[12] = static_cast<uint8_t>(sak | 0x04);
            pt[13] = static_cast<uint8_t>(sak & ~0x04);
            pt[14] = static_cast<uint8_t>(sak & ~0x04);
        }
        if (!write_pt_mem_a(pt, sizeof(pt), error)) return false;

        // Auto NFC-A AC only; disable AP2P / 212-424 first-response bits (M5 0x5C then clear d_106).
        (void)write_reg(kRegPassiveTarget, 0x5C, error);
        error.clear();
        (void)modify_reg(kRegPassiveTarget, 0x01, 0x00, error); // enable d_106_ac_a (auto-AC)
        error.clear();

        (void)modify_reg(kRegTimerEmv, 0xE0, 0x00, error); // GPT trigger off
        error.clear();
        (void)modify_reg(kRegTimerEmv, 0x00, 0x08, error); // mrt_step 512/fc
        error.clear();
        (void)write_reg(kRegMaskRxTimer, 0x02, error); // ~100 µs MRT
        error.clear();
        (void)modify_reg(kRegIso14443a, 0xE0, 0x00, error); // parity on, NFCIP-1 off
        error.clear();

        (void)direct_cmd(kCmdUnmaskRx, error);
        error.clear();
        (void)direct_cmd(kCmdStop, error);
        error.clear();
        (void)modify_reg(kRegOpControl, 0x00, 0x40, error); // rx_en
        error.clear();
        (void)modify_reg(kRegPassiveTarget, 0x01, 0x00, error); // auto-AC on
        error.clear();
        (void)direct_cmd(kCmdGotoSense, error);
        error.clear();
        (void)modify_reg(kRegAux, 0x00, 0x80, error); // no_crc_rx while sensing
        error.clear();

        // Bitrate-detect target mode (0xC8) — required Cap path before fixed 0x88.
        (void)write_reg(kRegMode, kModeBitrateDetect, error);
        error.clear();

        (void)read_irqs(error);
        error.clear();
        irq_mask_set(kListenIrqMask, error);
        error.clear();

        // Cap Passive Target does not TX its own field — wait for Flipper/phone field (I_eon).
        // Leave en_fd=11 (peer/CA auto) with en=rx=tx=0 → low-power NFC field detect.
        (void)write_reg(kRegOpControl, kOpEnFd, error);
        error.clear();

        uint8_t id = 0, mode = 0, op = 0, io2 = 0, aux = 0, ftha = 0, ptdef = 0;
        (void)read_reg(kRegIcIdentity, id, error);
        (void)read_reg(kRegMode, mode, error);
        (void)read_reg(kRegOpControl, op, error);
        (void)read_reg(kRegIoConf2, io2, error);
        (void)read_reg(kRegAuxDisplay, aux, error);
        (void)read_reg(kRegFieldThrActv, ftha, error);
        (void)read_reg(kRegPassiveTarget, ptdef, error);
        trace("listen arm IC=0x%02X MODE=0x%02X OP=0x%02X IO2=0x%02X AUX=0x%02X FTHA=0x%02X PT=0x%02X", id,
              mode, op, io2, aux, ftha, ptdef);
        if (!ic_identity_ok(id)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "listen arm SPI dead IC=0x%02X", id);
            error = buf;
            return false;
        }
        if ((mode & 0xF8) != kModeBitrateDetect) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "listen arm failed MODE=0x%02X (want 0xC8)", mode);
            error = buf;
            return false;
        }
        error.clear();
        return true;
    }

    // Re-arm Sense after field drop (M5 goto_off).
    void listen_rearm_sense(std::string& error) {
        (void)direct_cmd(kCmdStop, error);
        error.clear();
        (void)modify_reg(kRegOpControl, 0x00, 0x40, error); // rx_en
        error.clear();
        (void)modify_reg(kRegPassiveTarget, 0x01, 0x00, error); // auto-AC on
        error.clear();
        (void)direct_cmd(kCmdGotoSense, error);
        error.clear();
        (void)modify_reg(kRegAux, 0x00, 0x80, error); // no_crc_rx
        error.clear();
        (void)write_reg(kRegMode, kModeBitrateDetect, error);
        error.clear();
        (void)direct_cmd(kCmdClearFifo, error);
        error.clear();
        (void)direct_cmd(kCmdUnmaskRx, error);
        error.clear();
        (void)read_irqs(error);
        error.clear();
    }

    void listen_stop(std::string& error) {
        (void)direct_cmd(kCmdStop, error);
        error.clear();
        irq_mask_set(0, error); // mask all
        error.clear();
        (void)read_irqs(error);
        error.clear();
        // Back to poller defaults used by read path.
        (void)write_reg(kRegMode, 0x08, error);
        error.clear();
        (void)write_reg(kRegOpControl, 0x00, error);
        error.clear();
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
    {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "%s CS=GPIO%u IRQ=GPIO%u SPI mode1 (Cap-CC1101)",
                      id.c_str(), kNfcCsGpio, kNfcIrqGpio);
        ok.detail = detail;
    }
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
    // Best-effort: hold CE1 high so CC1101 stays off the shared SPI bus.
    // May fail if the SPI driver owns GPIO7 — SPI_NO_CS still keeps CE idle-high.
    {
        std::string ign;
        if (impl_->ce1.request_output(true, ign)) {
            trace("CE1 GPIO%u held high via gpiod", kSpiCe1Gpio);
        } else {
            // SPI driver owns CE1: remux to GPIO output-high like cap_nfc_probe.py, restore on close.
            // BCM SPI0_CE1_N is ALT0 ("a0"). If a prior run left the pin as "op", still restore to a0.
            const std::string cur = pinctrl_func(kSpiCe1Gpio);
            impl_->ce1_saved_func = (cur.empty() || cur == "op") ? "a0" : cur;
            const int rc = std::system(("pinctrl set " + std::to_string(kSpiCe1Gpio) + " op dh").c_str());
            trace("CE1 gpiod failed (%s); pinctrl op dh rc=%d was=%s restore=%s", ign.c_str(), rc, cur.c_str(),
                  impl_->ce1_saved_func.c_str());
            if (rc != 0) impl_->ce1_saved_func.clear();
        }
    }
    SpiConfig cfg;
    // Prefer spidev0.0 so the SPI controller never toggles Cap CC1101 CE1 (GPIO7) while we
    // soft-CS ST25 on GPIO22 (CE1 held high as GPIO). Contending CE1+GPIO22 corrupts TX bring-up.
    cfg.speed_hz = 1000000;
    cfg.mode = 1;
    cfg.no_chip_select = true;
    std::unique_ptr<SpiDevice> spi;
    if (!spidev.empty()) {
        cfg.path = spidev;
        spi = SpiDevice::open(cfg, error);
    } else if (::access("/dev/spidev0.0", F_OK) == 0) {
        cfg.path = "/dev/spidev0.0";
        spi = SpiDevice::open(cfg, error);
        if (!spi) {
            error.clear();
            cfg.path = default_cap_spidev_path();
            spi = SpiDevice::open(cfg, error);
        }
    } else {
        cfg.path = default_cap_spidev_path();
        spi = SpiDevice::open(cfg, error);
    }
    if (!spi) {
        trace("spi open %s failed: %s", cfg.path.c_str(), error.c_str());
        close();
        return false;
    }
    impl_->spi = std::move(spi);
    if (!impl_->ce1_saved_func.empty()) {
        // Opening spidev can remux CE1 back to SPI alt; force GPIO high again.
        (void)std::system(("pinctrl set " + std::to_string(kSpiCe1Gpio) + " op dh").c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    (void)impl_->direct_cmd(kCmdSetDefault, error);
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // Unlock + Ready so IC identity is readable (power-down returns zeros on some paths).
    {
        std::string e;
        (void)impl_->unlock_overheat(e);
        e.clear();
    }
    (void)impl_->write_reg(kRegIoConf2, kIoConf2Spi, error);
    error.clear();
    (void)impl_->write_reg(kRegOpControl, 0x80, error);
    error.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    uint8_t id = 0;
    for (int i = 0; i < 5; ++i) {
        std::string e;
        if (impl_->read_reg(kRegIcIdentity, id, e) && ic_identity_ok(id)) break;
        id = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    trace("open %s IC=0x%02X CE1 func now=%s", cfg.path.c_str(), id, pinctrl_func(kSpiCe1Gpio).c_str());
    if (!ic_identity_ok(id)) {
        error = "ST25 SPI dead (IC identity)";
        close();
        return false;
    }
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
    impl_->ce1.release();
    if (!impl_->ce1_saved_func.empty()) {
        // Give CE1 back to the SPI controller so the CC1101 (spidev0.1 HW CS) keeps working.
        (void)std::system(
            ("pinctrl set " + std::to_string(kSpiCe1Gpio) + " " + impl_->ce1_saved_func).c_str());
        trace("CE1 restored to %s", impl_->ce1_saved_func.c_str());
    }
    delete impl_;
    impl_ = nullptr;
}

bool St25Session::ok() const { return impl_ && impl_->spi && impl_->spi->ok() && impl_->cs.ok(); }

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
    tools::NfcDump d;
    if (!read_dump(d, error, cancel)) return false;
    uid_hex = d.uid_hex;
    return true;
}

bool St25Session::read_dump(tools::NfcDump& out, std::string& error, bool* cancel) {
    out = {};
    if (!ok()) {
        error = "ST25 not open";
        return false;
    }
    if (!impl_->anticoll_select(error, cancel)) {
        trace("read_dump: %s (ATQA=%02X %02X)", error.c_str(), impl_->atqa[0], impl_->atqa[1]);
        return false;
    }
    trace("read_dump: UID=%s SAK=%02X", hex_bytes(impl_->uid, impl_->uid_len).c_str(), impl_->sak);
    out.uid_hex = hex_bytes(impl_->uid, impl_->uid_len);
    out.atqa_hex = hex_bytes(impl_->atqa, 2, ' ');
    out.sak_hex = hex_bytes(&impl_->sak, 1);
    out.device_type = "ISO14443-3A";
    std::ostringstream pages;

    if (impl_->sak == 0x00) {
        uint8_t buf[16];
        int total = 64;
        if (impl_->t2t_read(3, buf, error, cancel)) {
            if (buf[2] == 0x12) total = 45;
            else if (buf[2] == 0x3E) total = 135;
            else if (buf[2] == 0x6D) total = 231;
        }
        error.clear();
        out.device_type = (total == 45) ? "NTAG213" : (total == 135) ? "NTAG215" : (total == 231) ? "NTAG216"
                                                                                                  : "MIFARE Ultralight";
        out.pages_total = total;
        for (int page = 0; page < total; page += 4) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            if (!impl_->t2t_read(static_cast<uint8_t>(page), buf, error, cancel)) {
                out.page_read_ok = false;
                break;
            }
            for (int o = 0; o < 4 && page + o < total; ++o) {
                char line[48];
                std::snprintf(line, sizeof(line), "Page %d: %02X %02X %02X %02X\n", page + o,
                              buf[o * 4], buf[o * 4 + 1], buf[o * 4 + 2], buf[o * 4 + 3]);
                pages << line;
                ++out.pages_read;
            }
        }
        out.pages_text = pages.str();
        const auto recs = tools::ndef_parse_from_pages(out.pages_text);
        if (!recs.empty()) {
            std::ostringstream nd;
            for (const auto& r : recs) {
                nd << r.type_label << ": " << r.summary << "\n";
                if (!r.detail.empty()) nd << "  " << r.detail << "\n";
            }
            out.ndef_text = nd.str();
        }
    } else if (impl_->sak == 0x08 || impl_->sak == 0x09 || impl_->sak == 0x18) {
        out.device_type = "Mifare Classic";
        out.ndef_text =
            "Classic sector dump needs RFAL Crypto1 on Cap ST25; use Dict attack after PN532 "
            "or port RFAL. UID/ATQA/SAK captured.";
        out.pages_read = 0;
        out.pages_total = (impl_->sak == 0x18) ? 256 : 64;
        out.page_read_ok = false;
    } else if (impl_->sak & 0x20) {
        out.device_type = "ISO14443-4A / DESFire?";
        out.ndef_text = "ISO-DEP activated at select; APDU exchange needs RFAL isoDep on Cap ST25.";
    }
    out.pages_text = pages.str();
    error.clear();
    return true;
}

bool St25Session::field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel) {
    present = false;
    uid_hex.clear();
    if (!ok()) {
        error = "ST25 not open";
        return false;
    }
    // anticoll_select does field_on + REQA itself; a separate field_on/REQA first would
    // drop the field (Set Default) and reset the tag mid-poll.
    if (impl_->anticoll_select(error, cancel)) {
        present = true;
        uid_hex = hex_bytes(impl_->uid, impl_->uid_len);
        error.clear();
        return true;
    }
    if (error == "cancelled") return false;
    if (impl_->atqa[0] || impl_->atqa[1]) {
        present = true;
        uid_hex = hex_bytes(impl_->atqa, 2);
        error.clear();
        return true;
    }
    error.clear();
    present = false;
    return true;
}

bool St25Session::erase_ultralight(std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel)) return false;
    if (impl_->sak != 0x00) {
        error = "Erase here targets NTAG/Ultralight";
        return false;
    }
    const uint8_t zero[4] = {0, 0, 0, 0};
    for (int page = 4; page < d.pages_total - 5; ++page) {
        if (!impl_->t2t_write(static_cast<uint8_t>(page), zero, error, cancel)) return false;
    }
    return true;
}

bool St25Session::write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel)) return false;
    if (impl_->sak != 0x00) {
        error = "Tag is not MIFARE Ultralight.";
        return false;
    }
    size_t off = 0;
    for (int page = 4; off < tlv.size(); ++page) {
        uint8_t block[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4 && off < tlv.size(); ++i, ++off) block[i] = tlv[off];
        if (!impl_->t2t_write(static_cast<uint8_t>(page), block, error, cancel)) return false;
    }
    return true;
}

bool St25Session::lock_ntag(std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel)) return false;
    if (impl_->sak != 0x00) {
        error = "Not an NTAG/Ultralight tag.";
        return false;
    }
    uint8_t page0[16];
    if (!impl_->t2t_read(0, page0, error, cancel)) return false;
    uint8_t lock[4] = {page0[8], page0[9], 0xFF, 0xFF};
    return impl_->t2t_write(2, lock, error, cancel);
}

bool St25Session::set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                                    bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel)) return false;
    if (impl_->sak != 0x00) {
        error = "Not supported here.";
        return false;
    }
    const int pwd_page = (d.pages_total >= 45) ? d.pages_total - 2 : 0x29;
    if (!impl_->t2t_write(static_cast<uint8_t>(pwd_page), pwd, error, cancel)) return false;
    uint8_t pk[4] = {pack[0], pack[1], 0, 0};
    return impl_->t2t_write(static_cast<uint8_t>(pwd_page + 1), pk, error, cancel);
}

bool St25Session::clear_ntag_password(std::string& error, bool* cancel) {
    const uint8_t pwd[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t pack[2] = {0, 0};
    return set_ntag_password(pwd, pack, error, cancel);
}

bool St25Session::transceive_apdu(const std::vector<uint8_t>&, std::vector<uint8_t>&, std::string& error,
                                  bool*) {
    error = "APDU on Cap ST25 needs RFAL isoDep (not wired yet). Use PN532 I2C.";
    return false;
}

bool St25Session::emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel,
                                   int timeout_ms) {
    if (!ok()) {
        error = "ST25 not open";
        return false;
    }

    // Cap phones read Type-2 (NTAG) far more reliably than our software T4T path.
    // Build a synthetic NTAG213 (45 pages) with the NDEF TLV in user memory.
    auto payload = tlv;
    if (payload.empty() || payload[0] != 0x03) {
        payload = tools::ndef_tlv_uri("https://github.com/kvx17/kvxputer");
    }
    if (payload.size() > 144) {
        error = "NDEF too large for NTAG213 emulate";
        return false;
    }

    // 7-byte NXP-style UID (ATQA 0x44 0x00, SAK 0x00).
    const uint8_t uid7[7] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    const uint8_t atqa[2] = {0x44, 0x00};
    const uint8_t sak = 0x00;
    const uint8_t bcc0 = static_cast<uint8_t>(0x88 ^ uid7[0] ^ uid7[1] ^ uid7[2]);
    const uint8_t bcc1 = static_cast<uint8_t>(uid7[3] ^ uid7[4] ^ uid7[5] ^ uid7[6]);

    constexpr int kPages = 45;
    uint8_t pages[kPages][4] = {};
    pages[0][0] = uid7[0];
    pages[0][1] = uid7[1];
    pages[0][2] = uid7[2];
    pages[0][3] = bcc0;
    pages[1][0] = uid7[3];
    pages[1][1] = uid7[4];
    pages[1][2] = uid7[5];
    pages[1][3] = uid7[6];
    pages[2][0] = bcc1;
    pages[2][1] = 0x48; // internal
    pages[2][2] = 0x00; // lock
    pages[2][3] = 0x00;
    pages[3][0] = 0xE1; // CC
    pages[3][1] = 0x10;
    pages[3][2] = 0x12; // NTAG213 size
    pages[3][3] = 0x00;
    // NTAG213 CFG0: AUTH0=0xFF so no page is password-protected.
    pages[41][0] = 0x04;
    pages[41][3] = 0xFF;
    for (size_t i = 0; i < payload.size(); ++i) {
        pages[4 + static_cast<int>(i / 4)][i % 4] = payload[i];
    }
    // NTAG213 GET_VERSION response.
    static const uint8_t kVersion[8] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};

    // Cap Passive Target (docs: Card Emulation NFC-A). Cap does NOT radiate —
    // Flipper must use NFC → Read with the Flipper flat on the Cap PCB NFC antenna
    // (not the RP-SMA rubber antenna). M5 Cap path: MODE 0xC8 → field → PTA → 0x88.
    if (!impl_->setup_listen_nfca(uid7, 7, atqa, sak, error)) {
        std::string stop_err;
        impl_->listen_stop(stop_err);
        return false;
    }

    // M5 ListenerST25R3916ForA state machine: Off → Idle → Ready → Active (→ Halt).
    enum class Ls { Off, Idle, Ready, Active, Halt };
    static const char* const kLsName[] = {"off", "idle", "ready", "active", "halt"};
    Ls st = Ls::Off;
    bool wakeup = false;
    bool had_hit = false;
    int reads = 0;
    std::string e;

    auto set_state = [&](Ls n) {
        if (n != st) trace("listen %s -> %s", kLsName[static_cast<int>(st)], kLsName[static_cast<int>(n)]);
        st = n;
    };
    auto field_present = [&]() {
        uint8_t aux = 0;
        return impl_->read_reg(kRegAuxDisplay, aux, e) && (aux & 0x40); // efd_o
    };
    auto goto_idle = [&]() {
        uint8_t op = 0;
        if (impl_->read_reg(kRegOpControl, op, e) && !(op & kOpEn)) {
            (void)impl_->modify_reg(kRegOpControl, 0x00, kOpEn | kOpRxEn, e);
            uint8_t aux = 0;
            for (int i = 0; i < 100; ++i) {
                if (impl_->read_reg(kRegAuxDisplay, aux, e) && (aux & 0x10)) break;
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        }
        (void)impl_->modify_reg(kRegAux, 0x00, kAuxNoCrcRx, e);
        if (st == Ls::Active && !wakeup) {
            (void)impl_->modify_reg(kRegPassiveTarget, 0x01, 0x00, e); // auto-AC on
            (void)impl_->direct_cmd(kCmdGotoSense, e);
        }
        (void)impl_->direct_cmd(kCmdClearFifo, e);
        (void)impl_->direct_cmd(kCmdUnmaskRx, e);
        wakeup = false;
        set_state(Ls::Idle);
    };
    auto goto_off = [&]() {
        impl_->listen_rearm_sense(e);
        set_state(Ls::Off);
        if (field_present()) {
            goto_idle();
        } else {
            (void)impl_->modify_reg(kRegOpControl, kOpEn | kOpRxEn | kOpTxEn, 0x00, e);
        }
    };
    auto goto_ready = [&]() {
        (void)impl_->modify_reg(kRegAux, kAuxNoCrcRx, 0x00, e);
        (void)impl_->write_reg(kRegBitrate, 0x00, e); // 106 kbps
        (void)impl_->modify_reg(kRegOpControl, 0x04, 0x00, e); // wu off
        (void)impl_->write_reg(kRegMode, kModeListenNfca, e);
        set_state(Ls::Ready);
    };
    auto goto_active = [&]() {
        (void)impl_->modify_reg(kRegPassiveTarget, 0x00, 0x01, e); // auto-AC off; host answers T2T
        (void)impl_->direct_cmd(kCmdClearFifo, e);                 // drop SELECT leftovers
        (void)impl_->direct_cmd(kCmdUnmaskRx, e);
        had_hit = true;
        set_state(Ls::Active);
    };
    auto goto_halt = [&]() {
        (void)impl_->modify_reg(kRegPassiveTarget, 0x01, 0x00, e); // auto-AC on
        (void)impl_->direct_cmd(kCmdGotoSleep, e);
        (void)impl_->write_reg(kRegMode, kModeBitrateDetect, e);
        (void)impl_->direct_cmd(kCmdUnmaskRx, e);
        set_state(Ls::Halt);
        if (!field_present()) goto_off();
    };

    // T2T command handler; returns false when the tag should leave Active.
    auto handle_cmd = [&](const uint8_t* rx, uint16_t n) -> bool {
        trace("listen rx %s", hex_bytes(rx, n, ' ').c_str());
        const uint8_t cmd = rx[0];
        if (cmd == 0x30 && n >= 2) { // READ → 4 pages, rolls over
            uint8_t resp[16];
            for (int i = 0; i < 4; ++i) std::memcpy(&resp[i * 4], pages[(rx[1] + i) % kPages], 4);
            (void)impl_->listen_respond(resp, sizeof(resp), e);
            ++reads;
        } else if (cmd == 0x3A && n >= 3 && rx[2] >= rx[1] && rx[2] < kPages) { // FAST_READ
            uint8_t resp[kPages * 4];
            uint16_t rl = 0;
            for (int p = rx[1]; p <= rx[2]; ++p, rl = static_cast<uint16_t>(rl + 4)) std::memcpy(&resp[rl], pages[p], 4);
            (void)impl_->listen_respond(resp, rl, e);
            ++reads;
        } else if (cmd == 0x60) { // GET_VERSION
            (void)impl_->listen_respond(kVersion, sizeof(kVersion), e);
        } else if (cmd == 0x3C) { // READ_SIG — iOS asks for it; no ECC signature
            static const uint8_t kSig[32] = {};
            (void)impl_->listen_respond(kSig, sizeof(kSig), e);
        } else if (cmd == 0x1B) { // PWD_AUTH — no password set
            static const uint8_t kPack[2] = {0x00, 0x00};
            (void)impl_->listen_respond(kPack, sizeof(kPack), e);
        } else if (cmd == 0x50) { // HALT
            goto_halt();
            return false;
        } else { // WRITE / READ_CNT / unknown → NAK (tag is read-only)
            (void)impl_->listen_nak(e);
        }
        return true;
    };

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    auto last_efd_poll = std::chrono::steady_clock::now();
    uint32_t last_traced = 0;
    int same_irq_count = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            break;
        }
        uint32_t irqs = impl_->read_irqs(e) | impl_->take_pending_irqs();
        e.clear();
        // IRQ pin path can miss I_eon on Cap; also poll the detector output.
        const auto now = std::chrono::steady_clock::now();
        if (st == Ls::Off && now - last_efd_poll > std::chrono::milliseconds(20)) {
            last_efd_poll = now;
            if (field_present()) irqs |= kIrqEon;
        }
        if (irqs == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        if (irqs == last_traced) {
            if (++same_irq_count == 1 || same_irq_count % 50 == 0) {
                trace("listen [%s] irqs=0x%08X (x%d) eon=%d eof=%d efd=%d", kLsName[static_cast<int>(st)], irqs,
                      same_irq_count, !!(irqs & kIrqEon), !!(irqs & kIrqEof), field_present());
            }
        } else {
            same_irq_count = 1;
            last_traced = irqs;
            trace("listen [%s] irqs=0x%08X eon=%d eof=%d nfct=%d rxe_pta=%d wu=%d rxe=%d efd=%d",
                  kLsName[static_cast<int>(st)], irqs, !!(irqs & kIrqEon), !!(irqs & kIrqEof), !!(irqs & kIrqNfct),
                  !!(irqs & kIrqRxePta), !!(irqs & (kIrqWuA | kIrqWuAx)), !!(irqs & kIrqRxe), field_present());
        }

        // eon/eof can land in the same read; trust the live detector output.
        const bool field_gone = (irqs & kIrqEof) && !field_present();

        if (st == Ls::Off) {
            if (!(irqs & kIrqEon) && !field_present()) continue;
            goto_idle();
        }
        if (field_gone) {
            goto_off();
            continue;
        }

        if (st == Ls::Idle || st == Ls::Halt) {
            if (irqs & kIrqRxe) { // non-AC frame while not selected: drop it
                (void)impl_->direct_cmd(kCmdClearFifo, e);
                (void)impl_->direct_cmd(kCmdUnmaskRx, e);
            }
            if (irqs & kIrqRxePta) {
                uint8_t pta = 0;
                if (impl_->read_reg(kRegPtaDisplay, pta, e)) {
                    const uint8_t s = pta & 0x0F;
                    trace("listen PTA state=0x%X", s);
                    if (st == Ls::Idle && s > 0x1) {
                        goto_ready();
                    } else if (st == Ls::Halt && s > 0x9) {
                        wakeup = true;
                        goto_ready();
                    }
                }
            }
        }
        if (st == Ls::Ready && (irqs & (kIrqWuA | kIrqWuAx))) {
            goto_active();
            continue; // SELECT's own rxe was handled by HW auto-AC
        }
        if (st == Ls::Active && (irqs & kIrqRxe)) {
            const uint8_t err = static_cast<uint8_t>((irqs >> 16) & 0xF0); // crc|par|err2|err1
            const uint16_t n = impl_->fifo_bytes(e);
            uint8_t rx[64] = {};
            const uint8_t take = static_cast<uint8_t>(std::min<uint16_t>(n, sizeof(rx)));
            (void)impl_->read_fifo(rx, take, e);
            if (err || n <= 2) {
                trace("listen rx error err=0x%02X n=%u %s", err, n, hex_bytes(rx, take, ' ').c_str());
                (void)impl_->direct_cmd(kCmdClearFifo, e);
                (void)impl_->direct_cmd(kCmdUnmaskRx, e);
                if (wakeup) goto_halt();
                else goto_idle();
                continue;
            }
            (void)handle_cmd(rx, static_cast<uint16_t>(take - 2)); // strip CRC
        }
    }

    impl_->listen_stop(error);
    if (cancel && *cancel && !had_hit) return false;
    if (had_hit || reads > 0) {
        error.clear();
        return true;
    }
    error = "listen timed out (no reader)";
    return false;
}

bool St25Session::iso15693_inventory(std::string& uid_hex, std::string& error, bool* cancel) {
    (void)uid_hex;
    (void)cancel;
    // Inventory without RFAL NFC-V is incomplete; keep honest.
    error = "ISO15693 inventory needs RFAL NFC-V on Cap ST25 (not fully ported).";
    return false;
}

bool St25Session::magic_probe(std::string& result, std::string& error, bool* cancel) {
    if (!impl_->anticoll_select(error, cancel)) return false;
    // Gen1 backdoor 0x40 (7-bit)
    const uint8_t cmd40 = 0x40;
    if (!impl_->tx_bits(&cmd40, 1, 7, false, error)) return false;
    std::vector<uint8_t> rx;
    if (impl_->wait_fifo(1, rx, error, cancel, 40) && !rx.empty() && rx[0] == 0x0A) {
        result = "Gen1 magic";
        error.clear();
        return true;
    }
    error.clear();
    if (impl_->sak == 0x00) {
        result = "Responded (unknown / possible Gen2)";
        return true;
    }
    result = "Not a known magic card (or probe unsupported).";
    return true;
}

bool St25Session::magic_clone_uid(const std::string& uid_hex, std::string& error, bool* cancel) {
    std::string probe;
    if (!magic_probe(probe, error, cancel)) return false;
    if (probe.find("Gen1") == std::string::npos && probe.find("unknown") == std::string::npos) {
        error = "Magic write not on this card.";
        return false;
    }
    // Best-effort Gen1 UID write (0x40/0x43 backdoor then write block 0) — simplified.
    const std::string h = tools::nfc_normalize_hex(uid_hex);
    if (h.size() < 8) {
        error = "UID too short";
        return false;
    }
    uint8_t block0[4];
    for (int i = 0; i < 4; ++i)
        block0[i] = static_cast<uint8_t>(std::strtoul(h.substr(static_cast<size_t>(i * 2), 2).c_str(),
                                                      nullptr, 16));
    const uint8_t cmd40 = 0x40;
    (void)impl_->tx_bits(&cmd40, 1, 7, false, error);
    std::vector<uint8_t> rx;
    (void)impl_->wait_fifo(1, rx, error, cancel, 40);
    const uint8_t cmd43 = 0x43;
    (void)impl_->tx_bits(&cmd43, 1, 8, false, error);
    (void)impl_->wait_fifo(1, rx, error, cancel, 40);
    error.clear();
    // WRITE 0xA0 block 0
    uint8_t wr[6] = {0xA0, 0x00, block0[0], block0[1], block0[2], block0[3]};
    if (!impl_->tx_bits(wr, 6, 0, true, error)) return false;
    (void)impl_->wait_fifo(1, rx, error, cancel, 50);
    error.clear();
    return true;
}

} // namespace platform

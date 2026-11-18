#include "cap_detect.h"

#include "cap_power.h"
#include "gpio_line.h"
#include "spi_device.h"
#include "st25r3916.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string config_dir() {
    if (const char* h = std::getenv("HOME")) {
        if (*h) return std::string(h) + "/.config/kvxputerzero";
    }
    return "/tmp/kvxputerzero";
}

std::string read_config_line(const char* name) {
    std::ifstream in(config_dir() + "/" + name);
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    return trim(line);
}

bool i2c_probe(int bus, uint8_t addr, std::string& path_out) {
    path_out = "/dev/i2c-" + std::to_string(bus);
    const int fd = ::open(path_out.c_str(), O_RDWR);
    if (fd < 0) return false;
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        ::close(fd);
        return false;
    }
    uint8_t b = 0;
    const ssize_t n = ::read(fd, &b, 1);
    ::close(fd);
    return n == 1;
}

CapInfo missing(CapKind kind, const char* name, const std::string& err) {
    CapInfo c;
    c.kind = kind;
    c.present = false;
    c.name = name;
    c.error = err.empty() ? "Cap not present" : err;
    return c;
}

} // namespace

std::string default_spidev_path() {
    // Cap radios sit on CE1; LCD (when exposed) is CE0.
    return default_cap_spidev_path();
}

std::string config_bus_path(const char* config_name, const std::string& fallback) {
    const std::string v = read_config_line(config_name);
    return v.empty() ? fallback : v;
}

CapInfo detect_cc1101(const std::string& spidev) {
    if (kDesktop) return missing(CapKind::Cc1101, "CC1101", "Cap not present");
    const std::string path = spidev.empty() ? config_bus_path("spi_cc1101.txt", default_spidev_path()) : spidev;
    std::string err;
    if (!enable_cap_power(CapKindHint::Cc1101, err)) {
        return missing(CapKind::Cc1101, "CC1101", err.empty() ? "Cap not present" : err);
    }
    // Keep Cap NFC_CS (GPIO22) high so ST25R3916 stays deselected on shared SPI.
    GpioLine nfc_cs("/dev/gpiochip0", 22);
    {
        std::string ign;
        (void)nfc_cs.request_output(true, ign);
    }
    SpiConfig cfg;
    cfg.path = path;
    cfg.speed_hz = 500000;
    auto spi = SpiDevice::open(cfg, err);
    if (!spi) {
        nfc_cs.release();
        return missing(CapKind::Cc1101, "CC1101", err.empty() ? "Cap not present" : err);
    }

    // Status regs (PARTNUM/VERSION …): header = addr | 0x40 | 0x80 (= 0xC0 | addr).
    // Config regs use addr | 0x80 only — see Cap-CC1101-SubG-Chat readRegRaw.
    auto read_status = [&](uint8_t reg, uint8_t& out) -> bool {
        uint8_t tx[2] = {static_cast<uint8_t>(0xC0 | (reg & 0x3F)), 0x00};
        uint8_t rx[2] = {0, 0};
        if (!spi->transfer(tx, rx, 2, err)) return false;
        out = rx[1];
        return true;
    };
    auto known_ver = [](uint8_t ver) {
        return ver == 0x04 || ver == 0x14 || ver == 0x03 || ver == 0x20 || ver == 0x3F || ver == 0x94 ||
               ver == 0xBF || ver == 0xFC;
    };
    uint8_t part = 0xFF;
    uint8_t ver = 0xFF;
    bool ok = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (!read_status(0x30, part) || !read_status(0x31, ver)) {
            nfc_cs.release();
            return missing(CapKind::Cc1101, "CC1101", err.empty() ? "Cap not present" : err);
        }
        if (known_ver(ver)) {
            ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!ok) {
        nfc_cs.release();
        char buf[48];
        std::snprintf(buf, sizeof(buf), "bad VERSION=0x%02X PART=0x%02X", ver, part);
        return missing(CapKind::Cc1101, "CC1101", buf);
    }
    CapInfo c;
    c.kind = CapKind::Cc1101;
    c.present = true;
    c.name = "CC1101";
    c.path = path;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "part=0x%02X ver=0x%02X", part, ver);
    c.detail = buf;
    nfc_cs.release();
    return c;
}

CapInfo detect_nrf24(const std::string& spidev) {
    if (kDesktop) return missing(CapKind::Nrf24, "nRF24", "Cap not present");
    const std::string path = spidev.empty() ? config_bus_path("spi_nrf24.txt", default_spidev_path()) : spidev;
    std::string err;
    SpiConfig cfg;
    cfg.path = path;
    cfg.speed_hz = 1000000;
    auto spi = SpiDevice::open(cfg, err);
    if (!spi) return missing(CapKind::Nrf24, "nRF24", err.empty() ? "Cap not present" : err);

    // R_REGISTER | SETUP_AW (0x03): expect 0x01..0x03 after power-on default 0x03
    uint8_t tx[2] = {0x03, 0xFF};
    uint8_t rx[2] = {0, 0};
    if (!spi->transfer(tx, rx, 2, err)) return missing(CapKind::Nrf24, "nRF24", err);
    const uint8_t aw = rx[1] & 0x03;
    // Also read FEATURE (0x1D) — NOP status in rx[0] should look like a status byte (not 0x00/0xFF always).
    if (aw == 0 || aw > 3) {
        // Try writing SETUP_AW=3 and reading back.
        uint8_t wtx[2] = {static_cast<uint8_t>(0x20 | 0x03), 0x03};
        uint8_t wrx[2] = {0, 0};
        if (!spi->transfer(wtx, wrx, 2, err)) return missing(CapKind::Nrf24, "nRF24", err);
        if (!spi->transfer(tx, rx, 2, err)) return missing(CapKind::Nrf24, "nRF24", err);
        if ((rx[1] & 0x03) != 0x03) return missing(CapKind::Nrf24, "nRF24", "Cap not present");
    }
    CapInfo c;
    c.kind = CapKind::Nrf24;
    c.present = true;
    c.name = "nRF24L01+";
    c.path = path;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "SETUP_AW=%u status=0x%02X", rx[1] & 0x03u, rx[0]);
    c.detail = buf;
    return c;
}

CapInfo detect_sx1262(const std::string& spidev) {
    if (kDesktop) return missing(CapKind::Sx1262, "SX1262", "Cap not present");
    const std::string path = spidev.empty() ? config_bus_path("spi_sx1262.txt", default_spidev_path()) : spidev;
    std::string err;
    if (!enable_cap_power(CapKindHint::Sx1262, err)) {
        return missing(CapKind::Sx1262, "SX1262", err.empty() ? "Cap not present" : err);
    }
    SpiConfig cfg;
    cfg.path = path;
    cfg.speed_hz = 1000000;
    auto spi = SpiDevice::open(cfg, err);
    if (!spi) return missing(CapKind::Sx1262, "SX1262", err.empty() ? "Cap not present" : err);

    (void)wait_sx1262_not_busy(200, err);
    err.clear();
    // GetStatus command 0xC0 — returns status on MISO during command byte clocking / next byte.
    uint8_t tx[2] = {0xC0, 0x00};
    uint8_t rx[2] = {0, 0};
    if (!spi->transfer(tx, rx, 2, err)) return missing(CapKind::Sx1262, "SX1262", err);
    // Status chipMode bits [6:4]: 0x2=STBY_RC, 0x3=STBY_XOSC, 0x4=FS, 0x5=RX, 0x6=TX — not 0.
    const uint8_t st = rx[0] ? rx[0] : rx[1];
    const uint8_t mode = (st >> 4) & 0x07;
    if (mode == 0 && st == 0x00) return missing(CapKind::Sx1262, "SX1262", "Cap not present");
    if (st == 0xFF) return missing(CapKind::Sx1262, "SX1262", "Cap not present");
    CapInfo c;
    c.kind = CapKind::Sx1262;
    c.present = true;
    c.name = "SX1262";
    c.path = path;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "status=0x%02X mode=%u", st, mode);
    c.detail = buf;
    return c;
}

CapInfo detect_si4713(int i2c_bus) {
    if (kDesktop) return missing(CapKind::Si4713, "Si4713", "Cap not present");
    if (i2c_bus < 0) {
        const std::string s = read_config_line("i2c_si4713.txt");
        i2c_bus = s.empty() ? 1 : std::atoi(s.c_str());
    }
    // Si4713 typically 0x63.
    std::string path;
    if (!i2c_probe(i2c_bus, 0x63, path)) return missing(CapKind::Si4713, "Si4713", "Cap not present");
    CapInfo c;
    c.kind = CapKind::Si4713;
    c.present = true;
    c.name = "Si4713";
    c.path = path;
    c.detail = "addr=0x63";
    return c;
}

CapInfo detect_pn532(int i2c_bus) {
    if (kDesktop) return missing(CapKind::Pn532, "PN532", "Cap not present");
    if (i2c_bus < 0) {
        const std::string s = read_config_line("i2c_pn532.txt");
        i2c_bus = s.empty() ? 1 : std::atoi(s.c_str());
    }
    std::string path;
    // PN532 I2C often 0x24.
    if (!i2c_probe(i2c_bus, 0x24, path)) return missing(CapKind::Pn532, "PN532", "Cap not present");
    CapInfo c;
    c.kind = CapKind::Pn532;
    c.present = true;
    c.name = "PN532";
    c.path = path;
    c.detail = "addr=0x24";
    return c;
}

std::vector<CapInfo> detect_all_caps() {
    // ST25 before SX1262: both want GPIO22 (NFC_CS vs LoRa BUSY) on different Caps.
    std::vector<CapInfo> out;
    out.push_back(detect_cc1101());
    out.push_back(detect_nrf24());
    out.push_back(detect_st25r3916());
    release_cap_power(true); // free GPIO22 before LoRa BUSY probe
    out.push_back(detect_sx1262());
    out.push_back(detect_si4713());
    out.push_back(detect_pn532());
    return out;
}

} // namespace platform

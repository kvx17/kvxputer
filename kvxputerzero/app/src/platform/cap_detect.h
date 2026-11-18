#pragma once

#include <string>
#include <vector>

namespace platform {

enum class CapKind {
    Cc1101,
    Nrf24,
    Sx1262,
    Si4713,
    Pn532,
    St25r3916,
    Unknown,
};

struct CapInfo {
    CapKind kind{CapKind::Unknown};
    bool present{false};
    std::string name;
    std::string path; // spidev, i2c bus path, or detail
    std::string detail;
    std::string error;
};

// Config overrides (optional files under ~/.config/kvxputerzero/):
//   spi_cc1101.txt, spi_nrf24.txt, spi_sx1262.txt  -> /dev/spidevX.Y (default Cap CE1 = spidev0.1)
//   i2c_si4713.txt -> bus number, i2c_pn532.txt -> bus number
// Detect/open enables Cap power (ext_5v_out + GPIO26) before SPI probe.
std::string default_spidev_path();
std::string config_bus_path(const char* config_name, const std::string& fallback);

CapInfo detect_cc1101(const std::string& spidev = {});
CapInfo detect_nrf24(const std::string& spidev = {});
CapInfo detect_sx1262(const std::string& spidev = {});
CapInfo detect_si4713(int i2c_bus = -1);
CapInfo detect_pn532(int i2c_bus = -1);
CapInfo detect_st25r3916(const std::string& spidev = {}); // Cap-CC1101 NFC (GPIO22 soft-CS)

std::vector<CapInfo> detect_all_caps();

} // namespace platform

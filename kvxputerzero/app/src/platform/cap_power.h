#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace platform {

enum class CapKindHint {
    Generic,
    Cc1101,
    Sx1262,
};

struct CapPowerStatus {
    bool ext5v_on{false};
    bool gpio26_high{false};
    bool pi4ioe_ant_on{false};
    std::string spidev; // preferred Cap path
    std::vector<std::string> lines;
};

// Prefer Cap CE1. Falls back to first listed spidev, then /dev/spidev0.1.
std::string default_cap_spidev_path();

// Enable Cap I/O supply: /sys/class/leds/ext_5v_out + GPIO26 POWER_EN/RST.
// For Cc1101 also pinmuxes G14/G15 when pinctrl exists and drives RF_SW0 high (868/915).
// For Sx1262 also pulses RST, enables PI4IOE5V6408 P0 antenna switch on I2C1@0x43.
// Never turns EXT5V off on release (shared with LCD SPI).
bool enable_cap_power(CapKindHint kind, std::string& error, std::atomic<bool>* cancel = nullptr);
void release_cap_power(bool preserve_ext5v = true);

CapPowerStatus query_cap_power_status();

// Wait until SX1262 BUSY (GPIO22) is low, or timeout.
bool wait_sx1262_not_busy(int timeout_ms, std::string& error, std::atomic<bool>* cancel = nullptr);

} // namespace platform

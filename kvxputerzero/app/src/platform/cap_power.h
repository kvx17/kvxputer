#pragma once

#include "gpio_line.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

enum class CapKindHint {
    Generic,
    Cc1101,
    Sx1262,
    Gps, // Cap LoRa-1262 GNSS: EXT5V (+ POWER_EN high). No SPI/RST. Grove is separate.
};

// Grove HY2.0-4P: UART mode (grove_fun=0) + grove_5v_out. Needed for Unit GPS v1.1.
bool enable_grove_uart_power(std::string& error);
void release_grove_uart_power(bool preserve_5v = true);

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
// For Gps: Cap EXT5V only (Unit GPS v1.1 uses enable_grove_uart_power).
// For Cc1101 also pinmuxes G14/G15 when pinctrl exists and drives RF_SW0 (band path).
// For Sx1262 also pulses RST, enables PI4IOE5V6408 P0 antenna switch on I2C1@0x43.
// Never turns EXT5V off on release (shared with LCD SPI).
bool enable_cap_power(CapKindHint kind, std::string& error, std::atomic<bool>* cancel = nullptr);
void release_cap_power(bool preserve_ext5v = true);

// Cap RF_SW0 (host GPIO14). RF_SW1 is driven via CC1101 GDO2 (IOCFG2) by the session.
// Returns the SW0/SW1 levels that match the Cap SP3T truth table for hz.
bool set_cc1101_rf_path(uint32_t hz, std::string& error);
void cc1101_rf_switch_levels(uint32_t hz, bool& sw0_high, bool& sw1_high);
// IOCFG2 value: constant-0 (0x2F) or inverted (0x6F) so GDO2 drives RF_SW1.
uint8_t cc1101_iocfg2_for_sw1(bool sw1_high);

bool read_cc1101_gdo0(bool& high, std::string& error);
// Re-request GDO0 as rising/falling edge event source (for RAW capture).
bool request_cc1101_gdo0_edges(std::string& error);
bool read_cc1101_gdo0_edges(std::vector<GpioEdgeEvent>& out, std::string& error);
// Drive GDO0 as async-serial TX data (exclusive with edge/input request).
bool request_cc1101_gdo0_output(bool initial_high, std::string& error);
bool set_cc1101_gdo0(bool high, std::string& error);
void release_cc1101_gdo0();
int cc1101_gdo0_fd(); // line request fd, or -1

CapPowerStatus query_cap_power_status();

// Wait until SX1262 BUSY (GPIO22) is low, or timeout.
bool wait_sx1262_not_busy(int timeout_ms, std::string& error, std::atomic<bool>* cancel = nullptr);

} // namespace platform

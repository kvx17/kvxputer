#pragma once

#include <cstddef>
#include <cstdint>

namespace model {

enum class ChannelKind {
    ToolsFill,      // has Wave 1 tools
    ConfigFill,     // theme colors
    ClockFill,      // Timer for now
    ComingSoon,     // later wave
    UseStock,       // OS already covers
};

struct ChannelDesc {
    const char* id;
    const char* label;
    const char* icon; // Phosphor Fill UTF-8 glyph (see view/ui_const.h)
    ChannelKind kind;
    const char* placeholder; // short message for UseStock / ComingSoon
};

inline constexpr ChannelDesc kChannels[] = {
    {"wifi", "WiFi", "\uE4EA", ChannelKind::ComingSoon, "WiFi tools in a later plan"},
    {"discovery", "Discovery", "\uE3D8", ChannelKind::ComingSoon, "Discovery tools in a later plan"},
    {"netops", "NetOps", "\uE344", ChannelKind::ComingSoon, "NetOps tools in a later plan"},
    {"ble", "BLE", "\uE0E8", ChannelKind::ComingSoon, "BLE tools in a later plan"},
    {"rf", "RF", "\uE0F6", ChannelKind::UseStock, "Use Cap-CC1101 stock apps"},
    {"nrf24", "NRF24", "\uE038", ChannelKind::ComingSoon, "Needs NRF24 accessory"},
    {"lora", "LoRa", "\uE3F6", ChannelKind::UseStock, "Use Cap-LoRa-1262 stock apps"},
    {"fm", "FM", "\uE3DA", ChannelKind::ComingSoon, "Needs SI4713 accessory"},
    {"infrared", "Infrared", "\uE2D6", ChannelKind::ComingSoon, "IR extras later; use IR-Remote"},
    {"ethernet", "Ethernet", "\uEDDE", ChannelKind::ComingSoon, "NIC tools in a later plan"},
    {"usb", "USB", "\uE4D0", ChannelKind::ComingSoon, "Most USB paths are ESP-only"},
    {"gps", "GPS", "\uE310", ChannelKind::ComingSoon, "Use Cap GPS; wardrive tools later"},
    {"rfid", "RFID", "\uE48A", ChannelKind::UseStock, "Use stock RFID app"},
    {"nfc", "NFC", "\uE346", ChannelKind::UseStock, "Use Cap-CC1101-NFC / RFID"},
    {"files", "Files", "\uE242", ChannelKind::UseStock, "Use stock Files app"},
    {"scripts", "Scripts", "\uE4B0", ChannelKind::ComingSoon, "Scripts runner later"},
    {"clock", "Clock", "\uE0FE", ChannelKind::ClockFill, nullptr},
    {"others", "Tools", "\uE51A", ChannelKind::ToolsFill, nullptr},
    {"modules", "Modules", "\uE3C2", ChannelKind::ComingSoon, "Grove modules later"},
    {"config", "Config", "\uE28A", ChannelKind::ConfigFill, nullptr},
};

inline constexpr size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);

// RGB888 defaults matching Cardputer kvxputer (RGB565 0x9818 / 0x07E0).
inline constexpr uint32_t kDefaultPriColor = 0x9900CC; // approx of 0x9818 purple
inline constexpr uint32_t kDefaultSecColor = 0x00E000; // approx of 0x07E0 green

// Exact RGB565-derived:
// 0x9818 -> R=0x13<<3=0x98, G=0x00<<2=0x00, B=0x18<<3=0xC0 -> 0x9800C0
// 0x07E0 -> R=0, G=0x3F<<2=0xFC, B=0 -> 0x00FC00
inline constexpr uint32_t kPriFrom565 = 0x9800C0;
inline constexpr uint32_t kSecFrom565 = 0x00FC00;

} // namespace model

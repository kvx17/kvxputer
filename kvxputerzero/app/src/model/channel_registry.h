#pragma once

#include <cstddef>
#include <cstdint>

namespace model {

enum class ChannelKind {
    ToolsFill,      // has Wave 1 tools (+ leftovers)
    ConfigFill,     // theme colors
    ClockFill,      // live clock face (+ Timer submenu)
    ChargeFill,     // charge / battery face
    CameraFill,     // live camera preview
    GalleryFill,    // Pictures browser
    ChannelFill,    // tool_registry menu for this channel id
    DiscoveryFill,  // discovery tools (BLE scan, walls, spectrum, wardrive)
    ComingSoon,     // later wave
    UseStock,       // OS already covers
};

struct ChannelDesc {
    const char* id;
    const char* label;
    ChannelKind kind;
    const char* placeholder; // short message for UseStock / ComingSoon
};

inline constexpr ChannelDesc kChannels[] = {
    {"wifi", "WiFi", ChannelKind::ChannelFill, nullptr},
    {"discovery", "Discovery", ChannelKind::DiscoveryFill, nullptr},
    {"netops", "NetOps", ChannelKind::ChannelFill, nullptr},
    {"ble", "BLE", ChannelKind::ChannelFill, nullptr},
    {"rf", "RF", ChannelKind::ChannelFill, nullptr},
    {"nrf24", "NRF24", ChannelKind::ChannelFill, nullptr},
    {"lora", "LoRa", ChannelKind::ChannelFill, nullptr},
    {"fm", "FM", ChannelKind::ChannelFill, nullptr},
    {"infrared", "Infrared", ChannelKind::ChannelFill, nullptr},
    {"ethernet", "Ethernet", ChannelKind::ChannelFill, nullptr},
    {"usb", "USB", ChannelKind::ChannelFill, nullptr},
    {"gps", "GPS", ChannelKind::ChannelFill, "Live wardrive needs the GPS cap."},
    {"rfid", "RFID", ChannelKind::ChannelFill, nullptr},
    {"nfc", "NFC", ChannelKind::ChannelFill, nullptr},
    {"files", "Files", ChannelKind::ChannelFill, nullptr},
    {"camera", "Camera", ChannelKind::CameraFill, nullptr},
    {"gallery", "Gallery", ChannelKind::GalleryFill, nullptr},
    {"scripts", "Scripts", ChannelKind::ChannelFill, nullptr},
    {"pcconnect", "PC Connect", ChannelKind::ChannelFill, nullptr},
    {"clock", "Clock", ChannelKind::ClockFill, nullptr},
    {"charge", "Charge", ChannelKind::ChargeFill, nullptr},
    {"others", "Tools", ChannelKind::ToolsFill, nullptr},
    {"modules", "Modules", ChannelKind::ChannelFill, nullptr},
    {"config", "Config", ChannelKind::ConfigFill, nullptr},
};

inline constexpr size_t kChannelCount = sizeof(kChannels) / sizeof(kChannels[0]);

// Exact RGB565-derived swatches (kvxputer DEFAULT_PRICOLOR / DEFAULT_SECCOLOR / KVX_DEFAULT_BGCOLOR):
// 0x9818 -> R=0x13<<3=0x98, G=0x00<<2=0x00, B=0x18<<3=0xC0 -> 0x9800C0
// 0x07E0 -> R=0, G=0x3F<<2=0xFC, B=0 -> 0x00FC00
// 0x0000 -> black
inline constexpr uint32_t kPriFrom565 = 0x9800C0; // purple (picker swatch)
inline constexpr uint32_t kSecFrom565 = 0x00FC00; // green accent default
inline constexpr uint32_t kBgDefault  = 0x000000; // black background default
// Main UI color default for kvxputerzero (user-editable; Reset colors restores this).
inline constexpr uint32_t kPriDefault = 0xFFFFFF;

} // namespace model

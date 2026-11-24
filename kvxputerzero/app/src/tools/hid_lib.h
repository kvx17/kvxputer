#pragma once

// kvxkeyboard HID — transport-independent pieces shared by the app, the USB
// gadget helper, and the BLE HID-over-GATT service. Port of the ESP32
// `menu/ble/hid_remote` app: same modes, shortcuts, and setting ranges.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tools::hid {

constexpr const char* kAppName = "kvxkeyboard HID";
constexpr const char* kDefaultBleName = "kvxKeyboard";
constexpr int kHostSlotCount = 6;

// One HID interface with three report IDs. The USB gadget and the BLE Report
// Map use the exact same descriptor so hosts see the same device either way.
constexpr uint8_t kReportKeyboard = 1; // [mods, reserved, k1..k6]
constexpr uint8_t kReportConsumer = 2; // [usage lo, usage hi]
constexpr uint8_t kReportMouse = 3;    // [buttons, dx, dy, wheel]
constexpr size_t kKeyboardReportLen = 8;
constexpr size_t kConsumerReportLen = 2;
constexpr size_t kMouseReportLen = 4;
// Largest report including the leading report ID (USB hidg report_length).
constexpr size_t kMaxReportLen = 1 + kKeyboardReportLen;

const std::vector<uint8_t>& report_descriptor();

// Keyboard modifier bits (byte 0 of the keyboard report).
constexpr uint8_t kModCtrl = 0x01;
constexpr uint8_t kModShift = 0x02;
constexpr uint8_t kModAlt = 0x04;
constexpr uint8_t kModGui = 0x08;

// Keyboard/Keypad usage page (0x07).
namespace key {
constexpr uint8_t A = 0x04;
constexpr uint8_t Enter = 0x28;
constexpr uint8_t Esc = 0x29;
constexpr uint8_t Backspace = 0x2A;
constexpr uint8_t Tab = 0x2B;
constexpr uint8_t Space = 0x2C;
constexpr uint8_t CapsLock = 0x39;
constexpr uint8_t F1 = 0x3A; // F1..F12 = 0x3A..0x45
constexpr uint8_t PrintScreen = 0x46;
constexpr uint8_t ScrollLock = 0x47;
constexpr uint8_t Pause = 0x48;
constexpr uint8_t Insert = 0x49;
constexpr uint8_t Home = 0x4A;
constexpr uint8_t PageUp = 0x4B;
constexpr uint8_t Delete = 0x4C;
constexpr uint8_t End = 0x4D;
constexpr uint8_t PageDown = 0x4E;
constexpr uint8_t Right = 0x4F;
constexpr uint8_t Left = 0x50;
constexpr uint8_t Down = 0x51;
constexpr uint8_t Up = 0x52;
constexpr uint8_t NumLock = 0x53;
constexpr uint8_t Menu = 0x65; // Application / context menu
constexpr uint8_t F13 = 0x68;
constexpr uint8_t function(int n) { return static_cast<uint8_t>(F1 + (n - 1)); } // n = 1..12
} // namespace key

// Consumer page (0x0C).
namespace consumer {
constexpr uint16_t NextTrack = 0x00B5;
constexpr uint16_t PrevTrack = 0x00B6;
constexpr uint16_t Stop = 0x00B7;
constexpr uint16_t PlayPause = 0x00CD;
constexpr uint16_t Mute = 0x00E2;
constexpr uint16_t VolumeUp = 0x00E9;
constexpr uint16_t VolumeDown = 0x00EA;
} // namespace consumer

// Mouse buttons.
constexpr uint8_t kMouseLeft = 0x01;
constexpr uint8_t kMouseRight = 0x02;
constexpr uint8_t kMouseMiddle = 0x04;

struct Stroke {
    uint8_t mods{0};
    uint8_t usage{0};
};

// US layout: printable ASCII (and '\n', '\t', '\b') to usage + Shift.
bool ascii_stroke(char c, Stroke& out);
// Lowercase letter / digit keycap to its usage, no modifiers. 0 when unknown.
uint8_t base_usage(char base);

// Human tag for the typed-text mirror ("[F5]", "[Ctrl+C]" …).
std::string stroke_tag(const Stroke& s);

// --- Modes ----------------------------------------------------------------------

enum class Mode : int {
    Presenter = 0,
    PresenterVertical,
    Keyboard,
    Media,
    AppleMusic,
    Movie,
    Mouse,
    Shorts,
    Clicker,
    Jiggler,
    StealthJiggler,
    PushToTalk,
    Shortcuts,
    Count,
};
constexpr int kModeCount = static_cast<int>(Mode::Count);
const char* mode_label(Mode m);

// --- System shortcuts (same list and order as the ESP32 app) ---------------------

struct Shortcut {
    const char* label;
    uint8_t mods;
    uint8_t usage;
};
const std::vector<Shortcut>& shortcuts();

// --- Push-to-talk presets ---------------------------------------------------------

struct PttPreset {
    const char* label;
    uint8_t mods;
    uint8_t usage;
};
constexpr int kPttPresetCount = 5;
const PttPreset& ptt_preset(int index);

// --- Settings ---------------------------------------------------------------------

enum class TransportPref : int { Usb = 0, Ble = 1, Sim = 2 };

struct Settings {
    TransportPref transport{TransportPref::Usb};
    std::string ble_name{kDefaultBleName};
    int last_mode{0};
    int mouse_sensitivity{5};   // 1..10
    int jiggler_interval{30};   // 5..600 s
    int jiggler_amount{2};      // 1..50 px
    int jiggler_dir{0};         // 0 left-right, 1 up-down, 2 random
    int stealth_min{45};        // 10..890 s
    int stealth_max{120};       // min+10..900 s
    int clicker_delay{100};     // 10..10000 ms
    int clicker_button{0};      // 0 left, 1 right, 2 middle
    int ptt_preset{0};          // 0..kPttPresetCount-1
    uint32_t shorts_up{0};      // LV key code; 0 = Up arrow
    uint32_t shorts_down{0};    // LV key code; 0 = Down arrow
    bool alt_is_modifier{false};
    // BLE host slots 1..6 (index 0..5): BlueZ device addresses.
    std::string host_slots[kHostSlotCount];
    std::string preferred_host;

    void clamp();
    static Settings load();
    void save() const;
};

const char* jiggler_dir_label(int dir);
const char* clicker_button_label(int b);
uint8_t clicker_button_mask(int b);

// Jiggler step. Normal jiggler returns the outward step (caller moves back).
// `rnd(lo, hi)` returns an int in [lo, hi].
template <typename Rng>
void jiggler_delta(int amount, int dir, bool stealth, Rng rnd, int8_t& dx, int8_t& dy) {
    if (amount < 1) amount = 1;
    if (amount > 50) amount = 50;
    dx = 0;
    dy = 0;
    if (stealth) {
        if (dir == 0) {
            dx = static_cast<int8_t>(rnd(-amount, amount));
            if (dx == 0) dx = 1;
        } else if (dir == 1) {
            dy = static_cast<int8_t>(rnd(-amount, amount));
            if (dy == 0) dy = 1;
        } else {
            dx = static_cast<int8_t>(rnd(-amount, amount));
            dy = static_cast<int8_t>(rnd(-amount, amount));
            if (dx == 0 && dy == 0) dx = 1;
        }
        return;
    }
    if (dir == 0) dx = static_cast<int8_t>(amount);
    else if (dir == 1) dy = static_cast<int8_t>(amount);
    else if (rnd(0, 1) == 0) dx = static_cast<int8_t>(rnd(0, 1) ? amount : -amount);
    else dy = static_cast<int8_t>(rnd(0, 1) ? amount : -amount);
}

} // namespace tools::hid

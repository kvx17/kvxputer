#include "hid_lib.h"

#include "user_paths.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tools::hid {

const std::vector<uint8_t>& report_descriptor() {
    static const std::vector<uint8_t> desc = {
        // Keyboard, report ID 1: modifiers, reserved, LED output, 6 keys.
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, kReportKeyboard,
        0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
        0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
        0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
        0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
        0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
        0xC0,
        // Consumer control, report ID 2: one 16-bit usage.
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, kReportConsumer,
        0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
        0xC0,
        // Mouse, report ID 3: 5 buttons, X, Y, wheel.
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, kReportMouse,
        0x09, 0x01, 0xA1, 0x00,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
        0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
        0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
        0x81, 0x06,
        0xC0, 0xC0,
    };
    return desc;
}

uint8_t base_usage(char base) {
    if (base >= 'a' && base <= 'z') return static_cast<uint8_t>(key::A + (base - 'a'));
    if (base >= 'A' && base <= 'Z') return static_cast<uint8_t>(key::A + (base - 'A'));
    if (base >= '1' && base <= '9') return static_cast<uint8_t>(0x1E + (base - '1'));
    if (base == '0') return 0x27;
    return 0;
}

bool ascii_stroke(char c, Stroke& out) {
    out = {};
    if (c >= 'a' && c <= 'z') {
        out.usage = base_usage(c);
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        out.usage = base_usage(c);
        out.mods = kModShift;
        return true;
    }
    if (c >= '0' && c <= '9') {
        out.usage = base_usage(c);
        return true;
    }
    struct Map {
        char c;
        uint8_t usage;
        bool shift;
    };
    static const Map kMap[] = {
        {'\n', key::Enter, false}, {'\r', key::Enter, false}, {'\t', key::Tab, false},
        {'\b', key::Backspace, false}, {' ', key::Space, false},
        {'!', 0x1E, true}, {'@', 0x1F, true}, {'#', 0x20, true}, {'$', 0x21, true},
        {'%', 0x22, true}, {'^', 0x23, true}, {'&', 0x24, true}, {'*', 0x25, true},
        {'(', 0x26, true}, {')', 0x27, true},
        {'-', 0x2D, false}, {'_', 0x2D, true}, {'=', 0x2E, false}, {'+', 0x2E, true},
        {'[', 0x2F, false}, {'{', 0x2F, true}, {']', 0x30, false}, {'}', 0x30, true},
        {'\\', 0x31, false}, {'|', 0x31, true}, {';', 0x33, false}, {':', 0x33, true},
        {'\'', 0x34, false}, {'"', 0x34, true}, {'`', 0x35, false}, {'~', 0x35, true},
        {',', 0x36, false}, {'<', 0x36, true}, {'.', 0x37, false}, {'>', 0x37, true},
        {'/', 0x38, false}, {'?', 0x38, true},
    };
    for (const auto& m : kMap) {
        if (m.c == c) {
            out.usage = m.usage;
            out.mods = m.shift ? kModShift : 0;
            return true;
        }
    }
    return false;
}

namespace {

const char* usage_name(uint8_t u) {
    switch (u) {
    case key::Enter: return "Enter";
    case key::Esc: return "Esc";
    case key::Backspace: return "Bksp";
    case key::Tab: return "Tab";
    case key::Space: return "Space";
    case key::PrintScreen: return "PrtSc";
    case key::ScrollLock: return "ScrLk";
    case key::Pause: return "Pause";
    case key::Insert: return "Ins";
    case key::Home: return "Home";
    case key::PageUp: return "PgUp";
    case key::Delete: return "Del";
    case key::End: return "End";
    case key::PageDown: return "PgDn";
    case key::Right: return "Right";
    case key::Left: return "Left";
    case key::Down: return "Down";
    case key::Up: return "Up";
    case key::NumLock: return "NumLk";
    case key::Menu: return "Menu";
    case key::F13: return "F13";
    default: return nullptr;
    }
}

} // namespace

std::string stroke_tag(const Stroke& s) {
    std::string out = "[";
    if (s.mods & kModCtrl) out += "Ctrl+";
    if (s.mods & kModAlt) out += "Alt+";
    if (s.mods & kModGui) out += "Win+";
    if ((s.mods & kModShift) && (s.mods & (kModCtrl | kModAlt | kModGui))) out += "Shift+";
    if (const char* n = usage_name(s.usage)) {
        out += n;
    } else if (s.usage >= key::F1 && s.usage <= key::function(12)) {
        out += "F" + std::to_string(s.usage - key::F1 + 1);
    } else if (s.usage >= key::A && s.usage <= key::A + 25) {
        out += static_cast<char>('A' + (s.usage - key::A));
    } else if (s.usage >= 0x1E && s.usage <= 0x26) {
        out += static_cast<char>('1' + (s.usage - 0x1E));
    } else if (s.usage == 0x27) {
        out += '0';
    } else {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "0x%02X", s.usage);
        out += buf;
    }
    out += "]";
    return out;
}

const char* mode_label(Mode m) {
    static const char* const kLabels[kModeCount] = {
        "Presenter",     "Presenter Vertical", "Keyboard",       "Media",        "Apple Music",
        "Movie",         "Mouse",              "Shorts",         "Mouse Clicker", "Mouse Jiggler",
        "Stealth Jiggler", "Push-to-Talk",     "System Shortcuts",
    };
    const int i = static_cast<int>(m);
    if (i < 0 || i >= kModeCount) return kLabels[0];
    return kLabels[i];
}

const std::vector<Shortcut>& shortcuts() {
    constexpr uint8_t C = kModCtrl, S = kModShift, A = kModAlt, G = kModGui;
    auto k = [](char c) { return base_usage(c); };
    static const std::vector<Shortcut> list = {
        {"Alt+Tab", A, key::Tab},
        {"Win/Cmd+Tab", G, key::Tab},
        {"Ctrl+Shift+Esc", static_cast<uint8_t>(C | S), key::Esc},
        {"Ctrl+Alt+Del", static_cast<uint8_t>(C | A), key::Delete},
        {"Alt+F4 close", A, key::function(4)},
        {"Ctrl+Alt+Shift+V", static_cast<uint8_t>(C | A | S), k('v')},
        {"Ctrl+C copy", C, k('c')},
        {"Ctrl+V paste", C, k('v')},
        {"Ctrl+X cut", C, k('x')},
        {"Ctrl+A select all", C, k('a')},
        {"Ctrl+Z undo", C, k('z')},
        {"Ctrl+Y redo", C, k('y')},
        {"Ctrl+S save", C, k('s')},
        {"Ctrl+F find", C, k('f')},
        {"Win/Cmd+D desktop", G, k('d')},
        {"Win/Cmd+L lock", G, k('l')},
        {"Win/Cmd+E files", G, k('e')},
        {"Win/Cmd+R run", G, k('r')},
        {"Win/Cmd+V clipboard", G, k('v')},
        {"Win/Cmd+Shift+S snip", static_cast<uint8_t>(G | S), k('s')},
        {"Print Screen", 0, key::PrintScreen},
        {"Alt+PrtSc", A, key::PrintScreen},
        {"Ctrl+T new tab", C, k('t')},
        {"Ctrl+W close tab", C, k('w')},
        {"Ctrl+Shift+T reopen", static_cast<uint8_t>(C | S), k('t')},
        {"Ctrl+L address", C, k('l')},
        {"Alt+Left back", A, key::Left},
        {"Alt+Right forward", A, key::Right},
        {"Ctrl+Alt+T terminal", static_cast<uint8_t>(C | A), k('t')},
        {"Cmd/Ctrl+Q quit", G, k('q')},
        {"Cmd/Ctrl+Space spotlight", G, key::Space},
        {"Cmd+Opt+Esc force quit", static_cast<uint8_t>(G | A), key::Esc},
        // Keyboard-mode FN overlay keys the Zero Fn layer does not print.
        {"Pause / Break", 0, key::Pause},
        {"Num Lock", 0, key::NumLock},
        {"Scroll Lock", 0, key::ScrollLock},
        {"Menu (App) key", 0, key::Menu},
        {"Caps Lock", 0, key::CapsLock},
        {"Win/Cmd key", G, 0},
    };
    return list;
}

const PttPreset& ptt_preset(int index) {
    static const PttPreset kPresets[kPttPresetCount] = {
        {"Ctrl+Shift+M (Teams)", static_cast<uint8_t>(kModCtrl | kModShift), base_usage('m')},
        {"Space (Zoom hold)", 0, key::Space},
        {"Ctrl+D (Meet)", kModCtrl, base_usage('d')},
        {"F13 (custom bind)", 0, key::F13},
        {"Cmd+Shift+A (Zoom Mac)", static_cast<uint8_t>(kModGui | kModShift), base_usage('a')},
    };
    if (index < 0 || index >= kPttPresetCount) index = 0;
    return kPresets[index];
}

const char* jiggler_dir_label(int dir) {
    if (dir == 1) return "Up-Down";
    if (dir == 2) return "Random";
    return "Left-Right";
}

const char* clicker_button_label(int b) {
    if (b == 1) return "Right";
    if (b == 2) return "Middle";
    return "Left";
}

uint8_t clicker_button_mask(int b) {
    if (b == 1) return kMouseRight;
    if (b == 2) return kMouseMiddle;
    return kMouseLeft;
}

void Settings::clamp() {
    auto cl = [](int v, int lo, int hi) { return std::clamp(v, lo, hi); };
    const int t = static_cast<int>(transport);
    transport = static_cast<TransportPref>(cl(t, 0, 2));
    if (ble_name.empty()) ble_name = kDefaultBleName;
    if (ble_name.size() > 20) ble_name.resize(20);
    last_mode = cl(last_mode, 0, kModeCount - 1);
    mouse_sensitivity = cl(mouse_sensitivity, 1, 10);
    jiggler_interval = cl(jiggler_interval, 5, 600);
    jiggler_amount = cl(jiggler_amount, 1, 50);
    jiggler_dir = ((jiggler_dir % 3) + 3) % 3;
    stealth_min = cl(stealth_min, 10, 890);
    stealth_max = cl(stealth_max, stealth_min + 10, 900);
    clicker_delay = cl(clicker_delay, 10, 10000);
    clicker_button = cl(clicker_button, 0, 2);
    ptt_preset = cl(ptt_preset, 0, kPttPresetCount - 1);
}

namespace {

std::string settings_path() {
    return tools::config_file("hid_remote.txt");
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

int to_int(const std::string& v, int fallback) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return fallback;
    return static_cast<int>(n);
}

} // namespace

Settings Settings::load() {
    Settings s;
    std::ifstream in(settings_path());
    std::string line;
    while (in && std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if (k == "transport") s.transport = static_cast<TransportPref>(to_int(v, 0));
        else if (k == "ble_name") s.ble_name = v;
        else if (k == "last_mode") s.last_mode = to_int(v, 0);
        else if (k == "mouse_sensitivity") s.mouse_sensitivity = to_int(v, 5);
        else if (k == "jiggler_interval") s.jiggler_interval = to_int(v, 30);
        else if (k == "jiggler_amount") s.jiggler_amount = to_int(v, 2);
        else if (k == "jiggler_dir") s.jiggler_dir = to_int(v, 0);
        else if (k == "stealth_min") s.stealth_min = to_int(v, 45);
        else if (k == "stealth_max") s.stealth_max = to_int(v, 120);
        else if (k == "clicker_delay") s.clicker_delay = to_int(v, 100);
        else if (k == "clicker_button") s.clicker_button = to_int(v, 0);
        else if (k == "ptt_preset") s.ptt_preset = to_int(v, 0);
        else if (k == "shorts_up") s.shorts_up = static_cast<uint32_t>(to_int(v, 0));
        else if (k == "shorts_down") s.shorts_down = static_cast<uint32_t>(to_int(v, 0));
        else if (k == "alt_is_modifier") s.alt_is_modifier = to_int(v, 0) != 0;
        else if (k == "preferred_host") s.preferred_host = v;
        else if (k.size() == 5 && k.compare(0, 4, "slot") == 0 && k[4] >= '1' &&
                 k[4] < '1' + kHostSlotCount) {
            s.host_slots[k[4] - '1'] = v;
        }
    }
    s.clamp();
    return s;
}

void Settings::save() const {
    Settings c = *this;
    c.clamp();
    std::ostringstream o;
    o << "transport=" << static_cast<int>(c.transport) << "\n"
      << "ble_name=" << c.ble_name << "\n"
      << "last_mode=" << c.last_mode << "\n"
      << "mouse_sensitivity=" << c.mouse_sensitivity << "\n"
      << "jiggler_interval=" << c.jiggler_interval << "\n"
      << "jiggler_amount=" << c.jiggler_amount << "\n"
      << "jiggler_dir=" << c.jiggler_dir << "\n"
      << "stealth_min=" << c.stealth_min << "\n"
      << "stealth_max=" << c.stealth_max << "\n"
      << "clicker_delay=" << c.clicker_delay << "\n"
      << "clicker_button=" << c.clicker_button << "\n"
      << "ptt_preset=" << c.ptt_preset << "\n"
      << "shorts_up=" << c.shorts_up << "\n"
      << "shorts_down=" << c.shorts_down << "\n"
      << "alt_is_modifier=" << (c.alt_is_modifier ? 1 : 0) << "\n"
      << "preferred_host=" << c.preferred_host << "\n";
    for (int i = 0; i < kHostSlotCount; ++i) o << "slot" << (i + 1) << "=" << c.host_slots[i] << "\n";
    const std::string path = settings_path();
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << o.str();
    }
    std::rename(tmp.c_str(), path.c_str());
}

} // namespace tools::hid

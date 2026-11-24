#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {

bool text_entry_mode = false;

namespace {

KeyMods last_mods{};
bool alt_is_modifier = false;

std::array<lv_obj_t*, kNavKeyCount> nav_buttons{};
uint32_t last_key = 0;
bool last_key_pressed = false;
bool nav_shortcut_mode = false;
bool fn_held = false;
bool shift_held = false;
bool sym_held = false;
uint32_t pressed_key = 0;
uint32_t press_started_at = 0;
bool long_press_sent = false;
bool pressed_key_consumed = false;
lv_timer_t* long_press_timer = nullptr;
KeyReleaseListener key_release_listener = nullptr;
void* key_release_listener_user_data = nullptr;
GlobalKeyListener global_key_listener = nullptr;
void* global_key_listener_user_data = nullptr;
GlobalKeyListener screen_key_listener = nullptr;
void* screen_key_listener_user_data = nullptr;
GlobalKeyListener early_key_listener = nullptr;
void* early_key_listener_user_data = nullptr;
KeyReleaseListener screen_key_release_listener = nullptr;
void* screen_key_release_listener_user_data = nullptr;
bool key_captured = false;
uint32_t system_repeat_at = 0;

constexpr uint32_t kLongPressMs = 700;
constexpr uint32_t kSystemRepeatDelayMs = 380;
constexpr uint32_t kSystemRepeatEveryMs = 140;
constexpr uint32_t kNavRepeatDelayMs = 280;
constexpr uint32_t kNavRepeatEveryMs = 55;

bool system_repeat_key(uint32_t key) {
    return key == kKeyBrightnessDown || key == kKeyBrightnessUp || key == kKeyVolumeDown ||
           key == kKeyVolumeUp;
}

bool nav_repeat_key(uint32_t key) {
    return key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT;
}

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool router_event_pending{false};
    uint32_t router_key{0};
    bool router_pressed{false};
};
#endif

size_t nav_key_to_index(uint32_t key) {
    if (nav_shortcut_mode) {
        switch (key) {
            case LV_KEY_ESC:
                return 4;
            case 'z':
            case 'Z':
            case LV_KEY_LEFT:
                return 1;
            case 'c':
            case 'C':
            case LV_KEY_RIGHT:
                return 3;
            default:
                return kNavKeyCount;
        }
    }

    switch (key) {
        case '4':
        case LV_KEY_ESC:
            return 0;
        case '5':
            return 1;
        case '6':
            return 2;
        case '7':
            return 3;
        case '8':
            return 4;
        default:
            return kNavKeyCount;
    }
}

void dispatch_nav_key(uint32_t key) {
    const auto index = nav_key_to_index(key);
    if (index >= nav_buttons.size()) {
        return;
    }

    auto* button = nav_buttons[index];
    if (!button || !lv_obj_is_valid(button) || !lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }

    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

bool emit_global_key(uint32_t key, bool long_pressed) {
    const char* name = describe_key(key);
    if (key_captured) {
        if (global_key_listener) {
            global_key_listener(key, name, long_pressed, global_key_listener_user_data);
        }
        return true;
    }
    if (early_key_listener &&
        early_key_listener(key, name, long_pressed, early_key_listener_user_data)) {
        return true;
    }
    if (global_key_listener &&
        global_key_listener(key, name, long_pressed, global_key_listener_user_data)) {
        return true;
    }
    if (screen_key_listener &&
        screen_key_listener(key, name, long_pressed, screen_key_listener_user_data)) {
        return true;
    }
    return false;
}

void long_press_timer_cb(lv_timer_t*) {
    if (!pressed_key) {
        return;
    }

    // Brightness and volume keep stepping while held, the same way the OS does.
    if (system_repeat_key(pressed_key)) {
        const auto elapsed = lv_tick_elaps(press_started_at);
        if (elapsed >= kSystemRepeatDelayMs &&
            (system_repeat_at == 0 || lv_tick_elaps(system_repeat_at) >= kSystemRepeatEveryMs)) {
            system_repeat_at = lv_tick_get();
            emit_global_key(pressed_key, false);
        }
    }

    // Arrows, and bare F/X/Z/C once they have been resolved to arrows, keep moving.
    if (nav_repeat_key(pressed_key)) {
        const auto elapsed = lv_tick_elaps(press_started_at);
        if (elapsed >= kNavRepeatDelayMs &&
            (system_repeat_at == 0 || lv_tick_elaps(system_repeat_at) >= kNavRepeatEveryMs)) {
            system_repeat_at = lv_tick_get();
            emit_global_key(pressed_key, false);
        }
        return;
    }

    if (long_press_sent || lv_tick_elaps(press_started_at) < kLongPressMs) {
        return;
    }

    long_press_sent = true;
    if (emit_global_key(pressed_key, true)) {
        pressed_key_consumed = true;
    }
}

void ensure_long_press_timer() {
    if (!long_press_timer) {
        long_press_timer = lv_timer_create(long_press_timer_cb, 40, nullptr);
    }
    if (long_press_timer) {
        lv_timer_resume(long_press_timer);
        lv_timer_reset(long_press_timer);
    }
}

void route_key_state(uint32_t key, bool pressed) {
    const bool new_hold = pressed && (!last_key_pressed || pressed_key != key);
    if (new_hold) {
        pressed_key = key;
        press_started_at = lv_tick_get();
        long_press_sent = false;
        system_repeat_at = 0;
        pressed_key_consumed = emit_global_key(key, false);
        ensure_long_press_timer();
        if (!pressed_key_consumed) {
            dispatch_nav_key(key);
        }
    }
    else if (!pressed && last_key_pressed && pressed_key == key) {
        const bool captured = key_captured;
        if (key_release_listener) {
            key_release_listener(key, describe_key(key), key_release_listener_user_data);
        }
        if (!captured && screen_key_release_listener) {
            screen_key_release_listener(key, describe_key(key), screen_key_release_listener_user_data);
        }
        pressed_key = 0;
        pressed_key_consumed = false;
        system_repeat_at = 0;
        if (long_press_timer) {
            lv_timer_pause(long_press_timer);
        }
    }

    last_key = key;
    last_key_pressed = pressed;
}

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

#if !USE_DESKTOP
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        // LVGL emits LV_EVENT_KEY on every poll, even when evdev had no new event.
        if (!keypad->router_event_pending) {
            return;
        }

        keypad->router_event_pending = false;
        const auto key = keypad->router_key;
        const bool pressed = keypad->router_pressed;

        route_key_state(key, pressed);
        return;
    }
#endif

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    route_key_state(key, pressed);
}

#if !USE_DESKTOP
uint32_t map_evdev_key(uint16_t code) {
    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_DELETE:
            return LV_KEY_DEL;
        case KEY_TAB:
            return kKeyTab;
        case KEY_HOME:
            return LV_KEY_HOME;
        case KEY_END:
            return LV_KEY_END;
        case KEY_PAGEUP:
            return LV_KEY_PREV;
        case KEY_PAGEDOWN:
            return LV_KEY_NEXT;
        case KEY_SPACE:
            return ' ';
        case KEY_SYSRQ:
            return kKeyPrintScreen;
        case KEY_HELP:
            return kKeyHelp;
        case KEY_F1:
            return kKeyF1;
        case KEY_F2:
            return kKeyF2;
        case KEY_F3:
            return kKeyF3;
        case KEY_F4:
            return kKeyF4;
        case KEY_F5:
            return kKeyF5;
        case KEY_F6:
            return kKeyF6;
        case KEY_F7:
            return kKeyF7;
        case KEY_F8:
            return kKeyF8;
        case KEY_F9:
            return kKeyF9;
        case KEY_F10:
            return kKeyF10;
        case KEY_F11:
            return kKeyF11;
        case KEY_F12:
            return kKeyF12;
        // Fn chords the tca8418 keymap emits as a single code.
        case KEY_FN_F:
            return LV_KEY_UP;
        case KEY_FN_1:
            return kKeyF1;
        case KEY_FN_2:
            return kKeyF2;
        // Sym punctuation the Zero keymap emits outside the letter range.
        // Observed on device: 199={  200=}  211=;  216='  233=|
        case 199:
            return '{';
        case KEY_PLAYCD:
            return '}';
        case KEY_HP:
            return ';';
        case KEY_CHAT:
            return '\'';
        case KEY_FORWARDMAIL:
            return '|';
        // Hardware Sym layer may emit these directly (tca8418 rewrite).
        case KEY_SEMICOLON:
            return ';';
        case KEY_COMMA:
            return ',';
        case KEY_DOT:
            return '.';
        case KEY_SLASH:
            return '/';
        case KEY_APOSTROPHE:
            return '\'';
        case KEY_GRAVE:
            return '`';
        case KEY_MINUS:
            return '-';
        case KEY_EQUAL:
            return '=';
        case KEY_LEFTBRACE:
            return '[';
        case KEY_RIGHTBRACE:
            return ']';
        case KEY_BACKSLASH:
            return '\\';
        // Sym+1..8 on Zero often arrive as KEY_F13..KEY_F20 (= !@#$%^&*).
        case KEY_F13:
            return '!';
        case KEY_F14:
            return '@';
        case KEY_F15:
            return '#';
        case KEY_F16:
            return '$';
        case KEY_F17:
            return '%';
        case KEY_F18:
            return '^';
        case KEY_F19:
            return '&';
        case KEY_F20:
            return '*';
        // Orange Fn legends the Cardputer OS emits as ordinary evdev codes.
        // The app grabs the keypad, so these never reach the desktop session.
        case KEY_MUTE:
            return kKeyMute;
        case KEY_VOLUMEDOWN:
            return kKeyVolumeDown;
        case KEY_VOLUMEUP:
            return kKeyVolumeUp;
        case KEY_PLAYPAUSE:
        case KEY_PLAY:
        case KEY_PAUSE:
            return kKeyMediaPlay;
        case KEY_NEXTSONG:
            return kKeyMediaNext;
        case KEY_PREVIOUSSONG:
            return kKeyMediaPrev;
        case KEY_BRIGHTNESSDOWN:
            return kKeyBrightnessDown;
        case KEY_BRIGHTNESSUP:
            return kKeyBrightnessUp;
        case KEY_PRINT:
            return kKeyPrintScreen;
        case KEY_INSERT:
            return kKeyInsert;
        case KEY_NEXT:
            return LV_KEY_NEXT;
        case KEY_PREVIOUS:
            return LV_KEY_PREV;
        default:
            break;
    }

    if (code >= KEY_1 && code <= KEY_9) {
        return static_cast<uint32_t>('1' + (code - KEY_1));
    }
    if (code == KEY_0) {
        return '0';
    }
    // Linux keycodes are not alphabetical. KEY_A is 30 and KEY_Z is 44, so a
    // range would turn S into 'b' and drop Q/W/E and X/C/V/B/N/M entirely.
    switch (code) {
        case KEY_A: return 'a';
        case KEY_B: return 'b';
        case KEY_C: return 'c';
        case KEY_D: return 'd';
        case KEY_E: return 'e';
        case KEY_F: return 'f';
        case KEY_G: return 'g';
        case KEY_H: return 'h';
        case KEY_I: return 'i';
        case KEY_J: return 'j';
        case KEY_K: return 'k';
        case KEY_L: return 'l';
        case KEY_M: return 'm';
        case KEY_N: return 'n';
        case KEY_O: return 'o';
        case KEY_P: return 'p';
        case KEY_Q: return 'q';
        case KEY_R: return 'r';
        case KEY_S: return 's';
        case KEY_T: return 't';
        case KEY_U: return 'u';
        case KEY_V: return 'v';
        case KEY_W: return 'w';
        case KEY_X: return 'x';
        case KEY_Y: return 'y';
        case KEY_Z: return 'z';
        default: return 0;
    }
}

bool has_key_bit(const unsigned long* key_bits, int code) {
    const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
    return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
}

bool is_keyboard_device(int fd) {
    unsigned long ev_bits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
        return false;
    }
    const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
    if ((ev_bits[EV_KEY / bits_per_word] & (1UL << (EV_KEY % bits_per_word))) == 0) {
        return false;
    }
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    // Prefer real keyboards (tca8418 advertises letters + enter).
    return has_key_bit(key_bits, KEY_ENTER) || has_key_bit(key_bits, KEY_A) ||
           has_key_bit(key_bits, KEY_LEFT) || has_key_bit(key_bits, KEY_ESC) ||
           has_key_bit(key_bits, KEY_UP);
}

bool device_name_is_tca8418(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        return false;
    }
    return std::strstr(name, "tca8418") != nullptr;
}

struct HeldEvdevKey {
    uint16_t code;
    uint32_t key;
};

std::vector<HeldEvdevKey> held_evdev_keys;

// Cardputer Zero modifiers are sticky: hold applies while down, one tap applies
// to the next key, a second tap locks until the modifier is pressed again.
struct LayerMod {
    bool down{false};
    bool once{false};
    bool locked{false};
    bool unlock_release{false};
    uint32_t down_at{0};
    uint32_t up_at{0};
};

LayerMod fn_mod;
LayerMod sym_mod;
LayerMod shift_mod;
LayerMod ctrl_mod;
LayerMod alt_mod;
uint32_t pending_scan{0};
bool have_scan{false};
std::unordered_map<uint32_t, char> scan_base;

bool layer_on(const LayerMod& mod) {
    return mod.down || mod.once || mod.locked;
}

void layer_down(LayerMod& mod) {
    const auto now = lv_tick_get();
    if (mod.locked) {
        mod.locked = false;
        mod.once = false;
        mod.unlock_release = true;
        mod.down = true;
        mod.down_at = now;
        return;
    }
    if (mod.once && mod.up_at != 0 && lv_tick_elaps(mod.up_at) < 400) {
        mod.locked = true;
        mod.once = false;
        mod.down = true;
        mod.down_at = now;
        return;
    }
    mod.down = true;
    mod.down_at = now;
}

void layer_up(LayerMod& mod) {
    const bool tapped = lv_tick_elaps(mod.down_at) < 450;
    mod.down = false;
    mod.up_at = lv_tick_get();
    if (mod.unlock_release) {
        mod.unlock_release = false;
        mod.once = false;
        return;
    }
    if (!mod.locked) {
        mod.once = tapped;
    }
}

void consume_layer(LayerMod& mod) {
    if (!mod.down && mod.once) {
        mod.once = false;
    }
}

void sync_layer_flags() {
    fn_held = layer_on(fn_mod);
    sym_held = layer_on(sym_mod);
    shift_held = layer_on(shift_mod);
}

bool is_fn_keycode(uint16_t code) {
    switch (code) {
        case KEY_F1:
        case KEY_F2:
        case KEY_F3:
        case KEY_F4:
        case KEY_F5:
        case KEY_F6:
        case KEY_F7:
        case KEY_F8:
        case KEY_F9:
        case KEY_F10:
        case KEY_F11:
        case KEY_F12:
        case KEY_FN_F:
        case KEY_FN_1:
        case KEY_FN_2:
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGEUP:
        case KEY_PAGEDOWN:
        case KEY_INSERT:
        case KEY_DELETE:
        case KEY_MUTE:
        case KEY_VOLUMEDOWN:
        case KEY_VOLUMEUP:
        case KEY_PLAYPAUSE:
        case KEY_PLAY:
        case KEY_PAUSE:
        case KEY_NEXTSONG:
        case KEY_PREVIOUSSONG:
        case KEY_BRIGHTNESSDOWN:
        case KEY_BRIGHTNESSUP:
        case KEY_SYSRQ:
        case KEY_PRINT:
        case KEY_HELP:
            return true;
        default:
            return false;
    }
}

char base_from_code(uint16_t code) {
    const auto mapped = map_evdev_key(code);
    if (mapped >= 'a' && mapped <= 'z') {
        return static_cast<char>(mapped);
    }
    if (mapped >= '0' && mapped <= '9') {
        return static_cast<char>(mapped);
    }
    return 0;
}

bool is_sym_key(uint16_t code) {
    // Zero Sym is not a letter key. Kernel keymaps disagree on the code
    // (Compose / Meta / Alt / Caps / Menu); accept all of them so Sym+B=. and
    // Sym+N=/ reach text fields (subnet CIDR, calc expressions, prompts).
    return code == KEY_COMPOSE || code == KEY_LEFTMETA || code == KEY_RIGHTMETA ||
           code == KEY_CAPSLOCK || code == KEY_LEFTALT || code == KEY_RIGHTALT ||
           code == KEY_MENU;
}

bool is_fn_key(uint16_t code) {
    return code == KEY_FN;
}

bool is_ctrl_key(uint16_t code) {
    return code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL;
}

bool is_alt_key(uint16_t code) {
    return code == KEY_LEFTALT || code == KEY_RIGHTALT;
}

// Physical keycap for a press: the kernel code's letter/digit, or the one this
// scan code produced before the kernel rewrote it for a Sym/Fn chord.
char physical_base(uint16_t code) {
    const char from_code = base_from_code(code);
    if (from_code != 0 || !have_scan) return from_code;
    const auto it = scan_base.find(pending_scan);
    return it != scan_base.end() ? it->second : 0;
}

uint32_t resolve_evdev_key(uint16_t code) {
    if (code == KEY_BACKSPACE) {
        return fn_held ? LV_KEY_DEL : LV_KEY_BACKSPACE;
    }

    const auto mapped = map_evdev_key(code);
    if (mapped >= 'a' && mapped <= 'z') {
        const char base = static_cast<char>(mapped);
        return resolve_printed_key(base, sym_char(base), fn_held, shift_held, sym_held);
    }
    if (mapped >= '0' && mapped <= '9') {
        const char base = static_cast<char>(mapped);
        return resolve_printed_key(base, sym_char(base), fn_held, false, sym_held);
    }
    return mapped;
}

// Bare letters follow the kernel keycode. A different code from the same
// physical key is the kernel's Sym/Fn rewrite; replace it with the Zero legend.
uint32_t resolve_physical_key(uint16_t code) {
    sync_layer_flags();
    const char from_code = base_from_code(code);
    char known = 0;
    if (have_scan) {
        const auto it = scan_base.find(pending_scan);
        if (it != scan_base.end()) {
            known = it->second;
        }
        if (from_code != 0) {
            scan_base[pending_scan] = from_code;
            known = from_code;
        }
    }

    const bool rewritten = known != 0 && from_code != known;
    if (!rewritten) {
        return resolve_evdev_key(code);
    }

    const bool fn = fn_held || is_fn_keycode(code);
    if (fn) {
        const auto legend = fn_legend(known);
        if (legend != 0) {
            return legend;
        }
    }
    const char punct = sym_char(known);
    if (punct != 0) {
        return static_cast<uint32_t>(static_cast<unsigned char>(punct));
    }
    return resolve_printed_key(known, 0, fn_held, shift_held, sym_held);
}

uint32_t take_held_evdev_key(uint16_t code, uint32_t fallback) {
    for (auto it = held_evdev_keys.begin(); it != held_evdev_keys.end(); ++it) {
        if (it->code == code) {
            const auto key = it->key;
            held_evdev_keys.erase(it);
            return key;
        }
    }
    return fallback;
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type == EV_MSC && input.code == MSC_SCAN) {
            pending_scan = static_cast<uint32_t>(input.value);
            have_scan = true;
            continue;
        }
        if (input.type != EV_KEY) {
            continue;
        }

        if (is_fn_key(input.code)) {
            if (input.value == 1) {
                layer_down(fn_mod);
            }
            else if (input.value == 0) {
                layer_up(fn_mod);
            }
            sync_layer_flags();
            continue;
        }
        if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT) {
            if (input.value == 1) {
                layer_down(shift_mod);
            }
            else if (input.value == 0) {
                layer_up(shift_mod);
            }
            sync_layer_flags();
            continue;
        }
        // Ctrl never changes the resolved key; it is only reported in KeyMods.
        if (is_ctrl_key(input.code)) {
            if (input.value == 1) {
                layer_down(ctrl_mod);
            }
            else if (input.value == 0) {
                layer_up(ctrl_mod);
            }
            continue;
        }
        if (alt_is_modifier && is_alt_key(input.code)) {
            if (input.value == 1) {
                layer_down(alt_mod);
            }
            else if (input.value == 0) {
                layer_up(alt_mod);
            }
            continue;
        }
        // Sym is Compose/Meta/Alt/Caps on the tca8418 keymap, not a letter key.
        if (is_sym_key(input.code)) {
            if (input.value == 1) {
                layer_down(sym_mod);
            }
            else if (input.value == 0) {
                layer_up(sym_mod);
            }
            sync_layer_flags();
            continue;
        }
        if (input.value == 2) {
            continue;
        }

        uint32_t key = 0;
        if (input.value) {
            const char base = physical_base(input.code);
            key = resolve_physical_key(input.code);
            if (!key) {
                have_scan = false;
                continue;
            }
            KeyMods snap;
            snap.ctrl = layer_on(ctrl_mod);
            snap.alt = layer_on(alt_mod);
            snap.shift = shift_held;
            snap.fn = fn_held || is_fn_keycode(input.code);
            snap.sym = sym_held;
            snap.base = base;
            snap.seq = last_mods.seq + 1;
            last_mods = snap;
            consume_layer(ctrl_mod);
            consume_layer(alt_mod);
            if (fn_held) {
                consume_layer(fn_mod);
            }
            if (sym_held) {
                consume_layer(sym_mod);
            }
            if (shift_held) {
                consume_layer(shift_mod);
            }
            sync_layer_flags();
            have_scan = false;
            held_evdev_keys.push_back({input.code, key});
        }
        else {
            key = take_held_evdev_key(input.code, 0);
            have_scan = false;
            if (!key) {
                continue;
            }
        }

        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->router_event_pending = true;
        keypad->router_key = key;
        keypad->router_pressed = keypad->state == LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        ioctl(keypad->fd, EVIOCGRAB, 0);
        close(keypad->fd);
    }
    delete keypad;
}

lv_indev_t* create_keypad_from_fd(int fd) {
    // Take exclusive access so APPLaunch / other clients do not steal keys.
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        LV_LOG_WARN("EVIOCGRAB failed on fd %d: %s (continuing without grab)", fd, strerror(errno));
    }

    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        ioctl(fd, EVIOCGRAB, 0);
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

lv_indev_t* try_create_keypad(const char* path, bool require_tca8418) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (require_tca8418 && !device_name_is_tca8418(fd)) {
        close(fd);
        return nullptr;
    }

    if (!is_keyboard_device(fd)) {
        close(fd);
        return nullptr;
    }

    char name[128] = {};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    LV_LOG_INFO("using evdev key input %s (%s)", path, name[0] ? name : "unknown");
    return create_keypad_from_fd(fd);
}

void discover_keypads(lv_display_t* display) {
    const char* configured_device = APP_KEY_INPUT_DEVICE;
    if (configured_device[0] != '\0') {
        auto* indev = try_create_keypad(configured_device, false);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return;
    }

    // Prefer by-path symlink when present.
    if (auto* indev =
            try_create_keypad("/dev/input/by-path/platform-3f804000.i2c-event", false)) {
        lv_indev_set_display(indev, display);
        return;
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return;
    }

    // First pass: tca8418c only.
    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        std::string path = "/dev/input/";
        path += entry->d_name;
        if (auto* indev = try_create_keypad(path.c_str(), true)) {
            lv_indev_set_display(indev, display);
            closedir(dir);
            return;
        }
    }

    rewinddir(dir);
    // Fallback: first keyboard-like device.
    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        std::string path = "/dev/input/";
        path += entry->d_name;
        if (auto* indev = try_create_keypad(path.c_str(), false)) {
            lv_indev_set_display(indev, display);
            closedir(dir);
            return;
        }
    }

    closedir(dir);
    LV_LOG_WARN("no usable keyboard evdev device found");
}
#endif

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    discover_keypads(display);
#else
    LV_UNUSED(display);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

void set_nav_shortcut_mode(bool enabled) {
    nav_shortcut_mode = enabled;
}

void register_nav_button(size_t index, lv_obj_t* button) {
    if (index < nav_buttons.size()) {
        nav_buttons[index] = button;
    }
}

void unregister_nav_button(size_t index, lv_obj_t* button) {
    if (index >= nav_buttons.size()) {
        return;
    }

    if (!button || nav_buttons[index] == button) {
        nav_buttons[index] = nullptr;
    }
}

void set_key_release_listener(KeyReleaseListener listener, void* user_data) {
    key_release_listener = listener;
    key_release_listener_user_data = user_data;
}

void clear_key_release_listener(KeyReleaseListener listener, void* user_data) {
    if (key_release_listener == listener && key_release_listener_user_data == user_data) {
        key_release_listener = nullptr;
        key_release_listener_user_data = nullptr;
    }
}

void set_global_key_listener(GlobalKeyListener listener, void* user_data) {
    global_key_listener = listener;
    global_key_listener_user_data = user_data;
}

void clear_global_key_listener(GlobalKeyListener listener, void* user_data) {
    if (global_key_listener == listener && global_key_listener_user_data == user_data) {
        global_key_listener = nullptr;
        global_key_listener_user_data = nullptr;
    }
}

void set_screen_key_listener(GlobalKeyListener listener, void* user_data) {
    screen_key_listener = listener;
    screen_key_listener_user_data = user_data;
}

void clear_screen_key_listener(GlobalKeyListener listener, void* user_data) {
    if (screen_key_listener == listener && screen_key_listener_user_data == user_data) {
        screen_key_listener = nullptr;
        screen_key_listener_user_data = nullptr;
    }
}

void set_early_key_listener(GlobalKeyListener listener, void* user_data) {
    early_key_listener = listener;
    early_key_listener_user_data = user_data;
}

void clear_early_key_listener(GlobalKeyListener listener, void* user_data) {
    if (early_key_listener == listener && early_key_listener_user_data == user_data) {
        early_key_listener = nullptr;
        early_key_listener_user_data = nullptr;
    }
}

void set_screen_key_release_listener(KeyReleaseListener listener, void* user_data) {
    screen_key_release_listener = listener;
    screen_key_release_listener_user_data = user_data;
}

void clear_screen_key_release_listener(KeyReleaseListener listener, void* user_data) {
    if (screen_key_release_listener == listener &&
        screen_key_release_listener_user_data == user_data) {
        screen_key_release_listener = nullptr;
        screen_key_release_listener_user_data = nullptr;
    }
}

void set_key_captured(bool captured) {
    key_captured = captured;
}

const char* describe_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ESC:
            return "Esc";
        case LV_KEY_LEFT:
            return "Left";
        case LV_KEY_RIGHT:
            return "Right";
        case LV_KEY_UP:
            return "Up";
        case LV_KEY_DOWN:
            return "Down";
        case LV_KEY_ENTER:
            return "Enter";
        case kKeyPrintScreen:
            return "PrintScreen";
        case kKeyHelp:
            return "Help";
        case kKeyF1:
            return "F1";
        case kKeyF2:
            return "F2";
        case kKeyF3:
            return "F3";
        case kKeyF4:
            return "F4";
        case kKeyF5:
            return "F5";
        case kKeyF6:
            return "F6";
        case kKeyF7:
            return "F7";
        case kKeyF8:
            return "F8";
        case kKeyF9:
            return "F9";
        case kKeyF10:
            return "F10";
        case kKeyF11:
            return "F11";
        case kKeyF12:
            return "F12";
        case kKeyMediaPlay:
            return "Play";
        case kKeyMediaPrev:
            return "Previous";
        case kKeyMediaNext:
            return "Next";
        case kKeyMute:
            return "Mute";
        case kKeyVolumeDown:
            return "Vol-";
        case kKeyVolumeUp:
            return "Vol+";
        case kKeyBrightnessDown:
            return "Bright-";
        case kKeyBrightnessUp:
            return "Bright+";
        case kKeyInsert:
            return "Insert";
        case kKeyTab:
            return "Tab";
        default:
            break;
    }

    static char buffer[24];
    if (key >= 32 && key <= 126) {
        std::snprintf(buffer, sizeof(buffer), "%c", static_cast<char>(key));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "Key_%u", key);
    }
    return buffer;
}

void deliver_screen_key(uint32_t key) {
    if (!screen_key_listener || key == 0) return;
    screen_key_listener(key, describe_key(key), false, screen_key_listener_user_data);
}

bool shortcut_key_reserved(uint32_t key) {
    switch (key) {
    case LV_KEY_ESC:
    case LV_KEY_ENTER:
    case '\r':
    case ' ':
    case LV_KEY_UP:
    case LV_KEY_DOWN:
    case LV_KEY_LEFT:
    case LV_KEY_RIGHT:
    case LV_KEY_BACKSPACE:
    case LV_KEY_DEL:
    case LV_KEY_HOME:
    case LV_KEY_END:
    case LV_KEY_NEXT:
    case LV_KEY_PREV:
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'f':
    case 'F':
    case 'x':
    case 'X':
    case 'z':
    case 'Z':
    case 'c':
    case 'C':
        return true;
    default:
        break;
    }
    if (key == kKeyTab || key == kKeyHelp || key == kKeyPrintScreen || key == kKeyInsert) return true;
    if (key == kKeyBrightnessDown || key == kKeyBrightnessUp) return true;
    if (key == kKeyVolumeDown || key == kKeyVolumeUp || key == kKeyMute) return true;
    if (key == kKeyMediaPlay || key == kKeyMediaPrev || key == kKeyMediaNext) return true;
    if (key >= kKeyF1 && key <= kKeyF12) return true;
    if (key < 128) {
        switch (static_cast<char>(key)) {
        case '.':
        case ',':
        case '/':
        case ';':
        case '\'':
        case '`':
        case '~':
        case '-':
        case '=':
        case '[':
        case ']':
        case '\\':
        case '|':
        case '{':
        case '}':
        case '!':
        case '@':
        case '#':
        case '$':
        case '%':
        case '^':
        case '&':
        case '*':
        case '(':
        case ')':
        case '<':
        case '>':
        case '?':
        case '"':
        case ':':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool shortcut_key_allowed(uint32_t key) {
    if (shortcut_key_reserved(key)) return false;
    return (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z');
}

std::string shortcut_key_label(uint32_t key) {
    if (key >= 32 && key < 127) return std::string(1, static_cast<char>(key));
    return "?";
}

char sym_char(char base) {
    switch (base) {
        case '1':
            return '!';
        case '2':
            return '@';
        case '3':
            return '#';
        case '4':
            return '$';
        case '5':
            return '%';
        case '6':
            return '^';
        case '7':
            return '&';
        case '8':
            return '*';
        case '9':
            return '(';
        case '0':
            return ')';
        case 'q':
            return '~';
        case 'w':
            return '`';
        case 'e':
            return '_';
        case 'r':
            return '-';
        case 't':
            return '+';
        case 'y':
            return '=';
        case 'u':
            return '[';
        case 'i':
            return ']';
        case 'o':
            return '{';
        case 'p':
            return '}';
        case 'a':
            return ';';
        case 's':
            return ':';
        case 'd':
            return '\'';
        case 'g':
            return '"';
        case 'h':
            return '<';
        case 'j':
            return '>';
        case 'k':
            return '\\';
        case 'l':
            return '|';
        case 'v':
            return ',';
        case 'b':
            return '.';
        case 'n':
            return '/';
        case 'm':
            return '?';
        default:
            return 0;
    }
}

uint32_t fn_legend(char base) {
    switch (base) {
        case '1':
            return kKeyF1;
        case '2':
            return kKeyF2;
        case '3':
            return kKeyF3;
        case '4':
            return kKeyF4;
        case '5':
            return kKeyF5;
        case '6':
            return kKeyF6;
        case '7':
            return kKeyF7;
        case '8':
            return kKeyF8;
        case '9':
            return kKeyF9;
        case '0':
            return kKeyF10;
        case 'q':
            return kKeyMediaPlay;
        case 'w':
            return kKeyMediaPrev;
        case 'e':
            return kKeyMediaNext;
        case 'u':
            return kKeyBrightnessDown;
        case 'i':
            return kKeyBrightnessUp;
        case 'o':
            return kKeyF11;
        case 'p':
            return kKeyF12;
        case 'a':
            return kKeyMute;
        case 's':
            return kKeyVolumeDown;
        case 'd':
            return kKeyVolumeUp;
        case 'f':
            return LV_KEY_UP;
        case 'h':
            return kKeyHelp;
        case 'j':
            return kKeyPrintScreen;
        case 'k':
            return LV_KEY_HOME;
        case 'l':
            return LV_KEY_PREV;
        case 'z':
            return LV_KEY_LEFT;
        case 'x':
            return LV_KEY_DOWN;
        case 'c':
            return LV_KEY_RIGHT;
        case 'b':
            return kKeyInsert;
        case 'n':
            return LV_KEY_END;
        case 'm':
            return LV_KEY_NEXT;
        default:
            return 0;
    }
}

uint32_t resolve_printed_key(char base, char sym, bool fn_held, bool shift, bool symbol_mode) {
    const bool letter = base >= 'a' && base <= 'z';
    const bool digit = base >= '0' && base <= '9';
    const auto legend = fn_legend(base);
    const char punct = (sym != 0) ? sym : sym_char(base);
    const bool arrow = legend == LV_KEY_UP || legend == LV_KEY_DOWN || legend == LV_KEY_LEFT ||
                       legend == LV_KEY_RIGHT;

    auto letter_out = [&]() -> uint32_t {
        if (!letter) return 0;
        return static_cast<uint32_t>(shift ? (base - 'a' + 'A') : base);
    };

    // Digits: bare stays 0-9 (menu row). Sym → !@#. Fn → F1-F10.
    if (digit) {
        if (fn_held && legend != 0) return legend;
        if (symbol_mode && punct != 0) {
            return static_cast<uint32_t>(static_cast<unsigned char>(punct));
        }
        return static_cast<uint32_t>(static_cast<unsigned char>(base));
    }

    if (letter) {
        // Fn+key → orange legend. Fn+F/X/Z/C are the arrows. Fn wins over Sym.
        if (fn_held && legend != 0) {
            return legend;
        }
        // Sym+key → blue punctuation printed on the keycap (B→. N→/ Q→~ …).
        if (symbol_mode && punct != 0) {
            return static_cast<uint32_t>(static_cast<unsigned char>(punct));
        }
        // Menus also accept bare F/X/Z/C as arrows. Text entry types the letter.
        if (arrow && !text_entry_mode) {
            return legend;
        }
        return letter_out();
    }

    if (shift && letter) {
        return letter_out();
    }
    if (base == 0) {
        return 0;
    }
    return static_cast<uint32_t>(static_cast<unsigned char>(base));
}

void set_text_entry(bool enabled) {
    text_entry_mode = enabled;
}

KeyMods last_key_mods() {
    return last_mods;
}

void note_key_mods(const KeyMods& mods) {
    const uint32_t seq = last_mods.seq + 1;
    last_mods = mods;
    last_mods.seq = seq;
}

void set_alt_is_modifier(bool enabled) {
#if !USE_DESKTOP
    if (alt_is_modifier != enabled) {
        // Do not leave a half-applied Alt/Sym layer behind when switching roles.
        alt_mod = {};
        sym_mod = {};
        sync_layer_flags();
    }
#endif
    alt_is_modifier = enabled;
}

} // namespace platform

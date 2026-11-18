#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
namespace {

std::array<lv_obj_t*, kNavKeyCount> nav_buttons{};
uint32_t last_key = 0;
bool last_key_pressed = false;
bool nav_shortcut_mode = false;
bool fn_held = false;
bool shift_held = false;
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

constexpr uint32_t kLongPressMs = 700;

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
    if (!pressed_key || long_press_sent || lv_tick_elaps(press_started_at) < kLongPressMs) {
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
        pressed_key_consumed = emit_global_key(key, false);
        ensure_long_press_timer();
        if (!pressed_key_consumed) {
            dispatch_nav_key(key);
        }
    }
    else if (!pressed && last_key_pressed && pressed_key == key) {
        if (key_release_listener) {
            key_release_listener(key, describe_key(key), key_release_listener_user_data);
        }
        pressed_key = 0;
        pressed_key_consumed = false;
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
            return LV_KEY_NEXT;
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
        case KEY_F1:
            return kKeyHelp;
        // Cardputer-style ; , . / navigation aliases.
        case KEY_SEMICOLON:
            return LV_KEY_UP;
        case KEY_COMMA:
            return LV_KEY_LEFT;
        case KEY_DOT:
            return LV_KEY_DOWN;
        case KEY_SLASH:
            return LV_KEY_RIGHT;
        default:
            break;
    }

    // Alphanumeric / punctuation for typing and WASD nav.
    if (code >= KEY_1 && code <= KEY_9) {
        return static_cast<uint32_t>('1' + (code - KEY_1));
    }
    if (code == KEY_0) {
        return '0';
    }
    if (code >= KEY_A && code <= KEY_Z) {
        return static_cast<uint32_t>('a' + (code - KEY_A));
    }
    switch (code) {
        case KEY_MINUS:
            return '-';
        case KEY_EQUAL:
            return '=';
        case KEY_LEFTBRACE:
            return '[';
        case KEY_RIGHTBRACE:
            return ']';
        case KEY_APOSTROPHE:
            return '\'';
        case KEY_GRAVE:
            return '`';
        case KEY_BACKSLASH:
            return '\\';
        default:
            return 0;
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

uint32_t resolve_evdev_key(uint16_t code) {
    if (code == KEY_BACKSPACE) {
        return fn_held ? LV_KEY_DEL : LV_KEY_BACKSPACE;
    }

    const auto mapped = map_evdev_key(code);
    if (mapped >= 'a' && mapped <= 'z') {
        return resolve_printed_key(static_cast<char>(mapped), 0, fn_held, shift_held, false);
    }
    if (mapped >= '0' && mapped <= '9') {
        return resolve_printed_key(static_cast<char>(mapped), 0, fn_held, false, false);
    }
    return mapped;
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
        if (input.type != EV_KEY) {
            continue;
        }

        if (input.code == KEY_FN) {
            fn_held = input.value != 0;
            continue;
        }
        if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT) {
            shift_held = input.value != 0;
            continue;
        }
        if (input.value == 2) {
            continue;
        }

        uint32_t key = 0;
        if (input.value) {
            key = resolve_evdev_key(input.code);
            if (!key) {
                continue;
            }
            held_evdev_keys.push_back({input.code, key});
        }
        else {
            key = take_held_evdev_key(input.code, resolve_evdev_key(input.code));
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
    const auto legend = fn_legend(base);

    if (fn_held && letter) {
        return static_cast<uint32_t>(shift ? (base - 'a' + 'A') : base);
    }
    if (symbol_mode && sym != 0) {
        return static_cast<uint32_t>(static_cast<unsigned char>(sym));
    }
    // Letters take their orange Fn legend as the bare key. Digits stay digits
    // so 4-8 remain the menu row; Fn+digit still selects F1-F10.
    if (letter && legend != 0 && !fn_held) {
        return legend;
    }
    if (fn_held && legend != 0) {
        return legend;
    }
    if (shift && letter) {
        return static_cast<uint32_t>(base - 'a' + 'A');
    }
    if (base == 0) {
        return 0;
    }
    return static_cast<uint32_t>(static_cast<unsigned char>(base));
}

} // namespace platform

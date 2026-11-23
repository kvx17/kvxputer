#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace platform {

constexpr size_t kNavKeyCount = 5;
constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;
constexpr uint32_t kKeyHelp = kSpecialKeyBase | 19U;
constexpr uint32_t kKeyF1 = kSpecialKeyBase | 0x30U;
constexpr uint32_t kKeyF2 = kSpecialKeyBase | 0x31U;
constexpr uint32_t kKeyF3 = kSpecialKeyBase | 0x32U;
constexpr uint32_t kKeyF4 = kSpecialKeyBase | 0x33U;
constexpr uint32_t kKeyF5 = kSpecialKeyBase | 0x34U;
constexpr uint32_t kKeyF6 = kSpecialKeyBase | 0x35U;
constexpr uint32_t kKeyF7 = kSpecialKeyBase | 0x36U;
constexpr uint32_t kKeyF8 = kSpecialKeyBase | 0x37U;
constexpr uint32_t kKeyF9 = kSpecialKeyBase | 0x38U;
constexpr uint32_t kKeyF10 = kSpecialKeyBase | 0x39U;
constexpr uint32_t kKeyF11 = kSpecialKeyBase | 0x3AU;
constexpr uint32_t kKeyF12 = kSpecialKeyBase | 0x3BU;
constexpr uint32_t kKeyMediaPlay = kSpecialKeyBase | 0x40U;
constexpr uint32_t kKeyMediaPrev = kSpecialKeyBase | 0x41U;
constexpr uint32_t kKeyMediaNext = kSpecialKeyBase | 0x42U;
constexpr uint32_t kKeyMute = kSpecialKeyBase | 0x43U;
constexpr uint32_t kKeyVolumeDown = kSpecialKeyBase | 0x44U;
constexpr uint32_t kKeyVolumeUp = kSpecialKeyBase | 0x45U;
constexpr uint32_t kKeyBrightnessDown = kSpecialKeyBase | 0x46U;
constexpr uint32_t kKeyBrightnessUp = kSpecialKeyBase | 0x47U;
constexpr uint32_t kKeyInsert = kSpecialKeyBase | 0x48U;
constexpr uint32_t kKeyTab = kSpecialKeyBase | 0x49U;

// Cardputer Zero layers (printed keycap guide, not Cardputer ADV):
//   bare letter/digit = the keycap; shift = uppercase
//   Sym+key = blue punctuation (Q~, B., V,, N/)
//   Fn+key = orange legend; Fn+F/X/Z/C = up/down/left/right
// Hold applies while held. One tap applies to the next key. A second tap locks.
// Menus also accept bare F/X/Z/C as arrows. Text screens type those letters.
char sym_char(char base);
uint32_t fn_legend(char base);
uint32_t resolve_printed_key(char base, char sym, bool fn_held, bool shift, bool symbol_mode);
void set_text_entry(bool enabled);

using KeyReleaseListener = void (*)(uint32_t key, const char* key_name, void* user_data);
using GlobalKeyListener = bool (*)(uint32_t key,
                                   const char* key_name,
                                   bool long_pressed,
                                   void* user_data);

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void set_nav_shortcut_mode(bool enabled);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);
void set_key_release_listener(KeyReleaseListener listener, void* user_data);
void clear_key_release_listener(KeyReleaseListener listener, void* user_data);
void set_global_key_listener(GlobalKeyListener listener, void* user_data);
void clear_global_key_listener(GlobalKeyListener listener, void* user_data);
// Screen navigation handler; invoked after the global/shortcut listener returns false.
void set_screen_key_listener(GlobalKeyListener listener, void* user_data);
void clear_screen_key_listener(GlobalKeyListener listener, void* user_data);
// Runs before the global shortcut handler. Return true to consume the key.
void set_early_key_listener(GlobalKeyListener listener, void* user_data);
void clear_early_key_listener(GlobalKeyListener listener, void* user_data);
// Key-up for the active screen. Does not replace the shortcut release listener.
void set_screen_key_release_listener(KeyReleaseListener listener, void* user_data);
void clear_screen_key_release_listener(KeyReleaseListener listener, void* user_data);
const char* describe_key(uint32_t key);
// Deliver a key to the active screen only. Does not re-enter shortcut handling.
void deliver_screen_key(uint32_t key);

// Letters the user may bind. Movement, the 4–8 bar, Ok, Esc, Space, Tab, and
// the Fn system keys are rejected.
bool shortcut_key_reserved(uint32_t key);
bool shortcut_key_allowed(uint32_t key);
std::string shortcut_key_label(uint32_t key);

} // namespace platform

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

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

// Cardputer Zero printed layers (matches tca8418 firmware):
//   bare letter  → the letter (F/Z/X/C stay arrows for nav)
//   Sym+letter   → punctuation printed on the key (B→. V→, N→/ …)
//                 Sym is often rewritten in hardware to KEY_DOT / F13… — those
//                 arrive as punctuation codes and skip this soft map.
//   Fn+letter    → orange Fn legend; Fn+F/Z/X/C types the letter
// Digits stay digits so 4–8 remain the menu row; Fn+digit → F1–F10; Sym+digit → !@#…
char sym_char(char base);
uint32_t fn_legend(char base);
uint32_t resolve_printed_key(char base, char sym, bool fn_held, bool shift, bool symbol_mode);

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
const char* describe_key(uint32_t key);

} // namespace platform

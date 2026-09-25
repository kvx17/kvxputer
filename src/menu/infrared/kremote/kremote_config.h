#pragma once

#include <Arduino.h>

static constexpr unsigned long KREMOTE_HOLD_MS = 450;
static constexpr unsigned long KREMOTE_DOUBLE_TAP_MS = 400;
static constexpr unsigned long KREMOTE_FLASH_MS = 120;
static constexpr unsigned long KREMOTE_REPEAT_MS = 180;
static constexpr int KREMOTE_SLOT_COUNT = 12;
static constexpr int KREMOTE_NAME_MAX = 24;
static constexpr int KREMOTE_ALL_CODES_MAX = 100;

enum KremoteSlot : uint8_t {
    KREMOTE_POWER = 0,
    KREMOTE_UP,
    KREMOTE_DOWN,
    KREMOTE_LEFT,
    KREMOTE_RIGHT,
    KREMOTE_OK,
    KREMOTE_BACK,
    KREMOTE_HOME,
    KREMOTE_VOL_UP,
    KREMOTE_VOL_DOWN,
    KREMOTE_CH_UP,
    KREMOTE_CH_DOWN,
};

inline const char *kremoteSlotName(int slot) {
    static const char *const names[KREMOTE_SLOT_COUNT] = {
        "POWER", "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "HOME", "VOL+", "VOL-", "CHA+", "CHA-",
    };
    if (slot < 0 || slot >= KREMOTE_SLOT_COUNT) return "";
    return names[slot];
}

enum KremotePhysKey : uint8_t {
    KREMOTE_PHYS_UP = 0,
    KREMOTE_PHYS_DOWN,
    KREMOTE_PHYS_LEFT,
    KREMOTE_PHYS_RIGHT,
    KREMOTE_PHYS_OK,
    KREMOTE_PHYS_BACK,
};

enum KremoteIrHw : uint8_t {
    KREMOTE_IR_ONBOARD = 0, // Cardputer / ADV onboard LED (TXLED)
    KREMOTE_IR_UNIT = 1,    // M5 Unit IR on Grove (TX yellow / RX white)
    KREMOTE_IR_BOTH = 2,    // sequential dual TX
};

// Virtual Remote action ids (fixed Cardputer key map)
enum KremoteAction : int {
    KREMOTE_ACT_NONE = -1,
    KREMOTE_ACT_UP = 0,
    KREMOTE_ACT_DOWN,
    KREMOTE_ACT_LEFT,
    KREMOTE_ACT_RIGHT,
    KREMOTE_ACT_OK,
    KREMOTE_ACT_VOL_UP,
    KREMOTE_ACT_VOL_DOWN,
    KREMOTE_ACT_CH_UP,
    KREMOTE_ACT_CH_DOWN,
    KREMOTE_ACT_POWER_OFF,
    KREMOTE_ACT_POWER_ON,
    KREMOTE_ACT_BACK,
    KREMOTE_ACT_HOME,
    KREMOTE_ACT_MENU,
    KREMOTE_ACT_MUTE,
    KREMOTE_ACT_PLAY,
    KREMOTE_ACT_FORWARD,
    KREMOTE_ACT_REWIND,
    KREMOTE_ACT_DIGIT_1,
    KREMOTE_ACT_DIGIT_2,
    KREMOTE_ACT_DIGIT_3,
    KREMOTE_ACT_DIGIT_4,
    KREMOTE_ACT_DIGIT_5,
    KREMOTE_ACT_DIGIT_6,
    KREMOTE_ACT_DIGIT_7,
    KREMOTE_ACT_DIGIT_8,
    KREMOTE_ACT_DIGIT_9,
    KREMOTE_ACT_DIGIT_0,
    KREMOTE_ACT_COUNT,
};

// Flash segment ids for pad UI + extra Virtual Remote keys
enum KremoteFlash : int {
    KREMOTE_FLASH_NONE = -1,
    KREMOTE_FLASH_UP = 0,
    KREMOTE_FLASH_DOWN = 1,
    KREMOTE_FLASH_LEFT = 2,
    KREMOTE_FLASH_RIGHT = 3,
    KREMOTE_FLASH_OK = 4,
    KREMOTE_FLASH_VOL_UP = 5,
    KREMOTE_FLASH_VOL_DOWN = 6,
    KREMOTE_FLASH_CH_UP = 7,
    KREMOTE_FLASH_CH_DOWN = 8,
    KREMOTE_FLASH_HOME = 9,
    KREMOTE_FLASH_BACK = 10,
    KREMOTE_FLASH_POWER = 11,
    KREMOTE_FLASH_POWER_OFF = 12,
    KREMOTE_FLASH_POWER_ON = 13,
    KREMOTE_FLASH_MENU = 14,
    KREMOTE_FLASH_MUTE = 15,
    KREMOTE_FLASH_PLAY = 16,
    KREMOTE_FLASH_FORWARD = 17,
    KREMOTE_FLASH_REWIND = 18,
    KREMOTE_FLASH_DIGIT = 19,
};

inline int kremoteFlashForAction(int action) {
    switch (action) {
        case KREMOTE_ACT_UP: return KREMOTE_FLASH_UP;
        case KREMOTE_ACT_DOWN: return KREMOTE_FLASH_DOWN;
        case KREMOTE_ACT_LEFT: return KREMOTE_FLASH_LEFT;
        case KREMOTE_ACT_RIGHT: return KREMOTE_FLASH_RIGHT;
        case KREMOTE_ACT_OK: return KREMOTE_FLASH_OK;
        case KREMOTE_ACT_VOL_UP: return KREMOTE_FLASH_VOL_UP;
        case KREMOTE_ACT_VOL_DOWN: return KREMOTE_FLASH_VOL_DOWN;
        case KREMOTE_ACT_CH_UP: return KREMOTE_FLASH_CH_UP;
        case KREMOTE_ACT_CH_DOWN: return KREMOTE_FLASH_CH_DOWN;
        case KREMOTE_ACT_HOME: return KREMOTE_FLASH_HOME;
        case KREMOTE_ACT_BACK: return KREMOTE_FLASH_BACK;
        case KREMOTE_ACT_POWER_OFF: return KREMOTE_FLASH_POWER_OFF;
        case KREMOTE_ACT_POWER_ON: return KREMOTE_FLASH_POWER_ON;
        case KREMOTE_ACT_MENU: return KREMOTE_FLASH_MENU;
        case KREMOTE_ACT_MUTE: return KREMOTE_FLASH_MUTE;
        case KREMOTE_ACT_PLAY: return KREMOTE_FLASH_PLAY;
        case KREMOTE_ACT_FORWARD: return KREMOTE_FLASH_FORWARD;
        case KREMOTE_ACT_REWIND: return KREMOTE_FLASH_REWIND;
        case KREMOTE_ACT_DIGIT_1:
        case KREMOTE_ACT_DIGIT_2:
        case KREMOTE_ACT_DIGIT_3:
        case KREMOTE_ACT_DIGIT_4:
        case KREMOTE_ACT_DIGIT_5:
        case KREMOTE_ACT_DIGIT_6:
        case KREMOTE_ACT_DIGIT_7:
        case KREMOTE_ACT_DIGIT_8:
        case KREMOTE_ACT_DIGIT_9:
        case KREMOTE_ACT_DIGIT_0: return KREMOTE_FLASH_DIGIT;
        default: return KREMOTE_FLASH_NONE;
    }
}

// Legacy helpers kept for Learn Remote slot names.
inline int kremoteResolveSlot(KremotePhysKey key, bool hold, bool swapped) {
    switch (key) {
        case KREMOTE_PHYS_UP: return (hold != swapped) ? KREMOTE_VOL_UP : KREMOTE_UP;
        case KREMOTE_PHYS_DOWN: return (hold != swapped) ? KREMOTE_VOL_DOWN : KREMOTE_DOWN;
        case KREMOTE_PHYS_LEFT: return (hold != swapped) ? KREMOTE_CH_DOWN : KREMOTE_LEFT;
        case KREMOTE_PHYS_RIGHT: return (hold != swapped) ? KREMOTE_CH_UP : KREMOTE_RIGHT;
        case KREMOTE_PHYS_OK: return (hold != swapped) ? KREMOTE_HOME : KREMOTE_OK;
        case KREMOTE_PHYS_BACK: return KREMOTE_BACK;
        default: return -1;
    }
}

inline int kremoteFlashForSlot(int slot) {
    switch (slot) {
        case KREMOTE_UP: return KREMOTE_FLASH_UP;
        case KREMOTE_DOWN: return KREMOTE_FLASH_DOWN;
        case KREMOTE_LEFT: return KREMOTE_FLASH_LEFT;
        case KREMOTE_RIGHT: return KREMOTE_FLASH_RIGHT;
        case KREMOTE_OK: return KREMOTE_FLASH_OK;
        case KREMOTE_HOME: return KREMOTE_FLASH_HOME;
        case KREMOTE_VOL_UP: return KREMOTE_FLASH_VOL_UP;
        case KREMOTE_VOL_DOWN: return KREMOTE_FLASH_VOL_DOWN;
        case KREMOTE_CH_UP: return KREMOTE_FLASH_CH_UP;
        case KREMOTE_CH_DOWN: return KREMOTE_FLASH_CH_DOWN;
        case KREMOTE_BACK: return KREMOTE_FLASH_BACK;
        case KREMOTE_POWER: return KREMOTE_FLASH_POWER;
        default: return KREMOTE_FLASH_NONE;
    }
}

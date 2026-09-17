#pragma once

#include <Arduino.h>

static constexpr unsigned long KREMOTE_HOLD_MS = 450;
static constexpr unsigned long KREMOTE_DOUBLE_TAP_MS = 400;
static constexpr unsigned long KREMOTE_FLASH_MS = 120;
static constexpr int KREMOTE_SLOT_COUNT = 12;
static constexpr int KREMOTE_NAME_MAX = 24;

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

// Flash segment ids for pad UI (mid-ring directions, center OK, outer vol/ch)
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
};

inline int kremoteResolveSlot(KremotePhysKey key, bool hold, bool swapped) {
    switch (key) {
        case KREMOTE_PHYS_UP: return (hold != swapped) ? KREMOTE_VOL_UP : KREMOTE_UP;
        case KREMOTE_PHYS_DOWN: return (hold != swapped) ? KREMOTE_VOL_DOWN : KREMOTE_DOWN;
        case KREMOTE_PHYS_LEFT: return (hold != swapped) ? KREMOTE_CH_DOWN : KREMOTE_LEFT;
        case KREMOTE_PHYS_RIGHT: return (hold != swapped) ? KREMOTE_CH_UP : KREMOTE_RIGHT;
        case KREMOTE_PHYS_OK: return (hold != swapped) ? KREMOTE_HOME : KREMOTE_OK;
        case KREMOTE_PHYS_BACK: return KREMOTE_BACK; // hold = exit, not IR
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

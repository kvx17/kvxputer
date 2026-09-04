#pragma once

#include <stdint.h>

#define KVXKEYBOARD_HID_NAME "kvxkeyboard HID"
#define HID_REMOTE_HOST_SLOTS 8
#define HID_REMOTE_BLE_APPEARANCE 0x03C0

enum HidRemoteTransport {
    HID_REMOTE_USB = 0,
    HID_REMOTE_BLE = 1,
};

enum HidRemoteMode {
    HID_MODE_PRESENTER = 0,
    HID_MODE_PRESENTER_VERT,
    HID_MODE_KEYBOARD,
    HID_MODE_MEDIA,
    HID_MODE_APPLE_MUSIC,
    HID_MODE_MOVIE,
    HID_MODE_MOUSE,
    HID_MODE_SHORTS,
    HID_MODE_CLICKER,
    HID_MODE_JIGGLER,
    HID_MODE_JIGGLER_STEALTH,
    HID_MODE_PUSH_TO_TALK,
    HID_MODE_SHORTCUTS,
    HID_MODE_COUNT
};

enum HidRemoteCapability {
    HID_CAP_KEYBOARD = 1,
    HID_CAP_MOUSE = 2,
    HID_CAP_MEDIA = 4,
};

struct HidRemoteModeInfo {
    const char *label;
    HidRemoteCapability caps;
};

const HidRemoteModeInfo &hidRemoteModeInfo(HidRemoteMode mode);

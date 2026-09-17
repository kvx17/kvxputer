#pragma once

#include "hid_remote_config.h"

enum HidRemoteLaunch {
    HID_REMOTE_LAUNCH_DEFAULT = 0,
    HID_REMOTE_LAUNCH_USB,
    HID_REMOTE_LAUNCH_BLE,
};

void hidRemoteMenu(HidRemoteLaunch launch = HID_REMOTE_LAUNCH_DEFAULT);

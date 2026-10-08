#pragma once

#if !defined(LITE_VERSION)

#include <Arduino.h>
#include <cstdint>
#include <functional>

// Deauth/disassoc flood ("jamming") detector.
// Watches management-frame deauth rate on the selected channel and raises a
// visual alert when it crosses a user-adjustable threshold.
void jam_detect_setup();

// Headless session for PC Connect (no TFT / keyboard).
struct JamChannelSample {
    uint8_t ch = 0;
    uint16_t deauthPerSec = 0;
    uint32_t frames = 0;
    int8_t rssi = -127;
    bool alert = false;
};

bool jamSessionStart();
void jamSessionStop();
bool jamSessionActive();
// One dwell on the next channel in the 1–11 hop. abortFn may end early.
bool jamSessionDwell(
    uint16_t dwellMs, uint32_t thresholdPerSec, JamChannelSample &out,
    const std::function<bool()> &abortFn = nullptr
);

#endif

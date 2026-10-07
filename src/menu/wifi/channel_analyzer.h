#pragma once

#if !defined(LITE_VERSION)

#include <Arduino.h>
#include <cstdint>
#include <functional>

// Real-time 2.4GHz channel utilization analyzer (on-device TFT UI).
void channel_analyzer_setup();

// Headless session for PC Connect (no TFT / keyboard).
struct CaChannelSample {
    uint8_t ch = 0;
    uint8_t load = 0;
    uint8_t peak = 0;
    int8_t rssi = -128;
};

struct CaApSample {
    char ssid[33] = {};
    uint8_t bssid[6] = {};
    uint8_t channel = 0;
    int8_t rssi = -128;
    char auth[16] = {};
    bool hidden = false;
};

bool caSessionStart();
void caSessionStop();
bool caSessionActive();
// One dwell on the next channel in the 1–11 sweep. Updates AP table.
// abortFn may return true to end the dwell early (e.g. serial stop).
bool caSessionDwell(
    uint16_t dwellMs, CaChannelSample &out, const std::function<bool()> &abortFn = nullptr
);
// Visit all known APs (up to 48).
void caSessionForEachAp(const std::function<void(const CaApSample &)> &fn);

#endif

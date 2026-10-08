#include "pc_connect.h"

#if !defined(LITE_VERSION)
#include "menu/wifi/jam_detect.h"
#include <ArduinoJson.h>

namespace {

bool g_active = false;
uint32_t g_threshold = 10;
uint16_t g_dwell = 100;

} // namespace

bool pcConnectJamActive() { return g_active; }

bool pcConnectJamStart(uint32_t thresholdPerSec) {
    pcConnectStopRadio();
    if (thresholdPerSec < 5) thresholdPerSec = 5;
    if (thresholdPerSec > 250) thresholdPerSec = 250;
    g_threshold = thresholdPerSec;
    if (!jamSessionStart()) return false;
    g_active = true;
    pcConnectSetStatus(PcRadio::Wifi, "jam.detect", 0);
    return true;
}

void pcConnectJamStop() {
    if (!g_active) return;
    jamSessionStop();
    g_active = false;
}

void pcConnectJamTick() {
    if (!g_active) return;

    JamChannelSample sample;
    if (!jamSessionDwell(g_dwell, g_threshold, sample, []() {
            pcConnectPollSerial();
            return !g_active || !jamSessionActive();
        })) {
        return;
    }

    pcConnectSetStatus(PcRadio::Wifi, "jam.detect", sample.ch);

    JsonDocument doc;
    doc["evt"] = "jam";
    doc["ch"] = sample.ch;
    doc["deauth"] = sample.deauthPerSec;
    doc["frames"] = sample.frames;
    doc["rssi"] = sample.rssi;
    doc["alert"] = sample.alert;
    String out;
    serializeJson(doc, out);
    pcConnectEmitJson(out);
}

#endif

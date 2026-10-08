#include "pc_connect.h"

#if !defined(LITE_VERSION)
#include "menu/wifi/channel_analyzer.h"
#include <ArduinoJson.h>
#include <map>

namespace {

bool g_active = false;
uint16_t g_dwell = 350;
struct ApTrack {
    int8_t rssi = -128;
    uint32_t lastEmit = 0;
};
std::map<String, ApTrack> g_apTrack;

String bssidStr(const uint8_t *b) {
    char buf[18];
    snprintf(
        buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]
    );
    return String(buf);
}

} // namespace

bool pcConnectWifiAnalyzerActive() { return g_active; }

bool pcConnectWifiAnalyzerStart(uint16_t dwellMs, uint8_t lockCh) {
    pcConnectStopRadio();
    if (dwellMs < 150) dwellMs = 150;
    if (dwellMs > 1000) dwellMs = 1000;
    if (lockCh > 11) lockCh = 0;
    g_dwell = dwellMs;
    g_apTrack.clear();
    if (!caSessionStart()) return false;
    caSessionSetLock(lockCh);
    g_active = true;
    pcConnectSetStatus(PcRadio::Wifi, "wifi.analyzer", lockCh);
    return true;
}

void pcConnectWifiAnalyzerStop() {
    if (!g_active) return;
    caSessionStop();
    g_active = false;
    g_apTrack.clear();
}

void pcConnectWifiAnalyzerTick() {
    if (!g_active) return;

    CaChannelSample sample;
    if (!caSessionDwell(g_dwell, sample, []() {
            pcConnectPollSerial();
            return !g_active || !caSessionActive();
        })) {
        return;
    }

    pcConnectSetStatus(PcRadio::Wifi, "wifi.analyzer", sample.ch);

    {
        JsonDocument doc;
        doc["evt"] = "analyzer";
        doc["ch"] = sample.ch;
        doc["load"] = sample.load;
        doc["peak"] = sample.peak;
        doc["rssi"] = sample.rssi;
        String out;
        serializeJson(doc, out);
        pcConnectEmitJson(out);
    }

    uint32_t now = millis();
    caSessionForEachAp([&](const CaApSample &ap) {
        String bssid = bssidStr(ap.bssid);
        auto it = g_apTrack.find(bssid);
        bool emit = false;
        if (it == g_apTrack.end()) {
            emit = true;
            g_apTrack[bssid] = {ap.rssi, now};
        } else {
            if (ap.rssi != it->second.rssi && now - it->second.lastEmit >= 1000) {
                emit = true;
                it->second.rssi = ap.rssi;
                it->second.lastEmit = now;
            }
        }
        if (!emit) return;

        JsonDocument doc;
        doc["evt"] = "ap";
        doc["ssid"] = ap.ssid;
        doc["bssid"] = bssid;
        doc["ch"] = ap.channel;
        doc["rssi"] = ap.rssi;
        doc["auth"] = ap.auth;
        doc["hidden"] = ap.hidden;
        String out;
        serializeJson(doc, out);
        pcConnectEmitJson(out);
    });
}

#endif

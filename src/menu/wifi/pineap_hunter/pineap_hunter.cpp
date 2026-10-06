#if !defined(LITE_VERSION)
#include "pineap_hunter.h"

#include "menu/others/audio.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <globals.h>
#include <map>
#include <vector>

// Copyright (c) 2023 Noah Axon — adapted from M5Stick-NEMO PineAP Hunter (GPL-3).

namespace {

struct SsidRecord {
    String essid;
    int32_t rssi = -127;
    uint32_t lastSeen = 0;
};

struct PineRecord {
    String bssid;
    std::vector<SsidRecord> essids;
    uint32_t lastSeen = 0;
};

static const size_t kMaxBssids = 50;
static const uint32_t kScanIntervalMs = 2000;

static void addScanResult(
    std::map<String, std::vector<SsidRecord>> &buffer, const String &bssid, const String &essid, int32_t rssi
) {
    if (essid.length() == 0 || bssid.length() == 0) return;
    auto &list = buffer[bssid];
    for (auto &rec : list) {
        if (rec.essid == essid) {
            rec.rssi = rssi;
            rec.lastSeen = millis();
            return;
        }
    }
    list.push_back({essid, rssi, millis()});
}

static void maintainBuffer(std::map<String, std::vector<SsidRecord>> &buffer) {
    while (buffer.size() > kMaxBssids) {
        // Drop the oldest BSSID by newest SSID lastSeen among its records.
        String oldestKey;
        uint32_t oldest = UINT32_MAX;
        for (const auto &entry : buffer) {
            uint32_t newest = 0;
            for (const auto &r : entry.second) {
                if (r.lastSeen > newest) newest = r.lastSeen;
            }
            if (newest < oldest) {
                oldest = newest;
                oldestKey = entry.first;
            }
        }
        if (oldestKey.length() == 0) break;
        buffer.erase(oldestKey);
    }
}

static std::vector<PineRecord> detectPineaps(
    const std::map<String, std::vector<SsidRecord>> &buffer, int alertSsids
) {
    std::vector<PineRecord> out;
    for (const auto &entry : buffer) {
        if ((int)entry.second.size() < alertSsids) continue;
        PineRecord pine;
        pine.bssid = entry.first;
        pine.essids = entry.second;
        std::sort(pine.essids.begin(), pine.essids.end(), [](const SsidRecord &a, const SsidRecord &b) {
            return a.lastSeen > b.lastSeen;
        });
        pine.lastSeen = pine.essids.empty() ? 0 : pine.essids.front().lastSeen;
        out.push_back(pine);
    }
    std::sort(out.begin(), out.end(), [](const PineRecord &a, const PineRecord &b) {
        return a.essids.size() > b.essids.size();
    });
    return out;
}

static void drawMainList(const std::vector<PineRecord> &pines, int cursor, int alertSsids, int totalScans) {
    drawMainBorderWithTitle("PineAP Hunter");
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    const int x0 = 8;
    int y = 28;
    const int lh = uiLineH(dense) + 1;
    const int bottom = uiFooterY(dense);

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString(
        "thr " + String(alertSsids) + " SSIDs  scans " + String(totalScans) + "  UP/DN", x0, y, 1
    );
    y += lh + 2;

    if (pines.empty()) {
        tft.drawString("No multi-SSID BSSIDs yet", x0, y, 1);
        y += lh;
        tft.drawString("Scanning every 2s...", x0, y, 1);
    } else {
        int rows = (bottom - y) / lh;
        if (rows < 1) rows = 1;
        int start = 0;
        if (cursor >= rows) start = cursor - rows + 1;
        for (int i = start; i < (int)pines.size() && (i - start) < rows; i++) {
            bool sel = (i == cursor);
            tft.setTextColor(
                sel ? kvxConfig.bgColor : kvxConfig.priColor, sel ? kvxConfig.priColor : kvxConfig.bgColor
            );
            String line = pines[i].bssid.substring(0, 17) + "  " + String((int)pines[i].essids.size());
            tft.drawString(line, x0, y, 1);
            y += lh;
        }
    }

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString("Sel open  ESC exit", x0, bottom, 1);
}

static void drawSsidList(const PineRecord &pine, int cursor) {
    drawMainBorderWithTitle("PineAP SSIDs");
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    const int x0 = 8;
    int y = 28;
    const int lh = uiLineH(dense) + 1;
    const int bottom = uiFooterY(dense);

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString(pine.bssid, x0, y, 1);
    y += lh + 2;

    // +1 for Back row
    int total = (int)pine.essids.size() + 1;
    int rows = (bottom - y) / lh;
    if (rows < 1) rows = 1;
    int start = 0;
    if (cursor >= rows) start = cursor - rows + 1;

    for (int i = start; i < total && (i - start) < rows; i++) {
        bool sel = (i == cursor);
        tft.setTextColor(
            sel ? kvxConfig.bgColor : kvxConfig.priColor, sel ? kvxConfig.priColor : kvxConfig.bgColor
        );
        String line;
        if (i == 0) line = "< Back";
        else {
            const auto &r = pine.essids[i - 1];
            line = String(r.rssi) + "  " + r.essid;
            if (line.length() > 28) line = line.substring(0, 28);
        }
        tft.drawString(line, x0, y, 1);
        y += lh;
    }

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString("Sel back  ESC exit", x0, bottom, 1);
}

} // namespace

void pineapHunterMenu() {
    returnToMenu = false;

    ensureWifiPlatform();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);

    int alertSsids = kvxConfig.pineapAlertSsids;
    if (alertSsids < 2) alertSsids = 2;
    if (alertSsids > 50) alertSsids = 50;

    std::map<String, std::vector<SsidRecord>> buffer;
    std::vector<PineRecord> pines;
    int cursor = 0;
    int viewMode = 0; // 0 = main list, 2 = SSID list
    int selectedPine = 0;
    int totalScans = 0;
    uint32_t lastScan = 0;
    uint32_t lastBeepMs = 0;
    size_t lastPineCount = 0;
    bool needsRedraw = true;

    tft.fillScreen(kvxConfig.bgColor);

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) break;

        if (viewMode == 0) {
            if (check(UpPress) && alertSsids < 50) {
                alertSsids++;
                kvxConfig.setPineapAlertSsids(alertSsids);
                needsRedraw = true;
            }
            if (check(DownPress) && alertSsids > 2) {
                alertSsids--;
                kvxConfig.setPineapAlertSsids(alertSsids);
                needsRedraw = true;
            }
            if (check(PrevPress) && !pines.empty()) {
                cursor = (cursor - 1 + (int)pines.size()) % (int)pines.size();
                needsRedraw = true;
            }
            if (check(NextPress) && !pines.empty()) {
                cursor = (cursor + 1) % (int)pines.size();
                needsRedraw = true;
            }
            if (check(SelPress) && !pines.empty()) {
                selectedPine = cursor;
                viewMode = 2;
                cursor = 0;
                needsRedraw = true;
                delay(200);
            }
        } else {
            const PineRecord &pine = pines[selectedPine];
            int total = (int)pine.essids.size() + 1;
            if (check(PrevPress) && total > 0) {
                cursor = (cursor - 1 + total) % total;
                needsRedraw = true;
            }
            if (check(NextPress) && total > 0) {
                cursor = (cursor + 1) % total;
                needsRedraw = true;
            }
            if (check(SelPress)) {
                viewMode = 0;
                cursor = selectedPine;
                needsRedraw = true;
                delay(200);
            }
        }

        uint32_t now = millis();
        if (now - lastScan >= kScanIntervalMs) {
            lastScan = now;
            int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    addScanResult(buffer, WiFi.BSSIDstr(i), WiFi.SSID(i), WiFi.RSSI(i));
                }
                totalScans++;
                maintainBuffer(buffer);
                pines = detectPineaps(buffer, alertSsids);
                if (cursor >= (int)pines.size()) cursor = pines.empty() ? 0 : (int)pines.size() - 1;
                if (selectedPine >= (int)pines.size()) selectedPine = 0;
                if (pines.size() > lastPineCount && (now - lastBeepMs > 500)) {
                    _tone(4000, 50);
                    lastBeepMs = now;
                }
                lastPineCount = pines.size();
                needsRedraw = true;
            }
            WiFi.scanDelete();
        }

        if (needsRedraw) {
            needsRedraw = false;
            if (viewMode == 2 && selectedPine < (int)pines.size()) {
                drawSsidList(pines[selectedPine], cursor);
            } else {
                viewMode = 0;
                drawMainList(pines, cursor, alertSsids, totalScans);
            }
        }

        delay(20);
    }

    wifiDisconnect();
}

#endif

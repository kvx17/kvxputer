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

static void clearBody() {
    const int top = 26;
    tft.fillRect(6, top, tftWidth - 12, tftHeight - top - 6, kvxConfig.bgColor);
}

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

// True when the visible list content changed enough to warrant a redraw.
static bool pinesVisuallyChanged(const std::vector<PineRecord> &a, const std::vector<PineRecord> &b) {
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].bssid != b[i].bssid) return true;
        if (a[i].essids.size() != b[i].essids.size()) return true;
    }
    return false;
}

static void drawMainList(const std::vector<PineRecord> &pines, int cursor, int alertSsids, int totalScans) {
    clearBody();
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    const int x0 = 8;
    int y = 28;
    const int lh = uiLineH(dense) + 1;
    const int bottom = uiFooterY(dense);

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    char head[48];
    snprintf(head, sizeof(head), "thr %2d SSIDs  scans %4d  UP/DN", alertSsids, totalScans);
    tft.drawString(head, x0, y, 1);
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
            char line[40];
            snprintf(
                line,
                sizeof(line),
                "%-17s %2d",
                pines[i].bssid.substring(0, 17).c_str(),
                (int)pines[i].essids.size()
            );
            tft.drawString(line, x0, y, 1);
            y += lh;
        }
    }

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString("Sel open  ESC exit", x0, bottom, 1);
}

static void drawSsidList(const PineRecord &pine, int cursor) {
    clearBody();
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    const int x0 = 8;
    int y = 28;
    const int lh = uiLineH(dense) + 1;
    const int bottom = uiFooterY(dense);

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString(pine.bssid, x0, y, 1);
    y += lh + 2;

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
            char buf[40];
            snprintf(buf, sizeof(buf), "%4d  %s", (int)r.rssi, r.essid.c_str());
            line = buf;
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
    int paintedScans = -1;
    int paintedAlert = -1;
    int paintedCursor = -1;
    int paintedView = -1;
    int paintedSelected = -1;
    uint32_t lastScan = 0;
    uint32_t lastBeepMs = 0;
    size_t lastPineCount = 0;
    bool needsRedraw = true;

    drawMainBorderWithTitle("PineAP Hunter", true);

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
            if (selectedPine >= (int)pines.size()) {
                viewMode = 0;
                cursor = 0;
                needsRedraw = true;
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
                std::vector<PineRecord> next = detectPineaps(buffer, alertSsids);
                if (cursor >= (int)next.size()) cursor = next.empty() ? 0 : (int)next.size() - 1;
                if (selectedPine >= (int)next.size()) selectedPine = 0;
                if (next.size() > lastPineCount && (now - lastBeepMs > 500)) {
                    _tone(4000, 50);
                    lastBeepMs = now;
                }
                lastPineCount = next.size();
                // Only mark dirty when the visible list actually changes.
                if (viewMode == 0) {
                    if (pinesVisuallyChanged(pines, next) || totalScans != paintedScans ||
                        alertSsids != paintedAlert) {
                        needsRedraw = true;
                    }
                } else if (selectedPine < (int)next.size()) {
                    // SSID detail: redraw if that BSSID's SSID set grew/changed.
                    bool sameBssid = selectedPine < (int)pines.size() &&
                                     pines[selectedPine].bssid == next[selectedPine].bssid;
                    size_t oldN = (selectedPine < (int)pines.size()) ? pines[selectedPine].essids.size() : 0;
                    if (!sameBssid || next[selectedPine].essids.size() != oldN) needsRedraw = true;
                }
                pines = std::move(next);
            } else if (totalScans != paintedScans) {
                // Still bump scan counter display occasionally even with 0 APs.
                needsRedraw = true;
            }
            WiFi.scanDelete();
        }

        if (needsRedraw || cursor != paintedCursor || viewMode != paintedView ||
            selectedPine != paintedSelected || alertSsids != paintedAlert) {
            needsRedraw = false;
            if (viewMode == 2 && selectedPine < (int)pines.size()) {
                drawSsidList(pines[selectedPine], cursor);
            } else {
                viewMode = 0;
                drawMainList(pines, cursor, alertSsids, totalScans);
            }
            paintedScans = totalScans;
            paintedAlert = alertSsids;
            paintedCursor = cursor;
            paintedView = viewMode;
            paintedSelected = selectedPine;
        }

        delay(40);
    }

    wifiDisconnect();
}

#endif

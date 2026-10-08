#if !defined(LITE_VERSION)
#include "ble_hunter.h"
#include "root/hal/ble/ble_backend.h"

#include "menu/others/audio.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/scanner_list.h"
#include <Arduino.h>
#include <globals.h>
#include <set>
#include <vector>

// Copyright (c) 2023 Noah Axon — adapted from M5Stick-NEMO BLE Hunter (GPL-3).

namespace {

static const uint32_t kWindowMs = 10000;
static const size_t kMaxUnique = 50;

static void clearBody() {
    const int top = 26;
    tft.fillRect(6, top, tftWidth - 12, tftHeight - top - 6, kvxConfig.bgColor);
}

static void drawRssiBar(int x, int y, int w, int h, int rssi, int floorDbm) {
    tft.drawRect(x, y, w, h, kvxConfig.priColor);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, kvxConfig.bgColor);
    int span = 0 - floorDbm;
    if (span < 1) span = 1;
    int level = rssi - floorDbm;
    if (level < 0) level = 0;
    if (level > span) level = span;
    int fill = (w - 2) * level / span;
    if (fill > 0) tft.fillRect(x + 1, y + 1, fill, h - 2, kvxConfig.priColor);
}

static void drawHud(
    uint32_t advCount, size_t uniqueCount, int alertPkts, int lastRssi, int rssiFloor, bool alertsPaused,
    uint32_t windowLeftSec
) {
    clearBody();
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    const int x0 = 8;
    int y = 28;
    const int lh = uiLineH(dense) + 2;

    bool over = alertPkts > 0 && (int)advCount >= alertPkts;
    uint16_t sc = over ? TFT_RED : (alertsPaused ? TFT_YELLOW : TFT_GREEN);
    const int bannerH = uiLineH(dense) + 10;
    tft.fillRect(x0, y, tftWidth - 2 * x0, bannerH, sc);
    tft.setTextColor(TFT_BLACK, sc);
    String banner = alertsPaused ? "alerts paused (Sel)" : (over ? "! BLE spam threshold" : "monitoring");
    tft.drawCentreString(banner, tftWidth / 2, y + 3, 1);
    y += bannerH + 6;

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    char line[40];
    snprintf(line, sizeof(line), "Adverts: %5lu / %3d", (unsigned long)advCount, alertPkts);
    tft.drawString(line, x0, y, 1);
    y += lh;
    snprintf(line, sizeof(line), "Unique:  %5d / %3d", (int)uniqueCount, (int)kMaxUnique);
    tft.drawString(line, x0, y, 1);
    y += lh;
    snprintf(line, sizeof(line), "Window:  %2lus left", (unsigned long)windowLeftSec);
    tft.drawString(line, x0, y, 1);
    y += lh + 4;

    snprintf(line, sizeof(line), "RSSI %4ddBm", lastRssi);
    tft.drawString(line, x0, y, 1);
    int barX = x0 + tft.textWidth("RSSI -000dBm") + 6;
    int barW = tftWidth - barX - x0;
    if (barW > 20) drawRssiBar(barX, y, barW, uiLineH(dense), lastRssi, rssiFloor);
    y += lh + 4;

    tft.drawString("UP/DN thr  Prev/Nxt floor", x0, y, 1);
    y += lh;
    tft.drawString("Sel pause  ESC exit", x0, y, 1);
}

} // namespace

void bleHunterMenu() {
    if (!bleNimbleProfileOrExplain("BLE Hunter")) return;
    returnToMenu = false;

    NimBLEScan *scan = scannerBleStart();
    if (!scan) {
        displayError("BLE scan failed", true);
        return;
    }

    int alertPkts = kvxConfig.bleHunterAlertPkts;
    if (alertPkts < 0) alertPkts = 0;
    if (alertPkts > 100) alertPkts = 100;
    int rssiFloor = kvxConfig.bleHunterRssiFloor;
    if (rssiFloor < -100) rssiFloor = -100;
    if (rssiFloor > -10) rssiFloor = -10;

    uint32_t windowStart = millis();
    uint32_t advInWindow = 0;
    std::set<String> uniqueMacs;
    int lastRssi = -90;
    bool alertsPaused = false;
    uint32_t lastBeepMs = 0;

    // Painted snapshot — only redraw when visible state changes.
    uint32_t paintedAdv = UINT32_MAX;
    size_t paintedUnique = SIZE_MAX;
    int paintedAlert = -1;
    int paintedRssi = 999;
    int paintedFloor = 999;
    bool paintedPaused = false;
    uint32_t paintedLeftSec = UINT32_MAX;

    drawMainBorderWithTitle("BLE Hunter", true);

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) break;

        bool controlsChanged = false;
        if (check(SelPress)) {
            alertsPaused = !alertsPaused;
            controlsChanged = true;
            delay(200);
        }
        if (check(UpPress) && alertPkts < 100) {
            alertPkts++;
            kvxConfig.setBleHunterAlertPkts(alertPkts);
            controlsChanged = true;
        }
        if (check(DownPress) && alertPkts > 0) {
            alertPkts--;
            kvxConfig.setBleHunterAlertPkts(alertPkts);
            controlsChanged = true;
        }
        if (check(PrevPress) && rssiFloor > -100) {
            rssiFloor -= 5;
            kvxConfig.setBleHunterRssiFloor(rssiFloor);
            controlsChanged = true;
        }
        if (check(NextPress) && rssiFloor < -10) {
            rssiFloor += 5;
            kvxConfig.setBleHunterRssiFloor(rssiFloor);
            controlsChanged = true;
        }

        scannerBleKeepAlive(scan);
        const auto batch = scannerBleTakeInbox(scan);
        bool dataChanged = !batch.empty();
        for (const auto &d : batch) {
            advInWindow++;
            lastRssi = d.rssi;
            if (uniqueMacs.size() < kMaxUnique) uniqueMacs.insert(d.mac);
        }

        uint32_t now = millis();
        if (now - windowStart >= kWindowMs) {
            windowStart = now;
            advInWindow = 0;
            uniqueMacs.clear();
            dataChanged = true;
        }

        bool over = (int)advInWindow >= alertPkts && alertPkts > 0;
        if (over && !alertsPaused && (now - lastBeepMs > 400)) {
            _tone(4000, 50);
            lastBeepMs = now;
        }

        uint32_t leftSec = (kWindowMs - (now - windowStart) + 999) / 1000;
        bool needPaint = controlsChanged || dataChanged || advInWindow != paintedAdv ||
                         uniqueMacs.size() != paintedUnique || alertPkts != paintedAlert ||
                         lastRssi != paintedRssi || rssiFloor != paintedFloor ||
                         alertsPaused != paintedPaused || leftSec != paintedLeftSec;

        if (needPaint) {
            drawHud(advInWindow, uniqueMacs.size(), alertPkts, lastRssi, rssiFloor, alertsPaused, leftSec);
            paintedAdv = advInWindow;
            paintedUnique = uniqueMacs.size();
            paintedAlert = alertPkts;
            paintedRssi = lastRssi;
            paintedFloor = rssiFloor;
            paintedPaused = alertsPaused;
            paintedLeftSec = leftSec;
        }

        delay(40);
    }

    scannerBleTeardown(true);
}

#endif

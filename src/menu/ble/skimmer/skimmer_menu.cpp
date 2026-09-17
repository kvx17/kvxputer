/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * BLE scan for common skimmer GAP names and MAC prefixes.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "skimmer.h"
#if defined(EVIL_EXTENSIONS)
#include "menu/ble/ble_common.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <globals.h>

namespace {

const char *kBadNames[] = {"HC-03", "HC-05", "HC-06", "HC-08", "BT04-A", "BT05"};
const char *kBadMac[] = {"00:11:22", "00:18:e4", "20:16:04"};

bool looksLikeSkimmer(const String &name, const String &addr) {
    String n = name;
    n.trim();
    for (auto *bad : kBadNames) {
        if (n.equalsIgnoreCase(bad)) return true;
    }
    String a = addr;
    a.toLowerCase();
    for (auto *pfx : kBadMac) {
        if (a.startsWith(pfx)) return true;
    }
    return false;
}

} // namespace

void skimmerMenu() {
    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(1349);
    scan->setWindow(449);
    scan->setDuplicateFilter(false);

    drawMainBorderWithTitle("Skimmer Detector");
    tft.drawString("Scanning... ESC stop", 10, tftHeight - 20);
    EscPress = false;
    bool hit = false;
    String hitMsg;
    unsigned long lastUi = 0;

    scan->start(0, false);
    while (!check(EscPress) && !returnToMenu && !hit) {
        NimBLEScanResults r = scan->getResults();
        int n = r.getCount();
        for (int i = 0; i < n; i++) {
            const NimBLEAdvertisedDevice *d = r.getDevice(i);
            if (!d) continue;
            String name = String(d->getName().c_str());
            String addr = String(d->getAddress().toString().c_str());
            int rssi = d->getRSSI();
            bool bad = looksLikeSkimmer(name, addr);
            if (millis() - lastUi > 400) {
                lastUi = millis();
                tft.fillRect(8, 36, tftWidth - 16, 56, kvxConfig.bgColor);
                tft.setTextColor(bad ? TFT_RED : kvxConfig.priColor, kvxConfig.bgColor);
                tft.drawString(name.length() ? name : addr, 8, 40);
                tft.drawString("RSSI " + String(rssi), 8, 56);
                tft.drawString(bad ? "Probable skimmer" : "No match", 8, 72);
                tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            }
            if (bad) {
                hit = true;
                hitMsg = (name.length() ? name : addr) + "\n" + addr + "\nRSSI " + String(rssi);
                break;
            }
        }
        delay(80);
    }
    scan->stop();
    NimBLEDevice::deinit(true);
    if (hit) displayError("Skimmer?\n" + hitMsg, true);
    else displayInfo("Scan stopped", true);
}
#endif

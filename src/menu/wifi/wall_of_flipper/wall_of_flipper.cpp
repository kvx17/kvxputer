/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Wall Of Flipper: BLE scan for Flipper Zero advertisements.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#if defined(EVIL_EXTENSIONS)
#include "wall_of_flipper.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <set>
#include <vector>

namespace {

bool alreadyLogged(FS &fs, const String &path, const String &mac) {
    File f = fs.open(path, FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.indexOf(mac) >= 0) {
            f.close();
            return true;
        }
    }
    f.close();
    return false;
}

} // namespace

void wallOfFlipperMenu() {
    FS *fs = nullptr;
    getFsStorage(fs);
    String logPath;
    if (fs) {
        kvx::paths::ensureDir(*fs, kvx::paths::WIFI_WOF);
        logPath = String(kvx::paths::WIFI_WOF) + "/WoF.txt";
    }

    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    std::set<String> seen;
    int found = 0;
    drawMainBorderWithTitle("Wall Of Flipper");
    tft.drawString("Scanning BLE...", 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        NimBLEScanResults results = scan->getResults(1000, false);
        int y = 56;
        tft.fillRect(10, 56, tftWidth - 20, tftHeight - 80, kvxConfig.bgColor);
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *dev = results.getDevice(i);
            String name = String(dev->getName().c_str());
            String mac = String(dev->getAddress().toString().c_str());
            bool isFlipper = name.indexOf("Flipper") >= 0 || name.indexOf("flipper") >= 0;
            if (!isFlipper) continue;
            if (seen.find(mac) == seen.end()) {
                seen.insert(mac);
                found++;
                if (fs && !alreadyLogged(*fs, logPath, mac)) {
                    File out = fs->open(logPath, FILE_APPEND);
                    if (!out) out = fs->open(logPath, FILE_WRITE);
                    if (out) {
                        out.println(name + " - " + mac + " - " + String(dev->getRSSI()) + " dBm");
                        out.close();
                    }
                }
            }
            if (y < tftHeight - 28) {
                tft.drawString(name + " " + String(dev->getRSSI()), 10, y);
                y += 12;
            }
        }
        tft.fillRect(10, 40, tftWidth - 20, 14, kvxConfig.bgColor);
        tft.drawString("Flippers: " + String(found), 10, 40);
        scan->clearResults();
    }

    scan->stop();
    NimBLEDevice::deinit(true);
    displayInfo("Logged " + String(found) + " Flipper(s)", true);
}

#endif

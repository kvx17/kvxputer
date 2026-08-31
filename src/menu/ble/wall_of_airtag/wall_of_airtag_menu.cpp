/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Wall of AirTag: BLE scan for Apple Find My / AirTag frames.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "wall_of_airtag.h"
#if defined(EVIL_EXTENSIONS)
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <set>

void wallOfAirtagMenu() {
    FS *fs = nullptr;
    getFsStorage(fs);
    String logPath;
    if (fs) {
        kvx::paths::ensureDir(*fs, kvx::paths::BLE_AIRTAGS);
        logPath = String(kvx::paths::BLE_AIRTAGS) + "/airtags.txt";
    }
    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);

    std::set<String> seen;
    int found = 0;
    drawMainBorderWithTitle("Wall Of Airtag");
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        NimBLEScanResults results = scan->getResults(1000, false);
        int y = 56;
        tft.fillRect(10, 40, tftWidth - 20, tftHeight - 70, kvxConfig.bgColor);
        tft.drawString("AirTags: " + String(found), 10, 40);
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *dev = results.getDevice(i);
            bool hit = false;
            if (dev->haveManufacturerData()) {
                std::string md = dev->getManufacturerData();
                if (md.size() >= 2) {
                    uint16_t cid = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
                    if (cid == 0x004C && md.size() >= 3 && ((uint8_t)md[2] == 0x12 || (uint8_t)md[2] == 0x07))
                        hit = true;
                }
            }
            String name = String(dev->getName().c_str());
            if (name.indexOf("AirTag") >= 0 || name.indexOf("FindMy") >= 0) hit = true;
            if (!hit) continue;
            String mac = String(dev->getAddress().toString().c_str());
            if (seen.insert(mac).second) {
                found++;
                if (fs) {
                    File out = fs->open(logPath, FILE_APPEND);
                    if (!out) out = fs->open(logPath, FILE_WRITE);
                    if (out) {
                        out.println(mac + " " + String(dev->getRSSI()) + " " + name);
                        out.close();
                    }
                }
            }
            if (y < tftHeight - 28) {
                tft.drawString(mac + " " + String(dev->getRSSI()), 10, y);
                y += 12;
            }
        }
        scan->clearResults();
    }
    scan->stop();
    NimBLEDevice::deinit(true);
    displayInfo("Logged " + String(found) + " AirTag(s)", true);
}
#endif

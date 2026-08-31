/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * BLE name flood: rotate advertised GAP names.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "name_flood.h"
#if defined(EVIL_EXTENSIONS)
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <vector>

void nameFloodMenu() {
    std::vector<String> names = {
        "kvxputer", "FreeVirusWiFi", "DoNotConnect", "NSA_Listener", "SkynetNode42", "PairedYouLOL"
    };
    FS *fs = nullptr;
    if (getFsStorage(fs) && fs) {
        File f = fs->open("/support_files/ble/names.txt", FILE_READ);
        if (f) {
            names.clear();
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length()) names.push_back(line);
            }
            f.close();
        }
    }
    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init(names[0].c_str());
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setName(names[0].c_str());
    adv->start();

    size_t idx = 0;
    unsigned long last = 0;
    unsigned long count = 0;
    drawMainBorderWithTitle("BLE Name Flood");
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - last > 80) {
            last = millis();
            idx = (idx + 1) % names.size();
            adv->stop();
            adv->setName(names[idx].c_str());
            adv->start();
            count++;
            tft.fillRect(10, 40, tftWidth - 20, 32, kvxConfig.bgColor);
            tft.drawString(names[idx], 10, 40);
            tft.drawString("Ads: " + String((unsigned)count), 10, 56);
        }
        delay(10);
    }
    adv->stop();
    NimBLEDevice::deinit(true);
}
#endif

/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * FindMyEvil: advertise Find My-style payloads (random or keys file).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "findmy.h"
#if defined(EVIL_EXTENSIONS)
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <vector>

void findMyMenu() {
    std::vector<String> keys;
    FS *fs = nullptr;
    if (getFsStorage(fs) && fs) {
        File f = fs->open(kvx::paths::BLE_FINDMY_KEYS, FILE_READ);
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() >= 8) keys.push_back(line);
            }
            f.close();
        }
    }

    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init("");
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();

    auto buildPayload = [&](uint8_t *out, size_t &len) {
        out[0] = 0x4C;
        out[1] = 0x00;
        out[2] = 0x12;
        out[3] = 0x19;
        for (int i = 4; i < 29; i++) out[i] = random(256);
        len = 29;
        if (!keys.empty()) {
            String k = keys[random(keys.size())];
            for (int i = 0; i < 12 && i * 2 + 1 < (int)k.length(); i++) {
                char buf[3] = {k[i * 2], k[i * 2 + 1], 0};
                out[4 + i] = (uint8_t)strtoul(buf, nullptr, 16);
            }
        }
    };

    uint8_t payload[32];
    size_t plen = 0;
    unsigned long last = 0, count = 0;
    drawMainBorderWithTitle("FindMyEvil");
    tft.drawString(keys.empty() ? "Random lab keys" : "SD keys loaded", 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        if (millis() - last > 200) {
            last = millis();
            buildPayload(payload, plen);
            adv->stop();
            NimBLEAdvertisementData data;
            data.setManufacturerData(payload, plen);
            adv->setAdvertisementData(data);
            adv->start();
            count++;
            tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Adv: " + String((unsigned)count), 10, 56);
        }
        delay(20);
    }
    adv->stop();
    NimBLEDevice::deinit(true);
}
#endif

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

struct FlipperHit {
    String mac;
    String name;
    int rssi;
};

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

bool payloadHasUuid16(const uint8_t *p, size_t len, uint16_t uuid) {
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t dlen = p[i];
        if (dlen == 0) break;
        if (i + 1 + dlen > len) break;
        uint8_t type = p[i + 1];
        if (type == 0x02 || type == 0x03) {
            for (size_t j = 2; j + 1 <= (size_t)dlen; j += 2) {
                uint16_t u = p[i + j] | ((uint16_t)p[i + j + 1] << 8);
                if (u == uuid) return true;
            }
        }
        i += (size_t)dlen + 1;
    }
    return false;
}

bool looksLikeFlipper(const NimBLEAdvertisedDevice *dev) {
    if (!dev) return false;
    String name = String(dev->getName().c_str());
    if (name.indexOf("Flipper") >= 0 || name.indexOf("flipper") >= 0) return true;

    const uint16_t ids[] = {0x3080, 0x3081, 0x3082, 0x3083};
    for (uint16_t id : ids) {
        if (dev->isAdvertisingService(NimBLEUUID((uint16_t)id))) return true;
    }
    if (dev->haveServiceUUID()) {
        NimBLEUUID u = dev->getServiceUUID();
        for (uint16_t id : ids) {
            if (u == NimBLEUUID((uint16_t)id)) return true;
        }
    }

    const std::vector<uint8_t> &payload = dev->getPayload();
    if (!payload.empty()) {
        for (uint16_t id : ids) {
            if (payloadHasUuid16(payload.data(), payload.size(), id)) return true;
        }
    }
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
    scan->setDuplicateFilter(false);

    std::vector<FlipperHit> hits;
    std::set<String> seen;
    drawMainBorderWithTitle("Wall Of Flipper");
    tft.setTextSize(FP);
    tft.drawString("ESC to stop", 10, tftHeight - 16);
    EscPress = false;

    scan->start(0, false);
    unsigned long lastUi = 0;
    int lastCount = -1;
    while (!check(EscPress) && !returnToMenu && !forceHome) {
        NimBLEScanResults results = scan->getResults();
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *dev = results.getDevice(i);
            if (!looksLikeFlipper(dev)) continue;
            String mac = String(dev->getAddress().toString().c_str());
            String name = String(dev->getName().c_str());
            int rssi = dev->getRSSI();
            if (seen.insert(mac).second) {
                hits.push_back({mac, name, rssi});
                if (fs && !alreadyLogged(*fs, logPath, mac)) {
                    File out = fs->open(logPath, FILE_APPEND);
                    if (!out) out = fs->open(logPath, FILE_WRITE);
                    if (out) {
                        out.println(
                            (name.length() ? name : String("Flipper")) + " - " + mac + " - " + String(rssi) +
                            " dBm"
                        );
                        out.close();
                    }
                }
            } else {
                for (auto &h : hits) {
                    if (h.mac == mac) {
                        h.rssi = rssi;
                        if (name.length()) h.name = name;
                        break;
                    }
                }
            }
        }

        if (millis() - lastUi > 250 || (int)hits.size() != lastCount) {
            lastUi = millis();
            lastCount = (int)hits.size();
            tft.fillRect(8, 28, tftWidth - 16, tftHeight - 48, kvxConfig.bgColor);
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString("Flippers: " + String((int)hits.size()), 10, 30);
            int y = 46;
            int nchars = max(1, (tftWidth - 16) / (FP * LW));
            for (size_t i = 0; i < hits.size() && y < tftHeight - 22; i++) {
                String label = hits[i].name.length() ? hits[i].name : hits[i].mac;
                String line = label + " " + String(hits[i].rssi);
                if ((int)line.length() > nchars) line = line.substring(0, nchars);
                tft.drawString(line, 10, y);
                y += 12;
            }
        }
        delay(40);
    }

    scan->stop();
    NimBLEDevice::deinit(true);
    displayInfo("Logged " + String((int)hits.size()) + " Flipper(s)", true);
}

#endif

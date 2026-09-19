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
#include <algorithm>
#include <math.h>
#include <set>
#include <vector>

namespace {

struct AirTagHit {
    String mac;
    int rssi = 0;
    uint8_t battery = 0xFF;
    bool separated = false;
    bool randomAddr = true;
    String keyPrefix;
    int hits = 0;
    unsigned long firstSeen = 0;
    unsigned long lastSeen = 0;
};

const char *batteryLabel(uint8_t batt) {
    switch (batt) {
        case 0: return "FULL";
        case 1: return "MED";
        case 2: return "LOW";
        case 3: return "CRIT";
        default: return "?";
    }
}

float approxMeters(int rssi) {
    float d = powf(10.0f, (-59.0f - (float)rssi) / 20.0f);
    if (d < 0.1f) d = 0.1f;
    if (d > 99.0f) d = 99.0f;
    return d;
}

String shortMac(const String &mac) {
    if (mac.length() < 14) return mac;
    return mac.substring(0, 5) + ".." + mac.substring(mac.length() - 5);
}

bool parseFindMy(const NimBLEAdvertisedDevice *dev, AirTagHit &out) {
    if (!dev || !dev->haveManufacturerData()) return false;
    std::string md = dev->getManufacturerData();
    if (md.size() < 5) return false;
    uint16_t cid = (uint8_t)md[0] | ((uint16_t)(uint8_t)md[1] << 8);
    if (cid != 0x004C) return false;
    if ((uint8_t)md[2] != 0x12) return false;

    uint8_t status = (uint8_t)md[4];
    out.battery = (status >> 6) & 0x03;
    out.separated = (status & 0x20) != 0;
    if (md.size() >= 8) {
        char buf[7];
        snprintf(buf, sizeof(buf), "%02X%02X%02X", (uint8_t)md[5], (uint8_t)md[6], (uint8_t)md[7]);
        out.keyPrefix = String(buf);
    }
    return true;
}

} // namespace

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
    scan->setInterval(100);
    scan->setWindow(99);
    scan->setDuplicateFilter(false);

    std::vector<AirTagHit> hits;
    std::set<String> logged;
    std::set<String> audioSeen;
    int scroll = 0;

    drawMainBorderWithTitle("Wall Of Airtag");
    tft.setTextSize(FP);
    tft.drawString("ESC stop  ;/. scroll", 8, tftHeight - 14);
    EscPress = false;

    scan->start(0, false);
    unsigned long lastUi = 0;
    while (!check(EscPress) && !returnToMenu && !forceHome) {
        if (check(UpPress) && scroll > 0) scroll--;
        if (check(DownPress)) scroll++;

        NimBLEScanResults results = scan->getResults();
        for (int i = 0; i < results.getCount(); i++) {
            const NimBLEAdvertisedDevice *dev = results.getDevice(i);
            if (!dev) continue;
            if (dev->haveManufacturerData()) {
                std::string md = dev->getManufacturerData();
                if (md.size() >= 3) {
                    uint16_t cid = (uint8_t)md[0] | ((uint16_t)(uint8_t)md[1] << 8);
                    if (cid == 0x004C && (uint8_t)md[2] == 0x07) {
                        audioSeen.insert(String(dev->getAddress().toString().c_str()));
                    }
                }
            }
            AirTagHit parsed;
            if (!parseFindMy(dev, parsed)) continue;
            String mac = String(dev->getAddress().toString().c_str());
            int rssi = dev->getRSSI();
            bool found = false;
            for (auto &h : hits) {
                if (h.mac == mac) {
                    h.rssi = rssi;
                    h.battery = parsed.battery;
                    h.separated = parsed.separated;
                    if (parsed.keyPrefix.length()) h.keyPrefix = parsed.keyPrefix;
                    h.hits++;
                    h.lastSeen = millis();
                    found = true;
                    break;
                }
            }
            if (!found) {
                parsed.mac = mac;
                parsed.rssi = rssi;
                parsed.randomAddr = (dev->getAddressType() != BLE_ADDR_PUBLIC);
                parsed.hits = 1;
                parsed.firstSeen = millis();
                parsed.lastSeen = parsed.firstSeen;
                hits.push_back(parsed);
                if (fs && logged.insert(mac).second) {
                    File out = fs->open(logPath, FILE_APPEND);
                    if (!out) out = fs->open(logPath, FILE_WRITE);
                    if (out) {
                        out.println(
                            mac + " " + String(rssi) + " batt=" + batteryLabel(parsed.battery) +
                            (parsed.separated ? " sep" : " near")
                        );
                        out.close();
                    }
                }
            }
        }

        std::sort(hits.begin(), hits.end(), [](const AirTagHit &a, const AirTagHit &b) {
            return a.rssi > b.rssi;
        });

        const int rowH = 2 * uiLineH(FP) + 4;
        const int startY = 44;
        const int visible = max(1, (uiFooterY(FP) - startY) / rowH);
        if (scroll > (int)hits.size() - visible) scroll = max(0, (int)hits.size() - visible);
        if (scroll < 0) scroll = 0;

        if (millis() - lastUi > 200) {
            lastUi = millis();
            tft.fillRect(6, 26, tftWidth - 12, tftHeight - 42, kvxConfig.bgColor);
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            String hdr = "AirTags " + String((int)hits.size());
            if (audioSeen.size()) hdr += "  audio " + String((int)audioSeen.size());
            tft.drawString(hdr, 8, 28);

            for (int n = 0; n < visible; n++) {
                int idx = scroll + n;
                if (idx >= (int)hits.size()) break;
                const AirTagHit &h = hits[idx];
                int y = startY + n * rowH;
                tft.setTextColor(DEFAULT_SECCOLOR, kvxConfig.bgColor);
                String l1 = String(idx + 1) + " " + shortMac(h.mac) + "  " + String(h.rssi) + "dBm ~" +
                            String(approxMeters(h.rssi), 1) + "m";
                int nchars = max(1, (tftWidth - 14) / uiCharW(FP));
                if ((int)l1.length() > nchars) l1 = l1.substring(0, nchars);
                tft.drawString(l1, 8, y);
                tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                String l2 = String("   batt ") + batteryLabel(h.battery) +
                            (h.separated ? "  sep" : "  near") + (h.randomAddr ? "  rnd" : "  pub");
                if (h.keyPrefix.length()) l2 += "  " + h.keyPrefix;
                if ((int)l2.length() > nchars) l2 = l2.substring(0, nchars);
                tft.drawString(l2, 8, y + uiLineH(FP) + 2);
            }
        }
        delay(30);
    }
    scan->stop();
    NimBLEDevice::deinit(true);
    displayInfo("Logged " + String((int)hits.size()) + " AirTag(s)", true);
}
#endif

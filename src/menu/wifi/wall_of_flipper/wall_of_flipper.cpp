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
#include "root/ui/scanner_list.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <set>
#include <vector>

namespace {

struct FlipperHit {
    String mac;
    String name;
    int rssi = 0;
    String addrType;
    String services;
    String txPower;
    String appearance;
    String mfgHex;
    String payloadHex;
    unsigned long firstSeen = 0;
    unsigned long lastSeen = 0;
};

String shortMac(const String &mac) {
    if (mac.length() < 14) return mac;
    return mac.substring(0, 5) + ".." + mac.substring(mac.length() - 5);
}

String bytesToHex(const uint8_t *p, size_t len, size_t maxBytes = 12) {
    String out;
    size_t n = min(len, maxBytes);
    for (size_t i = 0; i < n; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", p[i]);
        out += b;
        if (i + 1 < n) out += " ";
    }
    if (len > maxBytes) out += "...";
    return out;
}

String addrTypeName(uint8_t t) {
    switch (t) {
        case BLE_ADDR_PUBLIC: return "public";
        case BLE_ADDR_RANDOM: return "random";
        case BLE_ADDR_PUBLIC_ID: return "public-id";
        case BLE_ADDR_RANDOM_ID: return "random-id";
        default: return "other";
    }
}

void fillAdvertExtras(FlipperHit &h, const ScannerAdvSnap &dev) {
    h.addrType = addrTypeName(dev.addrType);
    if (dev.haveTXPower) h.txPower = String(dev.txPower) + " dBm";
    if (dev.haveAppearance) h.appearance = String(dev.appearance);
    if (dev.haveMfg && !dev.mfg.empty()) {
        h.mfgHex = bytesToHex((const uint8_t *)dev.mfg.data(), dev.mfg.size());
    }
    if (!dev.payload.empty()) h.payloadHex = bytesToHex(dev.payload.data(), dev.payload.size());
    h.services = dev.serviceUUID;
}

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

bool looksLikeFlipper(const ScannerAdvSnap &dev) {
    if (dev.name.indexOf("Flipper") >= 0 || dev.name.indexOf("flipper") >= 0) return true;

    const uint16_t ids[] = {0x3080, 0x3081, 0x3082, 0x3083};
    if (dev.haveServiceUUID && dev.serviceUUID.length()) {
        for (uint16_t id : ids) {
            NimBLEUUID u((uint16_t)id);
            if (dev.serviceUUID.equalsIgnoreCase(String(u.toString().c_str()))) return true;
            char hex[5];
            snprintf(hex, sizeof(hex), "%04X", id);
            if (dev.serviceUUID.indexOf(hex) >= 0) return true;
        }
    }
    if (!dev.payload.empty()) {
        for (uint16_t id : ids) {
            if (payloadHasUuid16(dev.payload.data(), dev.payload.size(), id)) return true;
        }
    }
    return false;
}

std::vector<String> flipperRowLabels(const std::vector<FlipperHit> &hits) {
    std::vector<String> rows;
    rows.reserve(hits.size());
    for (const auto &h : hits) {
        String label = h.name.length() ? h.name : shortMac(h.mac);
        label += "  " + String(h.rssi) + "dBm";
        rows.push_back(label);
    }
    return rows;
}

std::vector<ScannerDetailField> flipperDetail(const FlipperHit &h) {
    std::vector<ScannerDetailField> f;
    f.push_back({"MAC", h.mac});
    f.push_back({"Name", h.name.length() ? h.name : "<none>"});
    f.push_back({"RSSI", String(h.rssi) + " dBm"});
    f.push_back({"Addr type", h.addrType.length() ? h.addrType : "?"});
    if (h.services.length()) f.push_back({"Service", h.services});
    if (h.txPower.length()) f.push_back({"TX power", h.txPower});
    if (h.appearance.length()) f.push_back({"Appearance", h.appearance});
    if (h.mfgHex.length()) f.push_back({"Mfg data", h.mfgHex});
    if (h.payloadHex.length()) f.push_back({"Payload", h.payloadHex});
    f.push_back({"First seen", String(h.firstSeen) + " ms"});
    f.push_back({"Last seen", String(h.lastSeen) + " ms"});
    return f;
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

    NimBLEScan *scan = scannerBleStart();
    if (!scan) {
        displayError("BLE scan failed", true);
        return;
    }

    std::vector<FlipperHit> hits;
    std::set<String> seen;

    ScannerListState ui;
    scannerListBegin(ui, "Wall Of Flipper", "scanning");

    auto ingest = [&]() {
        scannerBleKeepAlive(scan);
        const auto batch = scannerBleTakeInbox(scan);
        std::vector<size_t> newIdx;
        for (const auto &dev : batch) {
            if (!looksLikeFlipper(dev)) continue;
            const String &mac = dev.mac;
            const String &name = dev.name;
            int rssi = dev.rssi;
            if (seen.insert(mac).second) {
                FlipperHit h;
                h.mac = mac;
                h.name = name;
                h.rssi = rssi;
                h.firstSeen = millis();
                h.lastSeen = h.firstSeen;
                fillAdvertExtras(h, dev);
                hits.push_back(h);
                newIdx.push_back(hits.size() - 1);
            } else {
                for (auto &h : hits) {
                    if (h.mac == mac) {
                        h.rssi = rssi;
                        h.lastSeen = millis();
                        if (name.length() && name != h.name) h.name = name;
                        break;
                    }
                }
            }
        }
        if (fs) {
            for (size_t idx : newIdx) {
                const FlipperHit &h = hits[idx];
                if (alreadyLogged(*fs, logPath, h.mac)) continue;
                File out = fs->open(logPath, FILE_APPEND);
                if (!out) out = fs->open(logPath, FILE_WRITE);
                if (out) {
                    out.println(
                        (h.name.length() ? h.name : String("Flipper")) + " - " + h.mac + " - " +
                        String(h.rssi) + " dBm"
                    );
                    out.close();
                }
            }
        }
    };

    unsigned long lastPaint = 0;
    while (true) {
        ScannerListResult r = scannerListPoll(ui);
        if (r == SCANNER_LIST_EXIT) break;
        if (r == SCANNER_LIST_DETAIL && ui.cursor >= 0 && ui.cursor < (int)hits.size()) {
            FlipperHit snap = hits[ui.cursor];
            String detailTitle = snap.name.length() ? snap.name : "RESULT DETAILS";
            scannerListShowDetail(detailTitle.c_str(), flipperDetail(snap), [&]() { scannerBleKeepAlive(scan); });
            scannerListRefresh(ui);
            lastPaint = 0;
            continue;
        }
        ingest();
        if (millis() - lastPaint > 220) {
            lastPaint = millis();
            String st = String(scannerBleAdvCount()) + " adv";
            if (!hits.empty()) st += "  " + String((int)hits.size()) + " hit";
            scannerListSetStatus(ui, st.c_str());
            scannerListSetRows(ui, flipperRowLabels(hits));
        }
        delay(20);
    }

    scannerBleTeardown(true);
    scannerListEnd();
    displayInfo("Logged " + String((int)hits.size()) + " Flipper(s)", true);
}

#endif

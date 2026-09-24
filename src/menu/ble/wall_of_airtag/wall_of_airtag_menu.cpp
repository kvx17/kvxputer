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
#include "root/ui/scanner_list.h"
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

bool parseFindMy(const ScannerAdvSnap &dev, AirTagHit &out) {
    if (!dev.haveMfg || dev.mfg.size() < 5) return false;
    const std::string &md = dev.mfg;
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

std::vector<String> airtagRowLabels(const std::vector<AirTagHit> &hits) {
    std::vector<String> rows;
    rows.reserve(hits.size());
    for (const auto &h : hits) {
        String label = shortMac(h.mac) + "  " + String(h.rssi) + "dBm ~" + String(approxMeters(h.rssi), 1) + "m";
        rows.push_back(label);
    }
    return rows;
}

std::vector<ScannerDetailField> airtagDetail(const AirTagHit &h) {
    std::vector<ScannerDetailField> f;
    f.push_back({"MAC", h.mac});
    f.push_back({"RSSI", String(h.rssi) + " dBm"});
    f.push_back({"Approx", String(approxMeters(h.rssi), 1) + " m"});
    f.push_back({"Battery", batteryLabel(h.battery)});
    f.push_back({"Status", h.separated ? "separated" : "near"});
    f.push_back({"Addr", h.randomAddr ? "random" : "public"});
    if (h.keyPrefix.length()) f.push_back({"Key prefix", h.keyPrefix});
    f.push_back({"Hits", String(h.hits)});
    f.push_back({"First seen", String(h.firstSeen) + " ms"});
    f.push_back({"Last seen", String(h.lastSeen) + " ms"});
    return f;
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
    NimBLEScan *scan = scannerBleStart();
    if (!scan) {
        displayError("BLE scan failed", true);
        return;
    }

    std::vector<AirTagHit> hits;
    std::set<String> logged;
    std::set<String> audioSeen;

    ScannerListState ui;
    scannerListBegin(ui, "Wall Of Airtag", "scanning");

    auto ingest = [&]() {
        scannerBleKeepAlive(scan);
        const auto batch = scannerBleTakeInbox(scan);
        std::vector<String> newMacs;
        for (const auto &dev : batch) {
            if (dev.haveMfg && dev.mfg.size() >= 3) {
                uint16_t cid = (uint8_t)dev.mfg[0] | ((uint16_t)(uint8_t)dev.mfg[1] << 8);
                if (cid == 0x004C && (uint8_t)dev.mfg[2] == 0x07) {
                    audioSeen.insert(dev.mac);
                }
            }
            AirTagHit parsed;
            if (!parseFindMy(dev, parsed)) continue;
            const String &mac = dev.mac;
            int rssi = dev.rssi;
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
                parsed.randomAddr = (dev.addrType != BLE_ADDR_PUBLIC);
                parsed.hits = 1;
                parsed.firstSeen = millis();
                parsed.lastSeen = parsed.firstSeen;
                hits.push_back(parsed);
                newMacs.push_back(mac);
            }
        }
        if (fs) {
            for (const auto &mac : newMacs) {
                if (!logged.insert(mac).second) continue;
                const AirTagHit *hp = nullptr;
                for (const auto &h : hits) {
                    if (h.mac == mac) {
                        hp = &h;
                        break;
                    }
                }
                if (!hp) continue;
                File out = fs->open(logPath, FILE_APPEND);
                if (!out) out = fs->open(logPath, FILE_WRITE);
                if (out) {
                    out.println(
                        hp->mac + " " + String(hp->rssi) + " batt=" + batteryLabel(hp->battery) +
                        (hp->separated ? " sep" : " near")
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
            AirTagHit snap = hits[ui.cursor];
            scannerListShowDetail("RESULT DETAILS", airtagDetail(snap), [&]() { scannerBleKeepAlive(scan); });
            scannerListRefresh(ui);
            lastPaint = 0;
            continue;
        }
        ingest();
        if (millis() - lastPaint > 220) {
            lastPaint = millis();
            String keep = (ui.cursor >= 0 && ui.cursor < (int)hits.size()) ? hits[ui.cursor].mac : "";
            std::sort(hits.begin(), hits.end(), [](const AirTagHit &a, const AirTagHit &b) {
                return a.rssi > b.rssi;
            });
            if (keep.length()) {
                for (int i = 0; i < (int)hits.size(); i++) {
                    if (hits[i].mac == keep) {
                        ui.cursor = i;
                        break;
                    }
                }
            }
            String st = String(scannerBleAdvCount()) + " adv";
            if (!hits.empty()) st += "  " + String((int)hits.size()) + " tag";
            if (audioSeen.size()) st += " a" + String((int)audioSeen.size());
            scannerListSetStatus(ui, st.c_str());
            scannerListSetRows(ui, airtagRowLabels(hits));
        }
        delay(20);
    }

    scannerBleTeardown(true);
    scannerListEnd();
    displayInfo("Logged " + String((int)hits.size()) + " AirTag(s)", true);
}
#endif

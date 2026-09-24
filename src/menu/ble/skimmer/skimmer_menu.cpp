/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * BLE scan for common skimmer GAP names and MAC prefixes.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "skimmer.h"
#if defined(EVIL_EXTENSIONS)
#include "root/ui/display.h"
#include "root/ui/scanner_list.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <set>
#include <vector>

namespace {

const char *kBadNames[] = {"HC-03", "HC-05", "HC-06", "HC-08", "BT04-A", "BT05"};
const char *kBadMac[] = {"00:11:22", "00:18:e4", "20:16:04"};

struct SkimmerHit {
    String name;
    String mac;
    int rssi = 0;
    String rule;
    String addrType;
    bool matched = false;
    unsigned long firstSeen = 0;
    unsigned long lastSeen = 0;
};

const char *matchRule(const String &name, const String &addr) {
    String n = name;
    n.trim();
    for (auto *bad : kBadNames) {
        if (n.equalsIgnoreCase(bad)) return bad;
    }
    String a = addr;
    a.toLowerCase();
    for (auto *pfx : kBadMac) {
        if (a.startsWith(pfx)) return pfx;
    }
    return nullptr;
}

String addrTypeName(uint8_t t) {
    switch (t) {
        case BLE_ADDR_PUBLIC: return "public";
        case BLE_ADDR_RANDOM: return "random";
        default: return "other";
    }
}

std::vector<String> skimmerRowLabels(const std::vector<SkimmerHit> &hits) {
    std::vector<String> rows;
    rows.reserve(hits.size());
    for (const auto &h : hits) {
        String label;
        if (h.matched) label += "! ";
        label += (h.name.length() ? h.name : h.mac);
        label += "  " + String(h.rssi) + "dBm";
        rows.push_back(label);
    }
    return rows;
}

std::vector<ScannerDetailField> skimmerDetail(const SkimmerHit &h) {
    std::vector<ScannerDetailField> f;
    f.push_back({"Name", h.name.length() ? h.name : "<none>"});
    f.push_back({"MAC", h.mac});
    f.push_back({"RSSI", String(h.rssi) + " dBm"});
    f.push_back({"Matched", h.matched ? (h.rule.length() ? h.rule : "yes") : "no"});
    f.push_back({"Addr type", h.addrType});
    f.push_back({"First seen", String(h.firstSeen) + " ms"});
    f.push_back({"Last seen", String(h.lastSeen) + " ms"});
    return f;
}

} // namespace

void skimmerMenu() {
    NimBLEScan *scan = scannerBleStart();
    if (!scan) {
        displayError("BLE scan failed", true);
        return;
    }

    std::vector<SkimmerHit> hits;
    std::set<String> seen;
    int matchCount = 0;

    ScannerListState ui;
    scannerListBegin(ui, "Skimmer Detector", "scanning");

    auto ingest = [&]() {
        scannerBleKeepAlive(scan);
        const auto batch = scannerBleTakeInbox(scan);
        for (const auto &d : batch) {
            const String &name = d.name;
            const String &addr = d.mac;
            int rssi = d.rssi;
            const char *rule = matchRule(name, addr);
            if (seen.insert(addr).second) {
                if ((int)hits.size() >= 100) {
                    seen.erase(addr);
                    continue;
                }
                SkimmerHit h;
                h.name = name;
                h.mac = addr;
                h.rssi = rssi;
                h.rule = rule ? String(rule) : String();
                h.matched = rule != nullptr;
                h.addrType = addrTypeName(d.addrType);
                h.firstSeen = millis();
                h.lastSeen = h.firstSeen;
                hits.push_back(h);
                if (h.matched) matchCount++;
            } else {
                for (auto &h : hits) {
                    if (h.mac == addr) {
                        h.rssi = rssi;
                        h.lastSeen = millis();
                        if (name.length() && name != h.name) h.name = name;
                        if (rule && !h.matched) {
                            h.matched = true;
                            h.rule = rule;
                            matchCount++;
                        }
                        break;
                    }
                }
            }
        }
    };

    unsigned long lastPaint = 0;
    while (true) {
        ScannerListResult res = scannerListPoll(ui);
        if (res == SCANNER_LIST_EXIT) break;
        if (res == SCANNER_LIST_DETAIL && ui.cursor >= 0 && ui.cursor < (int)hits.size()) {
            SkimmerHit snap = hits[ui.cursor];
            scannerListShowDetail("RESULT DETAILS", skimmerDetail(snap), [&]() { scannerBleKeepAlive(scan); });
            scannerListRefresh(ui);
            lastPaint = 0;
            continue;
        }
        ingest();
        if (millis() - lastPaint > 220) {
            lastPaint = millis();
            String st = String(scannerBleAdvCount()) + " adv";
            if (matchCount) st += "  " + String(matchCount) + " hit";
            scannerListSetStatus(ui, st.c_str());
            scannerListSetRows(ui, skimmerRowLabels(hits));
        }
        delay(20);
    }

    scannerBleTeardown(true);
    scannerListEnd();
    if (matchCount == 0) displayInfo("Scan stopped", true);
    else displayInfo("Found " + String(matchCount) + " suspect(s)", true);
}
#endif

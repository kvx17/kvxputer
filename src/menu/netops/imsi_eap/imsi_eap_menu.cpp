/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * WiFi EAP-Identity sniff (not cellular IMSI catcher).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "imsi_eap.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <globals.h>
#include <set>

namespace {
struct IdSlot {
    char id[64];
};
constexpr int kQ = 16;
IdSlot gQ[kQ];
volatile int gHead = 0, gTail = 0;

void eapCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt || pkt->rx_ctrl.sig_len < 36) return;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    for (int i = 0; i < len - 8; i++) {
        if (p[i] == 0x88 && p[i + 1] == 0x8e) {
            int eap = i + 4;
            if (eap + 5 >= len) return;
            if (p[eap + 1] == 1 && p[eap + 4] == 1) { // EAP Request Identity
                int idlen = p[eap + 2] * 256 + p[eap + 3];
                if (idlen > 5 && eap + idlen <= len) {
                    int n = idlen - 5;
                    if (n > 63) n = 63;
                    if (n <= 0) return;
                    int next = (gHead + 1) % kQ;
                    if (next == gTail) return;
                    memcpy(gQ[gHead].id, p + eap + 5, n);
                    gQ[gHead].id[n] = 0;
                    gHead = next;
                }
            }
            return;
        }
    }
}
} // namespace

void imsiEapMenu() {
    FS *fs = nullptr;
    getFsStorage(fs);
    String logPath;
    if (fs) {
        kvx::paths::ensureDir(*fs, kvx::paths::WIFI_CAPTURES);
        logPath = String(kvx::paths::WIFI_CAPTURES) + "/eap_identity.txt";
    }
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);
    gHead = gTail = 0;
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(eapCb);
    std::set<String> seen;
    uint8_t ch = 1;
    unsigned long hop = millis();
    drawMainBorderWithTitle("EAP Identity Sniff");
    tft.drawString("Not cellular IMSI", 10, uiStatusY(0));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - hop > 250) {
            hop = millis();
            ch = ch >= 11 ? 1 : ch + 1;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        }
        while (gTail != gHead) {
            String id(gQ[gTail].id);
            gTail = (gTail + 1) % kQ;
            if (id.length() && seen.insert(id).second && fs) {
                File f = fs->open(logPath, FILE_APPEND);
                if (!f) f = fs->open(logPath, FILE_WRITE);
                if (f) {
                    f.println(id);
                    f.close();
                }
            }
        }
        tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString("IDs: " + String((int)seen.size()) + " ch " + String(ch), 10, uiStatusY(1));
        delay(40);
    }
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    wifiDisconnect();
    displayInfo("Captured " + String((int)seen.size()) + " identities", true);
}
#endif

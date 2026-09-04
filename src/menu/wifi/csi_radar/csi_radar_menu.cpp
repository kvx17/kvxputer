/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * CSI / ESP-NOW radar with RSSI hop fallback.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "csi_radar.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include <math.h>

namespace {

volatile float gAmp[64];
volatile uint32_t gCsiFrames = 0;
int8_t peak[14];
uint32_t pkts[14];
volatile uint8_t curCh = 1;

#if defined(CONFIG_ESP_WIFI_CSI_ENABLED) || defined(ESP_WIFI_CSI)
void csiRx(void *ctx, wifi_csi_info_t *info) {
    (void)ctx;
    if (!info || !info->buf || info->len < 2) return;
    int n = info->len / 2;
    if (n > 64) n = 64;
    const int8_t *q = (const int8_t *)info->buf;
    for (int i = 0; i < n; i++) {
        float i_ = q[i * 2];
        float qq = q[i * 2 + 1];
        gAmp[i] = sqrtf(i_ * i_ + qq * qq);
    }
    gCsiFrames++;
}
#endif

void promiscCb(void *buf, wifi_promiscuous_pkt_type_t) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    uint8_t ch = curCh;
    if (ch < 1 || ch > 13) return;
    if (pkt->rx_ctrl.rssi > peak[ch]) peak[ch] = pkt->rx_ctrl.rssi;
    pkts[ch]++;
}

void onEspNow(
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_now_recv_info_t *, const uint8_t *data, int len
#else
    const uint8_t *, const uint8_t *data, int len
#endif
) {
    if (len < 2) return;
    gCsiFrames++;
    int n = len < 64 ? len : 64;
    for (int i = 0; i < n; i++) gAmp[i] = (float)data[i];
}

void drawScope(const char *label) {
    tft.fillRect(8, 36, tftWidth - 16, 70, kvxConfig.bgColor);
    int w = (tftWidth - 20) / 32;
    for (int i = 0; i < 32; i++) {
        int h = (int)gAmp[i] / 4;
        if (h < 1) h = 1;
        if (h > 50) h = 50;
        tft.fillRect(8 + i * w, 90 - h, w - 1, h, kvxConfig.priColor);
    }
    tft.drawString(String(label) + " n=" + String((unsigned)gCsiFrames), 8, 108);
}

void runHopRssi() {
    for (int i = 0; i < 14; i++) {
        peak[i] = -127;
        pkts[i] = 0;
    }
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscCb);
    drawMainBorderWithTitle("CSI Radar RSSI");
    tft.drawString("ESC stop  hop 1-13", 10, tftHeight - 20);
    EscPress = false;
    unsigned long hop = millis();
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - hop > 180) {
            hop = millis();
            curCh = curCh >= 13 ? 1 : curCh + 1;
            esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
        }
        tft.fillRect(8, 36, tftWidth - 16, 70, kvxConfig.bgColor);
        int barW = (tftWidth - 20) / 13;
        for (int ch = 1; ch <= 13; ch++) {
            int h = peak[ch] + 90;
            if (h < 2) h = 2;
            if (h > 50) h = 50;
            tft.fillRect(8 + (ch - 1) * barW, 90 - h, barW - 1, h, kvxConfig.priColor);
        }
        delay(80);
    }
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
}

void runCsiSta() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    memset((void *)gAmp, 0, sizeof(gAmp));
    gCsiFrames = 0;
#if defined(CONFIG_ESP_WIFI_CSI_ENABLED) || defined(ESP_WIFI_CSI)
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true;
    cfg.htltf_en = true;
    cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true;
    cfg.channel_filter_en = false;
    cfg.manu_scale = false;
    esp_wifi_set_csi_config(&cfg);
    esp_wifi_set_csi_rx_cb(csiRx, nullptr);
    esp_wifi_set_csi(true);
#endif
    drawMainBorderWithTitle("CSI STA");
    tft.drawString("Need CSI-enabled build", 8, tftHeight - 20);
    EscPress = false;
    WiFiUDP udp;
    unsigned long ping = 0;
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - ping > 80) {
            ping = millis();
            IPAddress gw = WiFi.gatewayIP();
            udp.beginPacket(gw, 55555);
            uint8_t b = 0x42;
            udp.write(&b, 1);
            udp.endPacket();
        }
        drawScope("CSI");
        delay(50);
    }
#if defined(CONFIG_ESP_WIFI_CSI_ENABLED) || defined(ESP_WIFI_CSI)
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
#endif
}

void runEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    if (esp_now_init() != ESP_OK) {
        displayError("ESP-NOW init fail", true);
        return;
    }
    esp_now_register_recv_cb(onEspNow);
    memset((void *)gAmp, 0, sizeof(gAmp));
    gCsiFrames = 0;
    drawMainBorderWithTitle("CSI ESP-NOW");
    tft.drawString("Need CSI-Beacon slave", 8, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        drawScope("NOW");
        delay(50);
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
}

} // namespace

void csiRadarMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"RSSI hop (no CSI)", runHopRssi},
            {"STA CSI + ping", runCsiSta},
            {"Multi beacon ESP-NOW", runEspNow},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "CSI Radar");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
    wifiDisconnect();
}
#endif

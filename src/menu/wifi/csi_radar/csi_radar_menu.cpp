/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * CSI radar: per-channel RSSI from promiscuous RX (CSI API optional).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "csi_radar.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <globals.h>

namespace {
int8_t peak[14];
uint32_t pkts[14];
volatile uint8_t curCh = 1;

void csiCb(void *buf, wifi_promiscuous_pkt_type_t) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    uint8_t ch = curCh;
    if (ch < 1 || ch > 13) return;
    if (pkt->rx_ctrl.rssi > peak[ch]) peak[ch] = pkt->rx_ctrl.rssi;
    pkts[ch]++;
}
} // namespace

void csiRadarMenu() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);
    for (int i = 0; i < 14; i++) {
        peak[i] = -127;
        pkts[i] = 0;
    }
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(csiCb);
    drawMainBorderWithTitle("CSI Radar");
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    unsigned long hop = millis();
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - hop > 180) {
            hop = millis();
            curCh = curCh >= 13 ? 1 : curCh + 1;
            esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
        }
        tft.fillRect(8, 36, tftWidth - 16, tftHeight - 60, kvxConfig.bgColor);
        int barW = (tftWidth - 20) / 13;
        for (int ch = 1; ch <= 13; ch++) {
            int rssi = peak[ch];
            int h = rssi + 90;
            if (h < 2) h = 2;
            if (h > 50) h = 50;
            int x = 8 + (ch - 1) * barW;
            tft.fillRect(x, 90 - h, barW - 1, h, kvxConfig.priColor);
            tft.setCursor(x, 92);
            tft.print(ch);
        }
        uint32_t total = 0;
        for (int ch = 1; ch <= 13; ch++) total += pkts[ch];
        tft.fillRect(8, 108, tftWidth - 16, 12, kvxConfig.bgColor);
        tft.drawString("pkts " + String(total) + " ch " + String(curCh), 8, 108);
        delay(80);
    }
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    wifiDisconnect();
}
#endif

#if !defined(LITE_VERSION)
#include "channel_analyzer.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include <Arduino.h>
#include <globals.h>

// 2.4GHz channels to sweep.
static const uint8_t CA_CHANNELS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const int CA_NCH = sizeof(CA_CHANNELS) / sizeof(CA_CHANNELS[0]);

// EVA-01 themed palette (RGB565), cycled per channel bar.
static const uint16_t CA_PALETTE[] = {
    0x9818, // purple  (EVA-01 body)
    0x07E0, // green   (EVA-01 accent)
    0xFD20, // orange
    0xF800, // red
    0xFFE0, // yellow
    0x780F, // deep violet (second purple)
};
static const int CA_NPAL = sizeof(CA_PALETTE) / sizeof(CA_PALETTE[0]);

// Counters updated from the promiscuous RX callback for the *current* channel.
static volatile uint32_t ca_bytes = 0;
static volatile uint32_t ca_pkts = 0;
static volatile int8_t ca_rssi_peak = -128;

// Keep the callback minimal: just accumulate. Airtime is estimated in the loop.
static void IRAM_ATTR ca_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    ca_pkts++;
    ca_bytes += pkt->rx_ctrl.sig_len;
    if (pkt->rx_ctrl.rssi > ca_rssi_peak) ca_rssi_peak = pkt->rx_ctrl.rssi;
}

// Tolerant WiFi bring-up. Unlike the sniffer (ESP_ERROR_CHECK), we never abort:
// if WiFi is already initialised/started we get ESP_ERR_WIFI_INIT_STATE (or
// similar) and just carry on — promiscuous mode works regardless.
static void ca_start_wifi() {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE)
        Serial.printf("[ChAnalyzer] wifi_init: %s\n", esp_err_to_name(e));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE)
        Serial.printf("[ChAnalyzer] wifi_start: %s\n", esp_err_to_name(e));
    esp_wifi_set_promiscuous(true);
    // Capture every frame type so the airtime estimate reflects real load.
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(ca_rx_cb);
}

static void ca_stop_wifi() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    wifiDisconnect();
    vTaskDelay(1 / portTICK_RATE_MS);
}

static void
ca_draw(const uint8_t *load, const uint8_t *peak, const int8_t *rssi, uint8_t curCh, uint16_t dwell) {
    drawMainBorder(false);

    const int x0 = 8;
    const int top = 26;                         // below title
    const int labelH = 10;                      // channel numbers under bars
    const int bottom = tftHeight - 2 * LH * FP; // leave room for footer
    const int baseline = bottom - labelH;       // bars grow up from here
    const int plotH = baseline - top;
    const int plotW = tftWidth - 2 * x0;
    const int slot = plotW / CA_NCH;            // horizontal space per channel
    const int barW = (slot * 2) / 3;            // bar narrower than its slot
    const int barOff = (slot - barW) / 2;

    tft.setTextSize(FP);
    tft.drawFastHLine(x0, baseline, plotW, kvxConfig.priColor); // axis

    for (int i = 0; i < CA_NCH; i++) {
        uint8_t ch = CA_CHANNELS[i];
        int cellX = x0 + i * slot;
        int bx = cellX + barOff;
        bool isCur = (ch == curCh);
        uint16_t col = CA_PALETTE[i % CA_NPAL];

        // clear this column so we can redraw without flicker
        tft.fillRect(cellX, top, slot, plotH, kvxConfig.bgColor);

        // vertical load bar (grows upward from baseline)
        int bh = plotH * load[ch] / 100;
        if (bh > 0) tft.fillRect(bx, baseline - bh, barW, bh, col);

        // outline the channel currently being sampled
        if (isCur) tft.drawRect(bx - 1, top, barW + 2, plotH, kvxConfig.secColor);

        // peak-hold tick
        int ph = plotH * peak[ch] / 100;
        if (ph > 0) tft.drawFastHLine(bx, baseline - ph, barW, TFT_WHITE);

        // channel number under the baseline
        tft.setTextColor(isCur ? kvxConfig.secColor : kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString(String(ch), bx, baseline + 2, 1);
    }

    // footer: current channel detail
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    String foot = "Ch" + String(curCh) + " " + String(load[curCh]) + "% pk" +
                  String(peak[curCh]) + "% " + String(rssi[curCh]) + "dBm dwell " +
                  String(dwell) + "ms";
    tft.drawString(foot, x0, bottom, 1);
}

void channel_analyzer_setup() {
    returnToMenu = false;

    uint8_t load[12] = {0};
    uint8_t peak[12] = {0};
    int8_t rssi[12];
    for (int i = 0; i < 12; i++) rssi[i] = -128;

    uint16_t dwell = 350; // ms per channel, adjustable with Up/Down
    int idx = 0;

    ca_start_wifi();

    tft.fillScreen(kvxConfig.bgColor);
    drawMainBorderWithTitle("Channel Analyzer");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    padprintln("");
    padprintln(" sweeping 1-11 ...");
    delay(1000);
    drawMainBorder(true);
    for (;;) {
        if (returnToMenu) break;
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }
        if (check(UpPress) && dwell < 1000) dwell += 100;  // longer dwell = more accurate
        if (check(DownPress) && dwell > 150) dwell -= 100; // shorter dwell = faster sweep

        uint8_t ch = CA_CHANNELS[idx];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

        // reset counters for this dwell window
        ca_bytes = 0;
        ca_pkts = 0;
        ca_rssi_peak = -128;

        uint32_t t0 = millis();
        while (millis() - t0 < dwell) {
            if (check(EscPress)) {
                returnToMenu = true;
                break;
            }
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        if (returnToMenu) break;

        // Estimate airtime utilisation: bytes at a conservative ~6Mbps baseline
        // plus per-frame preamble/IFS overhead. Clamp to 0-100%.
        uint32_t airtime_us = (ca_bytes * 8UL) / 6UL + ca_pkts * 60UL;
        uint32_t dwell_us = (uint32_t)dwell * 1000UL;
        uint32_t l = dwell_us ? (airtime_us * 100UL / dwell_us) : 0;
        if (l > 100) l = 100;

        load[ch] = (uint8_t)l;
        if (load[ch] > peak[ch]) peak[ch] = load[ch];
        rssi[ch] = (ca_rssi_peak == -128) ? 0 : ca_rssi_peak;

        ca_draw(load, peak, rssi, ch, dwell);

        idx = (idx + 1) % CA_NCH;
    }

    ca_stop_wifi();
}

#endif

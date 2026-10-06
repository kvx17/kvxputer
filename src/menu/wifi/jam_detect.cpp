#if !defined(LITE_VERSION)
#include "jam_detect.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "menu/others/audio.h"
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include <Arduino.h>
#include <globals.h>

static const uint8_t JD_CHANNELS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const int JD_NCH = sizeof(JD_CHANNELS) / sizeof(JD_CHANNELS[0]);

// HARDWARE CONSTRAINT: the ESP32 has a single WiFi radio, so it can only be
// tuned to one channel at a time — watching "all channels at once" is physically
// impossible. We approximate full-band coverage by hopping rapidly with a very
// short dwell, so a full sweep of channels 1-11 completes in ~1s. We also alert
// the instant a deauth is seen on the current channel rather than waiting for the
// dwell window to expire (see the sampling loop below).
//
// dwell per channel while hopping (ms). ~100ms * 11 channels ≈ 1.1s per sweep.
static const uint16_t JD_DWELL = 100;

// Counters for the current channel's dwell window.
static volatile uint32_t jd_deauth = 0;
static volatile uint32_t jd_total = 0;
static volatile int8_t jd_last_rssi = -127;

static void IRAM_ATTR jd_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    jd_total = jd_total + 1;
    if (pkt->rx_ctrl.sig_len < 2) return;
    const uint8_t *f = pkt->payload;
    uint16_t fc = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
    uint8_t ftype = (fc & 0x0C) >> 2; // 0 = management
    uint8_t fsub = (fc & 0xF0) >> 4;  // 0x0C deauth, 0x0A disassoc
    if (ftype == 0x00 && (fsub == 0x0C || fsub == 0x0A)) {
        jd_deauth = jd_deauth + 1;
        jd_last_rssi = pkt->rx_ctrl.rssi;
    }
}

static void jd_start_wifi() {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE)
        Serial.printf("[JamDetect] wifi_init: %s\n", esp_err_to_name(e));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE)
        Serial.printf("[JamDetect] wifi_start: %s\n", esp_err_to_name(e));
    esp_wifi_disconnect(); // drop any STA association so channel hopping isn't locked to one channel
    esp_wifi_set_promiscuous(true);
    // CRITICAL: deauth/disassoc are management frames — capture MGMT (+DATA for
    // an activity reference). Without this the default filter may drop them.
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(jd_rx_cb);
}

static void jd_stop_wifi() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    wifiDisconnect();
    vTaskDelay(1 / portTICK_RATE_MS);
}

// Clear only the app body (below title rule). Chrome is drawn once at entry.
static void jd_clear_body() {
    const int top = 26;
    const int bottomPad = 6;
    tft.fillRect(6, top, tftWidth - 12, tftHeight - top - bottomPad, kvxConfig.bgColor);
}

static void jd_draw_rssi_bar(int x, int y, int w, int h, int8_t rssi, int floorDbm) {
    tft.drawRect(x, y, w, h, kvxConfig.priColor);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, kvxConfig.bgColor);
    int span = 0 - floorDbm;
    if (span < 1) span = 1;
    int level = (int)rssi - floorDbm;
    if (level < 0) level = 0;
    if (level > span) level = span;
    int fill = (w - 2) * level / span;
    if (fill > 0) tft.fillRect(x + 1, y + 1, fill, h - 2, kvxConfig.priColor);
}

static void jd_draw(
    const uint16_t *dps, const uint16_t *peak, uint32_t thr, uint8_t curCh, int attackCh, bool hopPaused,
    int8_t lastRssi, int rssiFloor
) {
    jd_clear_body();

    const int dense = uiDenseFont();
    tft.setTextSize(dense);

    const int x0 = 8;
    int y = 26;

    bool attack = (attackCh >= 0);
    uint16_t sc = attack ? TFT_RED : TFT_GREEN;
    const int bannerH = uiLineH(dense) + 10;
    tft.fillRect(x0, y, tftWidth - 2 * x0, bannerH, sc);
    tft.setTextColor(TFT_BLACK, sc);
    String banner;
    if (hopPaused) banner = "FROZEN ch" + String(curCh);
    else if (attack) banner = "ATTACK ch" + String(attackCh) + "  " + String(dps[attackCh]) + "/s";
    else banner = "scanning... no jamming";
    tft.drawCentreString(banner, tftWidth / 2, y + 3, 1);
    y += bannerH + 4;

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    // Fixed-width RSSI so shorter values do not leave ghosts.
    char rssiBuf[20];
    snprintf(rssiBuf, sizeof(rssiBuf), "RSSI %4ddBm", (int)lastRssi);
    tft.drawString(rssiBuf, x0, y, 1);
    int barX = x0 + tft.textWidth("RSSI -000dBm") + 6;
    int barW = tftWidth - barX - x0;
    if (barW > 20) jd_draw_rssi_bar(barX, y, barW, uiLineH(dense), lastRssi, rssiFloor);
    y += uiLineH(dense) + 4;

    const int labelW = 28;
    const int valW = 30;
    const int chBarX = x0 + labelW;
    const int bottom = uiFooterY(dense);
    const int rowH = (bottom - y) / JD_NCH;
    const int barWch = tftWidth - chBarX - valW - 6;
    uint32_t scale = thr * 2;
    if (scale < 4) scale = 4;

    for (int i = 0; i < JD_NCH; i++) {
        uint8_t ch = JD_CHANNELS[i];
        int ry = y + i * rowH;
        bool isCur = (ch == curCh);
        bool over = (dps[ch] >= thr);

        tft.setTextColor(
            isCur ? kvxConfig.bgColor : kvxConfig.priColor,
            isCur ? kvxConfig.priColor : kvxConfig.bgColor
        );
        tft.drawString("Ch" + String(ch), x0, ry, 1);

        int bh = rowH - 3;
        if (bh < 4) bh = 4;
        tft.drawRect(chBarX, ry, barWch, bh, kvxConfig.priColor);
        tft.fillRect(chBarX + 1, ry + 1, barWch - 2, bh - 2, kvxConfig.bgColor);
        uint32_t fillW = (uint32_t)(barWch - 2) * dps[ch] / scale;
        if (fillW > (uint32_t)(barWch - 2)) fillW = barWch - 2;
        if (fillW > 0)
            tft.fillRect(chBarX + 1, ry + 1, (int)fillW, bh - 2, over ? TFT_RED : kvxConfig.priColor);
        uint32_t pkX = (uint32_t)(chBarX + 1) + (uint32_t)(barWch - 2) * peak[ch] / scale;
        if (peak[ch] > 0 && pkX > (uint32_t)(chBarX + 1))
            tft.drawFastVLine((int)pkX, ry + 1, bh - 2, TFT_YELLOW);

        tft.setTextColor(over ? TFT_RED : kvxConfig.priColor, kvxConfig.bgColor);
        char valBuf[8];
        snprintf(valBuf, sizeof(valBuf), "%3u", (unsigned)dps[ch]);
        tft.drawString(valBuf, chBarX + barWch + 4, ry, 1);
    }

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    char foot[48];
    snprintf(
        foot,
        sizeof(foot),
        "ch%u thr%lu/s fl%d  UP/DN Prev/Nxt Sel ESC",
        (unsigned)curCh,
        (unsigned long)thr,
        rssiFloor
    );
    tft.drawString(foot, x0, bottom, 1);
}

void jam_detect_setup() {
    returnToMenu = false;

    uint16_t dps[12] = {0};
    uint16_t peak[12] = {0};
    uint32_t threshold = (uint32_t)kvxConfig.jamDetectAlertPerSec;
    if (threshold < 5) threshold = 5;
    if (threshold > 250) threshold = 250;
    int rssiFloor = kvxConfig.jamDetectRssiFloor;
    if (rssiFloor < -100) rssiFloor = -100;
    if (rssiFloor > -10) rssiFloor = -10;
    int idx = 0;
    bool hopPaused = false;
    uint32_t lastBeepMs = 0;

    // Snapshot of last painted state — skip full body redraw when frozen/idle.
    uint8_t paintedCh = 0xFF;
    int paintedAttack = -2;
    uint32_t paintedThr = 0;
    int paintedFloor = 0;
    bool paintedPaused = false;
    int8_t paintedRssi = 127;
    uint16_t paintedDps[12] = {0};

    jd_start_wifi();
    drawMainBorderWithTitle("Jam Detect", true);

    for (;;) {
        if (returnToMenu) break;

        uint8_t ch = JD_CHANNELS[idx];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(5 / portTICK_PERIOD_MS);

        jd_deauth = 0;
        jd_total = 0;

        bool tripped = false;
        bool controlsChanged = false;
        uint32_t t0 = millis();
        while (millis() - t0 < JD_DWELL) {
            if (check(EscPress)) {
                returnToMenu = true;
                break;
            }
            if (check(SelPress)) {
                hopPaused = !hopPaused;
                controlsChanged = true;
                delay(200);
            }
            if (check(UpPress) && threshold < 250) {
                threshold += 5;
                kvxConfig.setJamDetectAlertPerSec((int)threshold);
                controlsChanged = true;
            }
            if (check(DownPress) && threshold > 5) {
                threshold -= 5;
                kvxConfig.setJamDetectAlertPerSec((int)threshold);
                controlsChanged = true;
            }
            if (check(PrevPress) && rssiFloor > -100) {
                rssiFloor -= 5;
                kvxConfig.setJamDetectRssiFloor(rssiFloor);
                controlsChanged = true;
            }
            if (check(NextPress) && rssiFloor < -10) {
                rssiFloor += 5;
                kvxConfig.setJamDetectRssiFloor(rssiFloor);
                controlsChanged = true;
            }

            uint32_t live = (uint32_t)jd_deauth * 1000UL / JD_DWELL;
            if (live >= threshold) {
                tripped = true;
                break;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        if (returnToMenu) break;

        uint32_t d = (uint32_t)jd_deauth * 1000UL / JD_DWELL;
        if (d > 65535) d = 65535;
        dps[ch] = (uint16_t)d;
        if (dps[ch] > peak[ch]) peak[ch] = dps[ch];

        int attackCh = -1;
        uint16_t worst = 0;
        for (int i = 0; i < JD_NCH; i++) {
            uint8_t c = JD_CHANNELS[i];
            if (dps[c] >= threshold && dps[c] >= worst) {
                worst = dps[c];
                attackCh = c;
            }
        }
        if (tripped && attackCh < 0) attackCh = ch;

        int8_t lastRssi = jd_last_rssi;
        if ((attackCh >= 0 || tripped) && !hopPaused) {
            uint32_t now = millis();
            if (now - lastBeepMs > 400) {
                _tone(4000, 50);
                lastBeepMs = now;
            }
        }

        bool dpsChanged = false;
        for (int i = 0; i < JD_NCH; i++) {
            uint8_t c = JD_CHANNELS[i];
            if (dps[c] != paintedDps[c]) {
                dpsChanged = true;
                break;
            }
        }

        bool needPaint = controlsChanged || dpsChanged || ch != paintedCh || attackCh != paintedAttack ||
                         threshold != paintedThr || rssiFloor != paintedFloor || hopPaused != paintedPaused ||
                         lastRssi != paintedRssi;

        // When frozen with no new data, leave the frame alone.
        if (hopPaused && !needPaint) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        if (needPaint) {
            jd_draw(dps, peak, threshold, ch, attackCh, hopPaused, lastRssi, rssiFloor);
            paintedCh = ch;
            paintedAttack = attackCh;
            paintedThr = threshold;
            paintedFloor = rssiFloor;
            paintedPaused = hopPaused;
            paintedRssi = lastRssi;
            for (int i = 0; i < JD_NCH; i++) paintedDps[JD_CHANNELS[i]] = dps[JD_CHANNELS[i]];
        }

        if (!hopPaused) idx = (idx + 1) % JD_NCH;
    }

    jd_stop_wifi();
}

#endif

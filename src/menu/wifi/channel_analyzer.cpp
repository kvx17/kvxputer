#if !defined(LITE_VERSION)
#include "channel_analyzer.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "root/ui/display.h"
#include "root/ui/scanner_list.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include <Arduino.h>
#include <cstring>
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

// --- Beacon/probe ring: copy in IRQ, parse on main loop ---
static const int CA_RING = 16;
static const int CA_IE_CAP = 96;
static const int CA_AP_MAX = 48;

struct CaRawFrame {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t ieLen;
    uint8_t ie[CA_IE_CAP];
    uint16_t beaconInterval;
    uint16_t capab;
    volatile bool ready;
};

struct CaAp {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t ssidLen;
    char revealed[33]; // non-empty name learned after hidden beacon
    bool hidden;       // saw empty SSID IE
    int8_t rssi;
    uint8_t channel;
    uint16_t beaconInterval;
    uint16_t capab;
    bool hasWpa;
    bool hasRsn;
    bool hasSae;
    bool used;
};

static CaRawFrame ca_ring[CA_RING];
static volatile uint8_t ca_ring_w = 0;
static CaAp ca_aps[CA_AP_MAX];
static int ca_ap_count = 0;

static void ca_clear_aps() {
    memset(ca_aps, 0, sizeof(ca_aps));
    ca_ap_count = 0;
    ca_ring_w = 0;
    for (int i = 0; i < CA_RING; i++) ca_ring[i].ready = false;
}

static void IRAM_ATTR ca_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    ca_pkts++;
    ca_bytes += pkt->rx_ctrl.sig_len;
    if (pkt->rx_ctrl.rssi > ca_rssi_peak) ca_rssi_peak = pkt->rx_ctrl.rssi;

    // Management: Beacon (0x80) or Probe Response (0x50)
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 36) return;
    uint8_t ftype = p[0];
    if (ftype != 0x80 && ftype != 0x50) return;

    uint8_t slot = ca_ring_w;
    CaRawFrame *dst = &ca_ring[slot];
    if (dst->ready) {
        // Drop if ring full; advance write index anyway
        ca_ring_w = (slot + 1) % CA_RING;
        return;
    }
    memcpy((void *)dst->bssid, p + 16, 6);
    dst->rssi = pkt->rx_ctrl.rssi;
    dst->channel = pkt->rx_ctrl.channel;
    dst->beaconInterval = (uint16_t)p[32] | ((uint16_t)p[33] << 8);
    dst->capab = (uint16_t)p[34] | ((uint16_t)p[35] << 8);

    int ieOff = 36;
    int ieAvail = len - ieOff;
    if (ieAvail < 0) ieAvail = 0;
    if (ieAvail > CA_IE_CAP) ieAvail = CA_IE_CAP;
    dst->ieLen = (uint8_t)ieAvail;
    if (ieAvail > 0) memcpy((void *)dst->ie, p + ieOff, ieAvail);
    dst->ready = true;
    ca_ring_w = (slot + 1) % CA_RING;
}

static String ca_mac_str(const uint8_t *m) {
    char buf[18];
    snprintf(
        buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]
    );
    return String(buf);
}

static String ca_auth_label(const CaAp &ap) {
    bool privacy = (ap.capab & 0x0010) != 0;
    if (!privacy && !ap.hasWpa && !ap.hasRsn) return "Open";
    if (ap.hasSae) return "WPA3";
    if (ap.hasRsn) return "WPA2";
    if (ap.hasWpa) return "WPA";
    if (privacy) return "WEP/Privacy";
    return "Unknown";
}

static void ca_parse_ies(CaAp &ap, const uint8_t *ie, uint8_t ieLen) {
    ap.hasWpa = false;
    ap.hasRsn = false;
    ap.hasSae = false;
    int pos = 0;
    while (pos + 1 < ieLen) {
        uint8_t tag = ie[pos];
        uint8_t tlen = ie[pos + 1];
        if (pos + 2 + tlen > ieLen) break;
        const uint8_t *val = ie + pos + 2;
        if (tag == 0) { // SSID
            if (tlen == 0) {
                ap.hidden = true;
                // Keep any previously revealed name; leave ssid for display as <hidden>
            } else {
                uint8_t n = tlen > 32 ? 32 : tlen;
                memcpy(ap.ssid, val, n);
                ap.ssid[n] = 0;
                ap.ssidLen = n;
                // If AP was broadcasting hidden, keep a revealed copy
                if (ap.hidden || ap.revealed[0] == 0) {
                    memcpy(ap.revealed, ap.ssid, n + 1);
                }
            }
        } else if (tag == 3 && tlen >= 1) { // DS Parameter Set
            ap.channel = val[0];
        } else if (tag == 0x30) { // RSN
            ap.hasRsn = true;
            if (tlen >= 8) {
                for (int i = 0; i + 3 < tlen; i++) {
                    if (val[i] == 0x00 && val[i + 1] == 0x0F && val[i + 2] == 0xAC && val[i + 3] == 0x08) {
                        ap.hasSae = true;
                        break;
                    }
                }
            }
        } else if (tag == 0xDD && tlen >= 4) {
            if (val[0] == 0x00 && val[1] == 0x50 && val[2] == 0xF2 && val[3] == 0x01) ap.hasWpa = true;
        }
        pos += 2 + tlen;
    }
}

static void ca_ingest_ring() {
    for (int i = 0; i < CA_RING; i++) {
        if (!ca_ring[i].ready) continue;
        CaRawFrame snap = ca_ring[i];
        ca_ring[i].ready = false;

        int found = -1;
        for (int a = 0; a < CA_AP_MAX; a++) {
            if (!ca_aps[a].used) continue;
            if (memcmp(ca_aps[a].bssid, snap.bssid, 6) == 0) {
                found = a;
                break;
            }
        }
        if (found < 0) {
            if (ca_ap_count >= CA_AP_MAX) {
                // Replace weakest RSSI
                int weakest = 0;
                for (int a = 1; a < CA_AP_MAX; a++) {
                    if (ca_aps[a].rssi < ca_aps[weakest].rssi) weakest = a;
                }
                found = weakest;
            } else {
                for (int a = 0; a < CA_AP_MAX; a++) {
                    if (!ca_aps[a].used) {
                        found = a;
                        ca_ap_count++;
                        break;
                    }
                }
            }
        }
        if (found < 0) continue;

        CaAp &ap = ca_aps[found];
        bool wasNew = !ap.used;
        ap.used = true;
        memcpy(ap.bssid, snap.bssid, 6);
        ap.rssi = snap.rssi;
        if (snap.channel) ap.channel = snap.channel;
        ap.beaconInterval = snap.beaconInterval;
        ap.capab = snap.capab;
        if (wasNew) {
            ap.ssid[0] = 0;
            ap.ssidLen = 0;
            ap.revealed[0] = 0;
            ap.hidden = false;
        }
        ca_parse_ies(ap, snap.ie, snap.ieLen);
    }
}

static std::vector<String> ca_row_labels(uint8_t filterCh) {
    // filterCh == 0 → all SSIDs; else only that channel. Prefix is channel number.
    std::vector<String> rows;
    for (int i = 0; i < CA_AP_MAX; i++) {
        if (!ca_aps[i].used) continue;
        if (filterCh != 0 && ca_aps[i].channel != filterCh) continue;
        String name;
        if (ca_aps[i].hidden && ca_aps[i].ssidLen == 0) {
            name = ca_aps[i].revealed[0] ? String(ca_aps[i].revealed) : String("<hidden>");
        } else if (ca_aps[i].ssidLen == 0) {
            name = "<hidden>";
        } else {
            name = String(ca_aps[i].ssid);
        }
        rows.push_back(
            String(ca_aps[i].channel) + "  " + name + "  " + String(ca_aps[i].rssi) + "dBm"
        );
    }
    return rows;
}

static int ca_ap_at_row(int row, uint8_t filterCh) {
    int n = 0;
    for (int i = 0; i < CA_AP_MAX; i++) {
        if (!ca_aps[i].used) continue;
        if (filterCh != 0 && ca_aps[i].channel != filterCh) continue;
        if (n == row) return i;
        n++;
    }
    return -1;
}

static std::vector<ScannerDetailField> ca_detail_fields(const CaAp &ap) {
    std::vector<ScannerDetailField> f;
    String ssidDisp =
        (ap.ssidLen == 0) ? String("<hidden>") : String(ap.ssid);
    f.push_back({"SSID", ssidDisp});
    if (ap.hidden && ap.revealed[0]) {
        f.push_back({"Revealed", String(ap.revealed)});
    }
    f.push_back({"BSSID", ca_mac_str(ap.bssid)});
    f.push_back({"Channel", String(ap.channel)});
    f.push_back({"RSSI", String(ap.rssi) + " dBm"});
    f.push_back({"Auth", ca_auth_label(ap)});
    f.push_back({"Beacon int", String(ap.beaconInterval) + " TU"});
    f.push_back({"Hidden", ap.hidden || ap.ssidLen == 0 ? "yes" : "no"});
    return f;
}

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

// Green border follows navCh (user selection). Sweep continues independently.
static void
ca_draw(const uint8_t *load, const uint8_t *peak, const int8_t *rssi, uint8_t navCh, uint16_t dwell) {
    drawMainBorder(false);

    const int x0 = 8;
    const int top = 26;
    const int labelFont = uiDenseFont();
    const int labelH = uiLineH(labelFont) + 2;
    const int bottom = uiFooterY(labelFont);
    const int baseline = bottom - labelH;
    const int plotH = baseline - top;
    const int plotW = tftWidth - 2 * x0;
    const int slot = plotW / CA_NCH;
    const int barW = (slot * 2) / 3;
    const int barOff = (slot - barW) / 2;

    tft.setTextSize(labelFont);
    tft.drawFastHLine(x0, baseline, plotW, kvxConfig.priColor);

    for (int i = 0; i < CA_NCH; i++) {
        uint8_t ch = CA_CHANNELS[i];
        int cellX = x0 + i * slot;
        int bx = cellX + barOff;
        bool isNav = (ch == navCh);
        uint16_t col = CA_PALETTE[i % CA_NPAL];

        tft.fillRect(cellX, top, slot, plotH, kvxConfig.bgColor);

        int bh = plotH * load[ch] / 100;
        if (bh > 0) tft.fillRect(bx, baseline - bh, barW, bh, col);

        if (isNav) tft.drawRect(bx - 1, top, barW + 2, plotH, kvxConfig.secColor);

        int ph = plotH * peak[ch] / 100;
        if (ph > 0) tft.drawFastHLine(bx, baseline - ph, barW, TFT_WHITE);

        tft.setTextColor(isNav ? kvxConfig.secColor : kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString(String(ch), bx, baseline + 2, 1);
    }

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    String foot = "Ch" + String(navCh) + " " + String(load[navCh]) + "% pk" + String(peak[navCh]) +
                  "% " + String(rssi[navCh]) + "dBm OK=ch L=all";
    tft.drawString(foot, x0, bottom, 1);
}

// Background sweep one channel; does not move the green nav border.
// Returns false if Esc exits the tool.
// *action: 0=none, 1=OK, 2=L (all list), 3=left, 4=right (checked during dwell).
static bool ca_sweep_tick(
    uint8_t *load, uint8_t *peak, int8_t *rssi, uint16_t &dwell, int &sweepIdx, uint8_t navCh, bool drawBars,
    int *action
) {
    if (action) *action = 0;
    if (check(EscPress)) return false;
    if (check(UpPress) && dwell < 1000) dwell += 100;
    if (check(DownPress) && dwell > 150) dwell -= 100;

    uint8_t sweepCh = CA_CHANNELS[sweepIdx];
    esp_wifi_set_channel(sweepCh, WIFI_SECOND_CHAN_NONE);

    ca_bytes = 0;
    ca_pkts = 0;
    ca_rssi_peak = -128;

    auto pollAction = [&]() -> int {
        if (check(SelPress)) return 1;
        if (PrevPress && !UpPress) {
            check(PrevPress);
            return 3;
        }
        if (NextPress && !DownPress) {
            check(NextPress);
            return 4;
        }
#ifdef HAS_KEYBOARD
        char c = checkLetterShortcutPress();
        if (c == 'l' || c == 'L') return 2;
#endif
        return 0;
    };

    int act = pollAction();
    if (act && action) {
        *action = act;
        ca_ingest_ring();
        if (drawBars) ca_draw(load, peak, rssi, navCh, dwell);
        return true;
    }

    uint32_t t0 = millis();
    while (millis() - t0 < dwell) {
        if (check(EscPress)) return false;
        ca_ingest_ring();
        act = pollAction();
        if (act) {
            if (action) *action = act;
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    uint32_t airtime_us = (ca_bytes * 8UL) / 6UL + ca_pkts * 60UL;
    uint32_t elapsed = millis() - t0;
    if (elapsed < 20) elapsed = 20;
    uint32_t dwell_us = elapsed * 1000UL;
    uint32_t l = dwell_us ? (airtime_us * 100UL / dwell_us) : 0;
    if (l > 100) l = 100;

    load[sweepCh] = (uint8_t)l;
    if (load[sweepCh] > peak[sweepCh]) peak[sweepCh] = load[sweepCh];
    rssi[sweepCh] = (ca_rssi_peak == -128) ? 0 : ca_rssi_peak;

    ca_ingest_ring();
    if (drawBars) ca_draw(load, peak, rssi, navCh, dwell);

    if (!act) sweepIdx = (sweepIdx + 1) % CA_NCH;
    return true;
}

// filterCh == 0 → all SSIDs; else channel-filtered. Esc only closes detail (not list).
// Left (,) or Backspace returns to bars.
static void ca_ssid_list_view(
    uint8_t *load, uint8_t *peak, int8_t *rssi, uint16_t &dwell, int &sweepIdx, uint8_t filterCh
) {
    ScannerListState ui;
    const char *status = filterCh ? "Lt/Bksp=bars" : "all Lt/Bksp=bars";
    scannerListBegin(ui, "kvx wifi analyzer", status);
    scannerListSetRows(ui, ca_row_labels(filterCh));

    unsigned long lastHop = 0;
    unsigned long lastRefresh = 0;
    unsigned long hopStart = 0;
    bool hopping = false;
    int hopIdx = 0;

    while (!forceHome) {
        // Left arrow alone or Backspace → return to channel bars
        if (PrevPress && !UpPress) {
            check(PrevPress);
            break;
        }
        if (KeyStroke.del) {
            KeyStroke.Clear();
            break;
        }

        unsigned long now = millis();
        // Keep sweeping so the list stays fresh (all channels if filter=0, else stay on filter)
        if (!hopping && now - lastHop >= dwell) {
            uint8_t hopCh = filterCh ? filterCh : CA_CHANNELS[hopIdx];
            esp_wifi_set_channel(hopCh, WIFI_SECOND_CHAN_NONE);
            ca_bytes = 0;
            ca_pkts = 0;
            ca_rssi_peak = -128;
            hopStart = now;
            hopping = true;
        }
        if (hopping && now - hopStart >= dwell) {
            uint8_t hopCh = filterCh ? filterCh : CA_CHANNELS[hopIdx];
            uint32_t airtime_us = (ca_bytes * 8UL) / 6UL + ca_pkts * 60UL;
            uint32_t dwell_us = (uint32_t)dwell * 1000UL;
            uint32_t l = dwell_us ? (airtime_us * 100UL / dwell_us) : 0;
            if (l > 100) l = 100;
            load[hopCh] = (uint8_t)l;
            if (load[hopCh] > peak[hopCh]) peak[hopCh] = load[hopCh];
            rssi[hopCh] = (ca_rssi_peak == -128) ? 0 : ca_rssi_peak;
            if (!filterCh) hopIdx = (hopIdx + 1) % CA_NCH;
            lastHop = now;
            hopping = false;
        }

        ca_ingest_ring();

        if (now - lastRefresh > 400) {
            scannerListSetRows(ui, ca_row_labels(filterCh));
            String st = String(ca_ap_count) + " APs";
            if (filterCh) st = "Ch" + String(filterCh) + " " + st;
            scannerListSetStatus(ui, st.c_str());
            lastRefresh = now;
        }

        ScannerListResult r = scannerListPoll(ui);
        if (r == SCANNER_LIST_EXIT) {
            // Esc only dismisses detail (handled inside ShowDetail). Ignore Esc on list.
            if (forceHome) break;
            continue;
        }
        if (r == SCANNER_LIST_DETAIL) {
            int apIdx = ca_ap_at_row(ui.cursor, filterCh);
            if (apIdx >= 0) {
                String title =
                    ca_aps[apIdx].ssidLen == 0 ? String("<hidden>") : String(ca_aps[apIdx].ssid);
                scannerListShowDetail(title.c_str(), ca_detail_fields(ca_aps[apIdx]), [&]() {
                    ca_ingest_ring();
                });
                scannerListRefresh(ui);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    scannerListEnd();
    tft.fillScreen(kvxConfig.bgColor);
    drawMainBorder(true);
}

void channel_analyzer_setup() {
    returnToMenu = false;
    ca_clear_aps();

    uint8_t load[12] = {0};
    uint8_t peak[12] = {0};
    int8_t rssi[12];
    for (int i = 0; i < 12; i++) rssi[i] = -128;

    uint16_t dwell = 350;
    int sweepIdx = 0;
    int navIdx = 0; // user-selected channel (green border)

    ca_start_wifi();

    tft.fillScreen(kvxConfig.bgColor);
    drawMainBorderWithTitle("kvx wifi analyzer");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    padprintln("");
    padprintln(" sweeping 1-11 ...");
    padprintln(" Lt/Rt=ch OK=SSIDs");
    padprintln(" L=all list  Esc=exit");
    delay(800);
    drawMainBorder(true);
    ca_draw(load, peak, rssi, CA_CHANNELS[navIdx], dwell);

    for (;;) {
        if (returnToMenu || forceHome) break;

        int action = 0;
        if (!ca_sweep_tick(load, peak, rssi, dwell, sweepIdx, CA_CHANNELS[navIdx], true, &action)) {
            returnToMenu = true;
            break;
        }

        if (action == 3) { // left
            if (navIdx == 0) navIdx = CA_NCH - 1;
            else navIdx--;
            ca_draw(load, peak, rssi, CA_CHANNELS[navIdx], dwell);
        } else if (action == 4) { // right
            navIdx = (navIdx + 1) % CA_NCH;
            ca_draw(load, peak, rssi, CA_CHANNELS[navIdx], dwell);
        } else if (action == 1) { // OK → SSIDs for selected channel
            ca_ssid_list_view(load, peak, rssi, dwell, sweepIdx, CA_CHANNELS[navIdx]);
            ca_draw(load, peak, rssi, CA_CHANNELS[navIdx], dwell);
        } else if (action == 2) { // L → all SSIDs
            ca_ssid_list_view(load, peak, rssi, dwell, sweepIdx, 0);
            ca_draw(load, peak, rssi, CA_CHANNELS[navIdx], dwell);
        }
    }

    ca_stop_wifi();
}

#endif

#include "rf_spectrum.h"
#include "protocols/rf_config.h"
#include "protocols/rf_decoder.h"
#include "rf_utils.h"
#include "structs.h"
#include <cstring>

static bool spectrum_rmt_rx_done_callback(
    rmt_channel_t *channel, const rmt_rx_done_event_data_t *edata, void *user_data
) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static int16_t s_rfColH[320];
static bool s_rfGridReady = false;
static unsigned long s_rfLastPaintMs = 0;

void draw_tf_spectrum_grid(bool clearPlot = true) {
    tft.setTextSize(uiDenseFont()) /* Spectrum axis/HUD overlays stay dense */;
    if (!s_rfGridReady) {
        tft.setCursor(3, 2);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor, true);
        tft.printf(" RF - Spectrum (%.2f Mhz)", kvxConfigPins.rfFreq);
        s_rfGridReady = true;
    }
    if (clearPlot) {
        tft.fillRect(0, 20, tftWidth, tftHeight - 20, kvxConfig.bgColor);
        memset(s_rfColH, 0, sizeof(s_rfColH));
    }
    tft.drawFastHLine(0, 20 + tftHeight / 2, tftWidth, TFT_DARKGREY);
    tft.drawFastVLine((1 * tftWidth) / 4, 20, tftHeight - 20, TFT_DARKGREY);
    tft.drawFastVLine((2 * tftWidth) / 4, 20, tftHeight - 20, TFT_DARKGREY);
    tft.drawFastVLine((3 * tftWidth) / 4, 20, tftHeight - 20, TFT_DARKGREY);
}

static void rf_paint_waveform_durations(const std::vector<int> &durations) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t fg = kvxConfig.priColor;
    const int mid = 20 + tftHeight / 2;
    const int maxH = tftHeight / 2;
    const int n = (int)durations.size();
    if (n <= 0) return;

    // Erase previous columns without blanking the whole plot.
    for (int x = 0; x < tftWidth && x < (int)(sizeof(s_rfColH) / sizeof(s_rfColH[0])); x++) {
        int h = s_rfColH[x];
        if (h <= 0) continue;
        tft.drawFastVLine(x, mid - h / 2, h, bg);
        s_rfColH[x] = 0;
    }
    tft.drawFastHLine(0, mid, tftWidth, TFT_DARKGREY);

    for (int i = 0; i < n; i++) {
        int pulse = abs(durations[i]);
        int lineHeight = map(pulse, 0, SIGNAL_STRENGTH_THRESHOLD, 0, maxH);
        int lineX = map(i, 0, n > 1 ? n - 1 : 1, 0, tftWidth - 1);
        if (lineX < 0) lineX = 0;
        if (lineX >= tftWidth) lineX = tftWidth - 1;
        if (lineX >= (int)(sizeof(s_rfColH) / sizeof(s_rfColH[0]))) continue;
        int startY = constrain(mid - lineHeight / 2, 20, 20 + tftHeight);
        int endY = constrain(mid + lineHeight / 2, 20, 20 + tftHeight);
        tft.drawLine(lineX, startY, lineX, endY, fg);
        s_rfColH[lineX] = (int16_t)(endY - startY);
    }
}

void rf_spectrum() {
    tft.fillScreen(kvxConfig.bgColor);
    s_rfGridReady = false;
    s_rfLastPaintMs = 0;
    draw_tf_spectrum_grid(true);
    if (kvxConfigPins.rfModule == M5_RF_MODULE) {
        RfRxSession rx;
        if (!rx.begin()) {
            deinitRfModule();
            return;
        }

        std::vector<int> durations;
        std::vector<int> pending;
        bool havePending = false;
        while (1) {
            if (rx.poll(durations)) {
                pending = durations;
                havePending = true;
                RF_DBG("m5 spectrum: durations=%u", (unsigned)durations.size());
            }
            // Keep draining the radio every 10 ms; paint the latest burst at ~15 fps.
            if (havePending && (!s_rfLastPaintMs || millis() - s_rfLastPaintMs >= 66)) {
                rf_paint_waveform_durations(pending);
                s_rfLastPaintMs = millis();
                havePending = false;
            }

            if (check(EscPress)) { break; }
            if (setMHZMenu()) {
                rx.end();
                rx.begin();
                s_rfGridReady = false;
                draw_tf_spectrum_grid(true);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        rx.end();
        returnToMenu = true;
        deinitRfModule();
        return;
    }

    rmt_channel_handle_t rx_ch = NULL;
    rx_ch = setup_rf_rx();
    if (rx_ch == NULL) return;
    ESP_LOGI("RMT_SPECTRUM", "register RX done callback");
    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    assert(receive_queue);
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = spectrum_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_ch, &cbs, receive_queue));
    ESP_ERROR_CHECK(rmt_enable(rx_ch));
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 3000,     // 6us minimum signal duration
        .signal_range_max_ns = 12000000, // 24ms maximum signal duration
    };
    rmt_symbol_word_t item[64];
    rmt_rx_done_event_data_t rx_data;
    ESP_ERROR_CHECK(rmt_receive(rx_ch, item, sizeof(item), &receive_config));

    size_t rx_size = 0;
    std::vector<int> pendingRmt;
    bool havePendingRmt = false;
    while (1) {
        rmt_symbol_word_t *rx_items = NULL;
        if (xQueueReceive(receive_queue, &rx_data, 0) == pdPASS) {
            rx_size = rx_data.num_symbols;
            rx_items = rx_data.received_symbols;
        }
        if (rx_size != 0 && rx_items) {
            pendingRmt.clear();
            pendingRmt.reserve(rx_size);
            for (size_t i = 0; i < rx_size; i++) {
                pendingRmt.push_back((int)(rx_items[i].duration0 + rx_items[i].duration1));
            }
            havePendingRmt = true;
            ESP_ERROR_CHECK(rmt_receive(rx_ch, item, sizeof(item), &receive_config));
            rx_size = 0;
        }
        if (havePendingRmt && (!s_rfLastPaintMs || millis() - s_rfLastPaintMs >= 66)) {
            rf_paint_waveform_durations(pendingRmt);
            s_rfLastPaintMs = millis();
            havePendingRmt = false;
        }
        // Checks to leave while
        if (check(EscPress)) { break; }
        if (setMHZMenu()) {
            s_rfGridReady = false;
            draw_tf_spectrum_grid(true);
            yield();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    returnToMenu = true;
    rmt_disable(rx_ch);
    rmt_del_channel(rx_ch);
    vQueueDelete(receive_queue);
    deinitRfModule();
}

#define TIME_DIVIDER (tftWidth / 10)
//@Pirata
void rf_SquareWave() {
    if (!initRfModule("rx", kvxConfigPins.rfFreq)) return;

    RfRxSession rx;
    if (!rx.begin()) {
        deinitRfModule();
        return;
    }
    int line_w = 0;
    int line_h = 15;
    std::vector<int> durations;
PRINT:
    tft.drawPixel(0, 0, 0);
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(uiDenseFont()) /* Spectrum axis/HUD overlays stay dense */;
    tft.setCursor(3, 2);
    tft.printf("  RF - SquareWave (%.2f Mhz)", kvxConfigPins.rfFreq);

    while (1) {
        if (rx.poll(durations)) {
            // Draw the captured square wave (HIGH width then LOW width per pair).
            for (size_t i = 0; i + 1 < durations.size(); i += 2) {
                int high = abs(durations[i]);
                int low = abs(durations[i + 1]);
                if (high == 0) break;

                if (high > 20000) high = 20000;
                if (low > 20000) low = 20000;
                if (line_w + (high + low) / TIME_DIVIDER > tftWidth) {
                    line_w = 10;
                    line_h += 10;
                }
                if (line_h > tftHeight) {
                    line_h = 15;
                    tft.fillRect(0, 12, tftWidth, tftHeight, kvxConfig.bgColor);
                }
                tft.drawFastVLine(line_w, line_h, 6, kvxConfig.priColor);
                tft.drawFastHLine(line_w, line_h, high / TIME_DIVIDER, kvxConfig.priColor);

                tft.drawFastVLine(line_w + high / TIME_DIVIDER, line_h, 6, kvxConfig.priColor);
                tft.drawFastHLine(
                    line_w + high / TIME_DIVIDER, line_h + 6, low / TIME_DIVIDER, kvxConfig.priColor
                );
                line_w += (high + low) / TIME_DIVIDER;
            }
        }
        // Checks to leave while
        if (check(EscPress)) { break; }
        if (setMHZMenu()) goto PRINT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    rx.end();
    returnToMenu = true;
}

void rf_CC1101_rssi() {
#if !defined(LITE_VERSION)
    if (kvxConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("only for CC1101 module", true);
        return;
    }
    int graph_size = tftWidth - 20;
    std::vector<int> signal(graph_size, -95);
    const size_t freq_count = sizeof(subghz_frequency_list) / sizeof(float);
    std::vector<int> bar_size(freq_count, 0);
    int max_bar_size = tftHeight - 20 /*bottom margin*/ - 20 /*top margin*/;
    bool redraw = true;
    const int min_value = map(-70, -95, -20, 0, max_bar_size);
    while (1) {
        if (redraw) {
            redraw = false;
            tft.drawPixel(0, 0, 0);
            tft.fillScreen(kvxConfig.bgColor);
            tft.setTextSize(uiDenseFont()) /* Spectrum axis/HUD overlays stay dense */;
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.setCursor(3, 2);
            // Fixed frequency sees a dot running grafic, showing RSSI over time
            if (kvxConfigPins.rfFxdFreq) {
                if (!initRfModule("rx", kvxConfigPins.rfFreq))
                    displayError("Error setting frequency", true);
                tft.printf(" RF - RSSI spectrum (%.2f Mhz)", kvxConfigPins.rfFreq);
                tft.drawFastVLine(20, 20, tftHeight, kvxConfig.priColor);
                // Axis: y = base_y - rssi (rssi is negative). Fit on short screens.
                const int base_y = max(20, tftHeight - 100);
                tft.drawString("-95", 0, base_y - (-95));
                tft.drawString("-80", 0, base_y - (-80));
                tft.drawString("-65", 0, base_y - (-65));
                tft.drawString("-50", 0, base_y - (-50));
                tft.drawString("-35", 0, base_y - (-35));
                tft.drawString("-20", 0, base_y - (-20));
                // resets signal array
                std::fill(signal.begin(), signal.end(), -95);
            }
            // Range Scan Sees a bargraph simillar to NRF24 grafic, using RSSI across frequencies
            else {
                if (!initRfModule("rx", kvxConfigPins.rfFreq)) displayError("Error starting module", true);
                tft.printf(" RF - RSSI spectrum (%s)", subghz_frequency_ranges[kvxConfigPins.rfScanRange]);
                tft.drawFastHLine(0, tftHeight - 20, tftWidth, kvxConfig.priColor);
                char buf[7];
                float var = subghz_frequency_list[range_limits[kvxConfigPins.rfScanRange][0]];
                snprintf(buf, sizeof(buf), "%.3f", var);
                tft.drawString(buf, 2, tftHeight - 10);
                var = subghz_frequency_list[range_limits[kvxConfigPins.rfScanRange][1]];
                snprintf(buf, sizeof(buf), "%.3f", var);
                tft.drawRightString(buf, tftWidth - 2, tftHeight - 10);
                int range = range_limits[kvxConfigPins.rfScanRange][1] -
                            range_limits[kvxConfigPins.rfScanRange][0] + 1;
                int space = tftWidth / range;
                for (int i = 0; i < range; i++) {
                    tft.drawFastVLine(space * i, tftHeight - 20, 5, kvxConfig.priColor);
                }
                std::fill(bar_size.begin(), bar_size.end(), 0);
            }
        }

        // draw dot graph for fixed frequency
        if (kvxConfigPins.rfFxdFreq) {
            int rssi = ELECHOUSE_cc1101.getRssi();
            tft.drawPixel(0, 0, 0); // To make sure CC1101 shared with TFT works properly
            const int base_y = max(20, tftHeight - 100);
            int prev = signal[0];
            for (int i = 1; i < graph_size; i++) {
                if (EscPress || SelPress) break;
                const int x0 = 20 + (i - 1);
                const int x1 = 20 + i;
                const int curr = signal[i];
                // erase old segment between previous and current points
                tft.drawLine(x0, base_y - prev, x1, base_y - curr, kvxConfig.bgColor);
                const int next_val = (i == graph_size - 1) ? rssi : signal[i + 1];
                // shift buffer left by one
                signal[i - 1] = curr;
                if (i == graph_size - 1) signal[i] = rssi;
                // draw updated segment using new values
                tft.drawLine(x0, base_y - curr, x1, base_y - next_val, kvxConfig.priColor);
                prev = curr;
            }
            // Axis stays inside the plot band (not the full panel height).
            tft.drawFastVLine(20, 20, max(0, tftHeight - 40), kvxConfig.priColor);
            vTaskDelay(pdMS_TO_TICKS(75));
        }
        // draw a bargraph similar to nrf24 across the range
        else {
            int range = range_limits[kvxConfigPins.rfScanRange][1] -
                        range_limits[kvxConfigPins.rfScanRange][0] + 1;

            int space = tftWidth / range;
            int max_idx = 0;
            for (int i = 0; i < range; i++) {
                if (EscPress || SelPress) break;
                setMHZ(subghz_frequency_list[range_limits[kvxConfigPins.rfScanRange][0] + i]);
                vTaskDelay(pdMS_TO_TICKS(5));
                int rssi = ELECHOUSE_cc1101.getRssi();
                tft.drawPixel(0, 0, 0); // To make sure CC1101 shared with TFT works properly
                int size = map(rssi, -95, -20, 0, max_bar_size);
                if (size > bar_size[i]) bar_size[i] = size;
                else bar_size[i] = bar_size[i] - (bar_size[i] - size) / 2; // slow down decrease
                // Delta fill: shrink clears the top of the bar, grow fills the new segment.
                static int lastBar[64] = {0};
                if (i < 64) {
                    int prev = lastBar[i];
                    int cur = bar_size[i];
                    if (cur < prev) {
                        tft.fillRect(
                            i * space, tftHeight - 20 - prev, space - 2, prev - cur, kvxConfig.bgColor
                        );
                    } else if (cur > prev) {
                        tft.fillRect(
                            i * space, tftHeight - 20 - cur, space - 2, cur - prev, kvxConfig.priColor
                        );
                    } else if (cur > 0 && prev == 0) {
                        tft.fillRect(
                            i * space, tftHeight - 20 - cur, space - 2, cur, kvxConfig.priColor
                        );
                    }
                    lastBar[i] = cur;
                }
                if (bar_size[i] > bar_size[max_idx] && bar_size[i] > min_value) max_idx = i;
            }
            if (bar_size[max_idx] > min_value) {
                char buf[7];
                float var = subghz_frequency_list[range_limits[kvxConfigPins.rfScanRange][0] + max_idx];
                snprintf(buf, sizeof(buf), "%.2f", var);
                tft.drawCentreString("Max=      ", tftWidth / 2, tftHeight - 10);
                tft.drawCentreString("Max=" + String(buf), tftWidth / 2, tftHeight - 10);
            }
        }
        if (check(EscPress)) { break; }
        if (check(SelPress)) {
            deinitRfModule();
            rf_range_selection(kvxConfigPins.rfFreq);
            redraw = true;
        }
    }
    deinitRfModule();
#else
    displayError("Not available on Launcher version");
#endif
}

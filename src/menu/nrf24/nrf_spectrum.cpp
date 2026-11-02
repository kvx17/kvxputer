#include "nrf_spectrum.h"
#include "root/ui/display.h"
#include "root/ui/ui_metrics.h"
#include "root/input/mykeyboard.h"
#include <cstring>

#define CHANNELS 80
#define RGB565(r, g, b) ((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
uint8_t channel[CHANNELS];

static uint8_t s_nrfPainted[CHANNELS];
static unsigned long s_nrfLastPaintMs = 0;

// Radio walk only. Drawing is rate-capped separately so SPI does not thrash.
String scanChannels(bool web) {
    String result;
    if (web) result = "{";

    uint8_t rpdValues[CHANNELS] = {0};
    digitalWrite(kvxConfigPins.NRF24_bus.io0, LOW);

    for (int i = 0; i < CHANNELS; i++) {
        NRFradio.setChannel(i);
        NRFradio.startListening();
        delayMicroseconds(128);
        NRFradio.stopListening();

        int rpd = NRFradio.testRPD() ? 1 : 0;
        channel[i] = (channel[i] * 3 + rpd * 125) / 4;
        rpdValues[i] = channel[i];
    }

    digitalWrite(kvxConfigPins.NRF24_bus.io0, HIGH);

    if (web) {
        for (int i = 0; i < CHANNELS; i++) {
            if (i > 0) result += ",";
            result += String(rpdValues[i]);
        }
        result += "}";
    }
    return result;
}

static void nrf_paint_plot(const uint8_t *levels) {
    const int bw = max(1, (int)(tftWidth / CHANNELS));
    const int plotTop = 12;
    const int plotBottom = uiFooterY(FP) - 2;
    const int plotH = max(8, plotBottom - plotTop);
    const uint16_t bg = kvxConfig.bgColor;

    for (int i = 0; i < CHANNELS; i++) {
        int level = levels[i];
        if (level > plotH - 2) level = plotH - 2;
        if (level < 0) level = 0;
        const int prev = s_nrfPainted[i];
        if (level == prev) continue;

        const int x = i * bw;
        // Clear previous bar inside the plot band only (never y=0 through footer).
        if (prev > 0) {
            tft.drawFastVLine(x, plotBottom - prev, prev, bg);
        }
        if (level > 0) {
            tft.drawFastVLine(
                x, plotBottom - level, level, (i % 2 == 0) ? kvxConfig.priColor : TFT_DARKGREY
            );
            tft.drawFastVLine(x, plotTop, min(level, 4), kvxConfig.secColor);
        }
        if (i % 5 == 0 && i != 0) {
            tft.setTextSize(uiDenseFont());
            tft.setTextColor(kvxConfig.priColor, bg, true);
            tft.drawCentreString(String(i).c_str(), x, plotTop + plotH / 2, 1);
        }
        s_nrfPainted[i] = (uint8_t)level;
    }
}

void nrf_spectrum() {
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(FP);
    const int footY = uiFooterY(FP);
    tft.drawString("2.40Ghz", 0, footY);
    tft.drawCentreString("2.44Ghz", tftWidth / 2, footY, 1);
    tft.drawRightString("2.48Ghz", tftWidth, footY, 1);

    if (kvxConfigPins.NRF24_bus.cs == GPIO_NUM_NC || kvxConfigPins.NRF24_bus.io0 == GPIO_NUM_NC) {
        displayError("NRF24: set CS/CE\nin Pins (M5-Bus)", true);
        delay(500);
        return;
    }

    if (nrf_start(NRF_MODE_SPI)) { // This function only works on SPI
        NRFradio.setAutoAck(false);
        NRFradio.disableCRC();       // accept any signal we find
        NRFradio.setAddressWidth(2); // a reverse engineering tactic (not typically recommended)
        const uint8_t noiseAddress[][2] = {
            {0x55, 0x55},
            {0xAA, 0xAA},
            {0xA0, 0xAA},
            {0xAB, 0xAA},
            {0xAC, 0xAA},
            {0xAD, 0xAA}
        };
        for (uint8_t i = 0; i < 6; ++i) { NRFradio.openReadingPipe(i, noiseAddress[i]); }
        NRFradio.setDataRate(RF24_1MBPS);

        memset(s_nrfPainted, 0, sizeof(s_nrfPainted));
        s_nrfLastPaintMs = 0;
        uiMetricsMark("nrf_spectrum", UI_METRICS_ENTRY);

        while (!check(EscPress)) {
            scanChannels(false);
            const unsigned long now = millis();
            // Cap plot paints at ~30 fps; radio walk stays full rate.
            if (!s_nrfLastPaintMs || now - s_nrfLastPaintMs >= 33) {
                nrf_paint_plot(channel);
                s_nrfLastPaintMs = now;
            }
            uiMetricsTick();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        NRFradio.stopListening();
        NRFradio.powerDown();
        uiMetricsMark("nrf_spectrum", UI_METRICS_EXIT);
        delay(250);
        return;

    } else {
        Serial.println("Fail Starting radio");
        displayError("NRF24 start fail", true);
        delay(500);
    }
}

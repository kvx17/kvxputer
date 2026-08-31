#include "nrf_common.h"
#include "../../root/hal/bus_HAL.h"
#include "../../root/input/mykeyboard.h"

RF24 NRFradio(kvxConfigPins.NRF24_bus.io0, kvxConfigPins.NRF24_bus.cs);
HardwareSerial NRFSerial = HardwareSerial(2); // Uses UART2 for External NRF's
SPIClass *NRFSPI;

void nrf_info() {
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(FM);
    tft.setTextColor(TFT_RED, kvxConfig.bgColor);
    tft.drawCentreString("_Disclaimer_", tftWidth / 2, 10, 1);
    tft.setTextColor(TFT_WHITE, kvxConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(15, 33);
    padprintln("These functions were made to be used in a controlled environment for STUDY only.");
    padprintln("");
    padprintln("DO NOT use these functions to harm people or companies, you can go to jail!");
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    padprintln("");
    padprintln(
        "This device is VERY sensible to noise, so long wires or passing near VCC line can make "
        "things go wrong."
    );
    delay(1000);
    while (!check(AnyKeyPress)) { vTaskDelay(pdMS_TO_TICKS(1)); }
}

bool nrf_start(NRF24_MODE mode) {
    bool result = false;
    if (mode == NRF_MODE_DISABLED) return false;

    if (CHECK_NRF_UART(mode)) {
        if (USBserial.getSerialOutput() == &Serial1) {
            displayError("(E) UART already in use", true);
            result = false;
        }
        NRFSerial.begin(115200, SERIAL_8N1, kvxConfigPins.uart_bus.rx, kvxConfigPins.uart_bus.tx);
        Serial.println("NRF24 on Serial Started");
        result = true;
    };

    if (!CHECK_NRF_SPI(mode)) return result;

    // Always re-assert CE LOW and CS HIGH before begin() — these pins
    // may have been left in an indeterminate state by the previous session,
    // especially after stopConstCarrier() which can leave CE HIGH internally.
    pinMode(kvxConfigPins.NRF24_bus.cs, OUTPUT);
    digitalWrite(kvxConfigPins.NRF24_bus.cs, HIGH);
    pinMode(kvxConfigPins.NRF24_bus.io0, OUTPUT);
    digitalWrite(kvxConfigPins.NRF24_bus.io0, LOW);
    delay(5); // Let pins settle before SPI traffic

    NRFSPI =
        acquireSPIBus(kvxConfigPins.NRF24_bus.sck, kvxConfigPins.NRF24_bus.miso, kvxConfigPins.NRF24_bus.mosi);
    if (!NRFSPI) {
        Serial.println("No hardware SPI bus available for NRF24, falling back to default SPI");
        NRFSPI = &SPI;
    }
    delay(10);

    if (NRFradio.begin(
            NRFSPI,
            rf24_gpio_pin_t(kvxConfigPins.NRF24_bus.io0),
            rf24_gpio_pin_t(kvxConfigPins.NRF24_bus.cs)
        )) {
        result = true;
    } else {
        return false;
    }
    return result;
}

NRF24_MODE nrf_setMode() {
    NRF24_MODE mode = NRF_MODE_DISABLED;
    bool nrfSPI = true;
    bool nrfUART = true;
    if (kvxConfigPins.NRF24_bus.checkConflict(GPIO_NUM_NC)) {
        displayError("NRF24 pins not configured", true);
        nrfSPI = false;
    }
    // Serial UART oly display errors on Serial Monitor
    if (kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.uart_bus.rx) ||
        kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.uart_bus.tx)) {
        Serial.println("NRF24 pins conflict with UART pins");
        nrfUART = false;
    }
    if (kvxConfigPins.uart_bus.checkConflict(GPIO_NUM_NC)) {
        Serial.println("UART pins not configured");
        nrfUART = false;
    }
    if (nrfSPI && nrfUART) {
        // FIX: having both valid SPI pins AND valid UART pins is not an error.
        // A UART-NRF can't be reliably auto-detected from pin config, so when the
        // SPI pins are configured, default to SPI (the common case — e.g. the
        // M5Stick RF Pack S3 with an SPI nRF24). Previously this branch wrongly
        // raised "NRF24 pins undefined", blocking every SPI-only NRF24 device.
        mode = NRF_MODE_SPI;
    } else if (nrfSPI) {
        mode = NRF_MODE_SPI;
    } else if (nrfUART) {
        mode = NRF_MODE_UART;
    }
    return mode;
}

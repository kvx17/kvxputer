#include "root/hal/bus_HAL.h"
#include "root/app/powerSave.h"
#include "root/app/utils.h"
#include <M5Unified.h>
#include <interface.h>

#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000

// Side (DW): tap = Down (scroll next), hold = Up (scroll prev)
// Main (SEL): tap = OK/Sel, hold = Back/Esc
constexpr uint32_t kBtnLongPressMs = 600;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    M5.begin();
    Wire1.begin(47, 48);
    setSysI2CBus(&Wire1);

    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);

    M5.Power.setExtOutput(false); // It buzzes it ext power is turned on

    /*
| Device  | SCK   | MISO  | MOSI  | CS    | GDO0/CE   |
| ---     | :---: | :---: | :---: | :---: | :---:     |
| SD Card | 5     | 4     | 6     | 7     | ---       |
| CC1101  | 5     | 4     | 6     | 2     | 3         |
| NRF24   | 5     | 4     | 6     | 8     | 1         |
| PN532   | 5     | 4     | 6     | 43    | --        |
| WS500   | 5     | 4     | 6     | **    | **        |
| LoRa    | 5     | 4     | 6     | **    | **        |
    */
    pinMode(7, OUTPUT);
    digitalWrite(7, HIGH); // SD Card CS
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH); // CC1101 CS
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH); // nRF24L01 CS
    pinMode(43, OUTPUT);
    digitalWrite(43, HIGH); // PN532 CS
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW); // M5RF433 avoid Jamming
    pinMode(46, OUTPUT);
    digitalWrite(46, LOW); // Infrared LED Off

    pinMode(TFT_BL, OUTPUT);
    kvxConfig.colorInverted = 0;
}
/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 250);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 5;
    else dutyCycle = ((brightval * 250) / 100);

    // Serial.printf("dutyCycle for bright 0-255: %d\n", dutyCycle);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        // Serial.println("Failed to set brightness");
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int level = M5.Power.getBatteryLevel();
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

/*********************************************************************
** Function: InputHandler
** Side (DW): tap = DownPress, hold = UpPress (scroll order like Unit Scroll).
** Main (SEL): tap = Sel, hold = Esc.
** Use Up/Down — not Next/Prev — so the channel grid walks 1→2→3… (column-major)
** instead of jumping sideways 1→3→5.
**********************************************************************/
void InputHandler(void) {
    static bool selWasDown = false;
    static bool dwWasDown = false;
    static unsigned long selDownAt = 0;
    static unsigned long dwDownAt = 0;
    static bool selLongFired = false;
    static bool dwLongFired = false;
    static unsigned long tm = 0;

    unsigned long now = millis();
    if (now - tm < 180 && !LongPress) return;

    bool selDown = (digitalRead(SEL_BTN) == BTN_ACT);
    bool dwDown = (digitalRead(DW_BTN) == BTN_ACT);

    if (!(selDown || dwDown || selWasDown || dwWasDown)) return;

    // Returns true if the press should perform a menu action (false = woke screen only).
    auto actAfterWake = [&]() -> bool {
        if (wakeUpScreen()) return false;
        AnyKeyPress = true;
        return true;
    };

    // --- Side button: tap Down, hold Up ---
    if (dwDown && !dwWasDown) {
        dwWasDown = true;
        dwDownAt = now;
        dwLongFired = false;
    }
    if (dwDown && dwWasDown && !dwLongFired && (now - dwDownAt) >= kBtnLongPressMs) {
        dwLongFired = true;
        tm = now;
        if (actAfterWake()) UpPress = true;
    }
    if (!dwDown && dwWasDown) {
        if (!dwLongFired && (now - dwDownAt) < kBtnLongPressMs) {
            tm = now;
            if (actAfterWake()) DownPress = true;
        }
        dwWasDown = false;
        dwLongFired = false;
    }

    // --- Main button: tap Sel (OK), hold Esc (back) ---
    if (selDown && !selWasDown) {
        selWasDown = true;
        selDownAt = now;
        selLongFired = false;
    }
    if (selDown && selWasDown && !selLongFired && (now - selDownAt) >= kBtnLongPressMs) {
        selLongFired = true;
        tm = now;
        if (actAfterWake()) EscPress = true;
    }
    if (!selDown && selWasDown) {
        if (!selLongFired && (now - selDownAt) < kBtnLongPressMs) {
            tm = now;
            if (actAfterWake()) SelPress = true;
        }
        selWasDown = false;
        selLongFired = false;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}

bool isCharging() {
    // Strategy to stop buzzing
    static int lastState = -1;
    bool charging = M5.Power.isCharging();
    if (charging && lastState != 1) {
        lastState = 1;
        M5.Power.setExtOutput(false);
    } else if (!charging && lastState != 0) {
        lastState = 0;
        M5.Power.setExtOutput(true);
    }
    return charging;
}

/*********************************************************************
** Function: _setup_codec_speaker
** location: modules/others/audio.cpp
** Handles audio CODEC to enable/disable speaker
**********************************************************************/
static TimerHandle_t speaker_off_timer = NULL;

static void speaker_off_timer_cb(TimerHandle_t xTimer) {
    if (!speaker_off_timer) return;
    static constexpr const uint8_t disabled_bulk_data[] = {0};
    i2c_bulk_write(&Wire1, ES8311_ADDR, disabled_bulk_data); // Shutdown ES8311
    M5.In_I2C.bitOff(0x6E, 0x11, 1 << 3, 100000);            // Set gpio3 output low (turn off PA)
}

void _setup_codec_speaker(bool enable) {

    static constexpr const uint8_t enabled_bulk_data[] = {
        2, 0x00, 0x80, // 0x00 RESET/  CSM POWER ON
        2, 0x01, 0xB5, // 0x01 CLOCK_MANAGER/ MCLK=BCLK
        2, 0x02, 0x18, // 0x02 CLOCK_MANAGER/ MULT_PRE=3
        2, 0x0D, 0x01, // 0x0D SYSTEM/ Power up analog circuitry
        2, 0x12, 0x00, // 0x12 SYSTEM/ power-up DAC - NOT default
        2, 0x13, 0x10, // 0x13 SYSTEM/ Enable output to HP drive - NOT default
        2, 0x32, 0xBF, // 0x32 DAC/ DAC volume (0xBF == +-0 dB )
        2, 0x37, 0x08, // 0x37 DAC/ Bypass DAC equalizer - NOT default
        0
    };

    if (speaker_off_timer == NULL) {
        speaker_off_timer =
            xTimerCreate("SpkOffTimer", pdMS_TO_TICKS(100), pdFALSE, (void *)0, speaker_off_timer_cb);
    }

    if (enable) {
        if (speaker_off_timer != NULL && xTimerIsTimerActive(speaker_off_timer)) {
            xTimerStop(speaker_off_timer, 0); // Cancel pending shutdown
        } else {
            i2c_bulk_write(&Wire1, ES8311_ADDR, enabled_bulk_data);
            M5.In_I2C.bitOn(0x6E, 0x11, 1 << 3, 100000); // Set gpio3 output high (turn on PA)
        }
    } else {
        if (speaker_off_timer != NULL) {
            xTimerReset(speaker_off_timer, 0); // Start/reset shutdown timeout for 100ms
        }
    }
}

/*********************************************************************
** Function: _setup_codec_mic
** location: modules/others/mic.cpp
** Handles audio CODEC to enable/disable microphone
**********************************************************************/
void _setup_codec_mic(bool enable) {
    // Set microfone pin for ADV
    mic_bclk_pin = (gpio_num_t)17;

    static constexpr const uint8_t enabled_bulk_data[] = {
        2, 0x00, 0x80, // 0x00 RESET/  CSM POWER ON
        2, 0x01, 0xBA, // 0x01 CLOCK_MANAGER/ MCLK=BCLK
        2, 0x02, 0x18, // 0x02 CLOCK_MANAGER/ MULT_PRE=3
        2, 0x0D, 0x01, // 0x0D SYSTEM/ Power up analog circuitry
        2, 0x0E, 0x02, // 0x0E SYSTEM/ : Enable analog PGA, enable ADC modulator
        2, 0x14, 0x10, // ES8311_ADC_REG14 : select Mic1p-Mic1n / PGA GAIN (minimum)
        2, 0x17, 0xBF, // ES8311_ADC_REG17 : ADC_VOLUME 0xBF == +- 0 dB
        2, 0x1C, 0x6A, // ES8311_ADC_REG1C : ADC Equalizer bypass, cancel DC offset in digital domain
        0
    };
    static constexpr const uint8_t disabled_bulk_data[] = {
        2,
        0x0D,
        0xFC, // 0x0D SYSTEM/ Power down analog circuitry
        2,
        0x0E,
        0x6A, // 0x0E SYSTEM
        2,
        0x00,
        0x00, // 0x00 RESET/  CSM POWER DOWN
        0
    };

    i2c_bulk_write(&Wire1, ES8311_ADDR, enable ? enabled_bulk_data : disabled_bulk_data);
}

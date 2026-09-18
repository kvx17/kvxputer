#include "root/app/powerSave.h"
#include "root/input/unit_scroll.h"
#include "root/input/unit_joystick2.h"
#include "root/hal/pahub.h"
#include "root/hal/led_control.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include <Adafruit_TCA8418.h>
#include <Keyboard.h>
#include <Wire.h>
#include <cstring>
#include <interface.h>
#include <globals.h>
#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)
#include "menu/lora/LoRaRF.h"
#endif

// Cardputer and 1.1 keyboard
Keyboard_Class Keyboard;
// TCA8418 keyboard controller for ADV variant
Adafruit_TCA8418 tca;
bool UseTCA8418 = false; // Set to true to use TCA8418 (Cardputer ADV)

// Keyboard state variables
bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;

constexpr unsigned long TCA8418_REPEAT_START_MS = 350;
constexpr unsigned long TCA8418_REPEAT_MS = 150;

// Sticky held-nav state for TCA8418 auto-repeat. File-scoped so charge/sleep
// can clear a stuck '.' hold that otherwise keeps DownPress pulsing forever.
static bool navHoldSel = false;
static bool navHoldPrev = false;
static bool navHoldNext = false;
static bool navHoldUp = false;
static bool navHoldDown = false;
static bool navHoldEsc = false;
static bool navHoldDel = false;
static bool navHoldGui = false;
static bool navHoldAlt = false;
static bool navHoldCtrl = false;
static unsigned long navNextRepeatTime = 0;
static unsigned long navPrevRepeatTime = 0;
static unsigned long navUpRepeatTime = 0;
static unsigned long navDownRepeatTime = 0;
// Printable keys currently held on TCA8418 (press/release tracked). Needed for
// tap-vs-hold UX — KeyStroke is only a press pulse on ADV.
static bool tcaCharHeld[128] = {};

void resetHeldNavKeys(void) {
    navHoldSel = false;
    navHoldPrev = false;
    navHoldNext = false;
    navHoldUp = false;
    navHoldDown = false;
    navHoldEsc = false;
    navHoldDel = false;
    navHoldGui = false;
    navHoldAlt = false;
    navHoldCtrl = false;
    navNextRepeatTime = 0;
    navPrevRepeatTime = 0;
    navUpRepeatTime = 0;
    navDownRepeatTime = 0;
    NextPress = false;
    PrevPress = false;
    UpPress = false;
    DownPress = false;
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
    KeyStroke.Clear();
    memset(tcaCharHeld, 0, sizeof(tcaCharHeld));
}

bool isCardputerKeyHeld(char c) {
    if (UseTCA8418) {
        const unsigned char uc = (unsigned char)c;
        if (uc < 128 && tcaCharHeld[uc]) return true;
        // Digits / letters may arrive shifted; also check opposite case.
        if (c >= 'a' && c <= 'z') {
            const unsigned char up = (unsigned char)(c - 'a' + 'A');
            return tcaCharHeld[up];
        }
        if (c >= 'A' && c <= 'Z') {
            const unsigned char lo = (unsigned char)(c - 'A' + 'a');
            return tcaCharHeld[lo];
        }
        return false;
    }
    Keyboard.update();
    if (Keyboard.isKeyPressed(c)) return true;
    if (c >= 'a' && c <= 'z' && Keyboard.isKeyPressed(c - 'a' + 'A')) return true;
    if (c >= 'A' && c <= 'Z' && Keyboard.isKeyPressed(c - 'A' + 'a')) return true;
    return false;
}

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed);
void mapRawKeyToPhysical(uint8_t rawValue, uint8_t &row, uint8_t &col);

char getKeyChar(uint8_t row, uint8_t col) {
    char keyVal;
    if (shift_key_pressed ^ caps_lock) {
        keyVal = _key_value_map[row][col].value_second;
    } else {
        keyVal = _key_value_map[row][col].value_first;
    }
    return keyVal;
}

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed) {
    char keyVal = _key_value_map[row][col].value_first;
    switch (keyVal) {
        case 0xFF:
            fn_key_pressed = pressed;
            if (fn_key_pressed) Serial.println("FN Pressed");
            else Serial.println("FN Released");
            return 1;
        case 0x81:
            shift_key_pressed = pressed;
            if (shift_key_pressed) Serial.println("Shift Pressed");
            else Serial.println("Shift Released");
            if (shift_key_pressed && fn_key_pressed) {
                caps_lock = !caps_lock;
                if (caps_lock) Serial.println("CAPS Lock activated");
                else Serial.println("CAPS Lock DEactivated");
                shift_key_pressed = false;
                fn_key_pressed = false;
            }
            return 1;
        default: break;
    }
    return 0;
}

/***************************************************************************************
** Function name: mapRawKeyToPhysical()
** Location: interface.cpp
** Description:   initial mapping for keyboard
***************************************************************************************/
inline void mapRawKeyToPhysical(uint8_t keyvalue, uint8_t &row, uint8_t &col) {
    const uint8_t u = keyvalue % 10; // 1..8
    const uint8_t t = keyvalue / 10; // 0..6

    if (u >= 1 && u <= 8 && t <= 6) {
        const uint8_t u0 = u - 1;   // 0..7
        row = u0 & 0x03;            // bits [1:0] => 0..3
        col = (t << 1) | (u0 >> 2); // t*2 + bit2(u0) => 0..13
    } else {
        row = 0xFF; // invalid
        col = 0xFF;
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    //    Keyboard.begin();
    pinMode(0, INPUT);
    pinMode(5, OUTPUT);
    // Set GPIO5 HIGH for SD card compatibility (thx for the tip @bmorcelli & 7h30th3r0n3)
    digitalWrite(5, HIGH);
}
volatile bool kb_interrupt = false;
void IRAM_ATTR gpio_isr_handler(void *arg) {
    kb_interrupt = true;
    // static long i = 0;
    // Serial.printf("interrupt %ld\n", i++);
}
void _post_setup_gpio() {
    // Initialize TCA8418 I2C keyboard controller
    Serial.println("DEBUG: Cardputer ADV - Initializing TCA8418 keyboard");

    // Use correct I2C pins for Cardputer ADV
    Serial.printf("DEBUG: Initializing I2C with SDA=%d, SCL=%d\n", TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    Wire1.begin(TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    delay(100);

    // Scan I2C bus to see what's available
    Serial.println("DEBUG: Scanning I2C bus...");
    byte found_devices = 0;
    for (byte i = 1; i < 127; i++) {
        Wire1.beginTransmission(i);
        if (Wire1.endTransmission() == 0) {
            Serial.printf("DEBUG: Found I2C device at address 0x%02X\n", i);
            found_devices++;
        }
    }
    Serial.printf("DEBUG: Found %d I2C devices\n", found_devices);

    // Try to initialize TCA8418
    Serial.printf("DEBUG: Attempting to initialize TCA8418 at address 0x%02X\n", TCA8418_I2C_ADDR);
    UseTCA8418 = tca.begin(TCA8418_I2C_ADDR, &Wire1);

    if (!UseTCA8418) {
        Serial.println("ADV  : Failed to initialize TCA8418!");
        Serial.println("Probable standard Cardputer detected, switching to Keyboard library");
        Wire1.end();
        Keyboard.begin();
        pahubBootProbe();
        pahubInitInputDevices();
        return;
    }
    kvxConfigPins.sys_i2c.sda = (gpio_num_t)8;
    kvxConfigPins.sys_i2c.scl = (gpio_num_t)9;

#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)
    applyLoraCapPinDefaults();
    ensureLoraSettings();
#else
    kvxConfigPins.gps_bus.rx = (gpio_num_t)15;
    kvxConfigPins.gps_bus.tx = (gpio_num_t)13;
    kvxConfigPins.gpsBaudrate = 115200;
#endif

    kvxConfigPins.CC1101_bus.sck = (gpio_num_t)40;
    kvxConfigPins.CC1101_bus.miso = (gpio_num_t)39;
    kvxConfigPins.CC1101_bus.mosi = (gpio_num_t)14;
    kvxConfigPins.CC1101_bus.cs = (gpio_num_t)13;
    kvxConfigPins.CC1101_bus.io0 = (gpio_num_t)5;

    kvxConfigPins.NRF24_bus.sck = (gpio_num_t)40;
    kvxConfigPins.NRF24_bus.miso = (gpio_num_t)39;
    kvxConfigPins.NRF24_bus.mosi = (gpio_num_t)14;
    kvxConfigPins.NRF24_bus.cs = (gpio_num_t)6;
    kvxConfigPins.NRF24_bus.io0 = (gpio_num_t)4;

    pinMode(kvxConfigPins.NRF24_bus.cs, OUTPUT);
    pinMode(kvxConfigPins.CC1101_bus.cs, OUTPUT);
    digitalWrite(kvxConfigPins.NRF24_bus.cs, HIGH);
    digitalWrite(kvxConfigPins.CC1101_bus.cs, HIGH);

    tca.matrix(7, 8);
    tca.flush();
    pinMode(11, INPUT);
    attachInterruptArg(digitalPinToInterrupt(11), gpio_isr_handler, nullptr, CHANGE);
    tca.enableInterrupts();

    // Optional Unit Scroll / PaHub / Joystick2 on Grove PORT.A — silent if absent
    pahubBootProbe();
    pahubInitInputDevices();
}

/***************************************************************************************
** Function name: pollEncoder
** Samples Unit Scroll over I2C every input-task tick (not gated on AnyKeyPress).
***************************************************************************************/
void pollEncoder(void) {
    pahubBeginGrovePoll();
    unitScrollPoll();
    unitJoystick2Poll();
    pahubEndGrovePoll();
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0 && chargeModeActive && !chargeUserSleep) return;
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
        return;
    }
    int bl;
    if (chargeModeActive) {
        // Nightstand: 1% must be visibly dimmer than MINBRIGHT (160/255).
        bl = 12 + (int)(243 * (int)brightval / 100);
    } else {
        bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
    }
    analogWrite(TFT_BL, bl);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    // Apply encoder-task pending flags on every exit, including early returns.
    // pollEncoder() only samples; merging here (after keyboard assignment, after
    // taskInputHandler cleared the globals) is what makes tap/hold survive.
    struct GroveMerge {
        ~GroveMerge() {
            unitScrollApplyInput();
            unitJoystick2ApplyInput();
        }
    } groveMerge;

    // G0: tap = fake-off display; hold (>=700ms) = force home once.
    // Do NOT re-assert forceHome every poll while held — that starves the
    // mainscreen loop (forceHome → continue forever) and freezes input.
    // While forceHome is still true, keep EscPress/returnToMenu sticky so
    // nested apps that only check Esc unwind all the way to the main grid.
    static bool g0WasLow = false;
    static unsigned long g0PressAt = 0;
    static bool g0HoldFired = false;
    const bool g0Low = digitalRead(0) == LOW;
    if (g0Low && !g0WasLow) {
        g0PressAt = millis();
        g0HoldFired = false;
        AnyKeyPress = true;
    }
    if (g0Low && !g0HoldFired && (millis() - g0PressAt >= 700)) {
        g0HoldFired = true;
        if (kvxConfig.g0HoldHome) {
            forceHome = true;
            returnToMenu = true;
            EscPress = true;
            AnyKeyPress = true;
            if (isScreenOff) wakeUpScreen();
        }
    }
    // Sticky Esc while forceHome is pending (main menu clears it).
    if (forceHome && kvxConfig.g0HoldHome) {
        returnToMenu = true;
        EscPress = true;
        AnyKeyPress = true;
    }
    if (!g0Low && g0WasLow) {
        if (!g0HoldFired) {
            if (isScreenOff || chargeUserSleep) {
                if (chargeModeActive) {
                    chargeUserSleep = false;
                    isScreenOff = false;
                    dimmer = false;
                    if (chargeModeBright >= 0) _setBrightness((uint8_t)chargeModeBright);
                    else _setBrightness(kvxConfig.bright);
                } else {
                    wakeUpScreen();
                }
            } else if (chargeModeActive) {
                // Charge owns blanking: allow brightness 0 via chargeUserSleep.
                chargeUserSleep = true;
                isScreenOff = true;
                turnOffDisplay();
            } else {
                isScreenOff = true;
                turnOffDisplay();
                ledSetStatus(LED_STATUS_OFF);
            }
        }
        g0HoldFired = false;
    }
    g0WasLow = g0Low;

    static unsigned long tm = 0;

    bool arrow_up = false;
    bool arrow_dw = false;
    bool arrow_ry = false;
    bool arrow_le = false;
    if (!UseTCA8418 && millis() - tm < 200 && !LongPress) return;

    if (UseTCA8418) {
        bool keyEventHandled = false;
        bool nextPulse = false;
        bool prevPulse = false;
        bool upPulse = false;
        bool downPulse = false;
        bool delPulse = false;
        bool keyPulse = false;
        keyStroke key;

        if (kb_interrupt || digitalRead(11) == LOW) {
            if (!kb_interrupt && digitalRead(11) == LOW) {
                detachInterrupt(digitalPinToInterrupt(11));
                attachInterruptArg(digitalPinToInterrupt(11), gpio_isr_handler, nullptr, CHANGE);
                Serial.println("Forcing keyboard interrupt, Restoring Interruptions.");
                kb_interrupt = true;
            }

            while (tca.available() > 0) {
                int keyEvent = tca.getEvent();
                bool pressed = (keyEvent & 0x80); // Bit 7: 1 Pressed, 0 Released
                uint8_t value = keyEvent & 0x7F;  // Bits 0-6: key value

                uint8_t row, col;
                mapRawKeyToPhysical(value, row, col);
                if (row >= 4 || col >= 14) continue;
                if (wakeUpScreen()) continue;

                AnyKeyPress = true;
                keyEventHandled = true;

                if (handleSpecialKeys(row, col, pressed) > 0) continue;

                if (!pressed) { KeyStroke.Clear(); }

                char keyVal = getKeyChar(row, col);
                if (keyVal >= 32 && keyVal < 127) {
                    tcaCharHeld[(unsigned char)keyVal] = pressed;
                }

                // Del/Backspace is text delete only — never Esc. Esc is backtick
                // (and Fn+backtick → 0xB1). Mapping Del to Esc made the PDA editor
                // open "Discard unsaved changes?" and popped menus on Backspace.
                if (keyVal == KEY_BACKSPACE && col == 13) {
                    navHoldDel = pressed;
                    if (pressed) delPulse = true;
                } else if (keyVal == '`') {
                    navHoldEsc = pressed;
                } else if (keyVal == KEY_ENTER && col == 13) {
                    navHoldSel = pressed;
                } else if (keyVal == ';') {
                    navHoldUp = pressed;
                    arrow_up = pressed;
                    if (pressed) {
                        navHoldPrev = true;
                        prevPulse = true;
                        upPulse = true;
                        navPrevRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                        navUpRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                    } else {
                        navHoldPrev = false;
                    }
                } else if (keyVal == ',') {
                    navHoldPrev = pressed;
                    arrow_le = pressed;
                    if (pressed) {
                        prevPulse = true;
                        navPrevRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                    }
                } else if (keyVal == '.') {
                    navHoldDown = pressed;
                    arrow_dw = pressed;
                    if (pressed) {
                        navHoldNext = true;
                        nextPulse = true;
                        downPulse = true;
                        navNextRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                        navDownRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                    } else {
                        navHoldNext = false;
                    }
                } else if (keyVal == '/') {
                    navHoldNext = pressed;
                    arrow_ry = pressed;
                    if (pressed) {
                        nextPulse = true;
                        navNextRepeatTime = millis() + TCA8418_REPEAT_START_MS;
                    }
                } else if (keyVal == 0xFF) {
                    key.fn = pressed;
                } else if (keyVal == KEY_LEFT_CTRL) {
                    navHoldCtrl = pressed;
                } else if (keyVal == KEY_LEFT_ALT) {
                    navHoldAlt = pressed;
                } else if (keyVal == KEY_OPT) {
                    navHoldGui = pressed;
                }

                if (!pressed) continue;

                if (navHoldGui) {
                    key.gui = true;
                    key.modifier_keys.emplace_back(KEY_OPT);
                    key.hid_keys.emplace_back(KEY_OPT);
                    keyPulse = true;
                }
                if (navHoldAlt) {
                    key.alt = true;
                    key.modifier_keys.emplace_back(KEY_LEFT_ALT);
                    key.hid_keys.emplace_back(KEY_LEFT_ALT);
                    keyPulse = true;
                }
                if (navHoldCtrl) {
                    key.ctrl = true;
                    key.modifier_keys.emplace_back(KEY_LEFT_CTRL);
                    key.hid_keys.emplace_back(KEY_LEFT_CTRL);
                    keyPulse = true;
                }
                if (shift_key_pressed) {
                    key.modifier_keys.emplace_back(KEY_LEFT_SHIFT);
                    key.hid_keys.emplace_back(KEY_LEFT_SHIFT);
                    keyPulse = true;
                }
                if (navHoldSel) {
                    key.enter = true;
                    key.exit_key = true;
                    keyPulse = true;
                }
                if (fn_key_pressed) {
                    key.fn = true;
                    keyPulse = true;
                }

                if (keyVal != 0xFF && !navHoldSel && !navHoldGui && !navHoldAlt && !navHoldCtrl &&
                    !navHoldDel && keyVal != KEY_LEFT_SHIFT) {
                    if (fn_key_pressed && arrow_up) key.word.emplace_back(0xDA);
                    else if (fn_key_pressed && arrow_dw) key.word.emplace_back(0xD9);
                    else if (fn_key_pressed && arrow_ry) key.word.emplace_back(0xD7);
                    else if (fn_key_pressed && arrow_le) key.word.emplace_back(0xD8);
                    else if (fn_key_pressed && keyVal == '`') key.word.emplace_back(0xB1);
                    else key.word.emplace_back(keyVal);
                    keyPulse = true;
                }
            }

            tca.writeRegister(TCA8418_REG_INT_STAT, 1);
            int intstat = tca.readRegister(TCA8418_REG_INT_STAT);
            if ((intstat & 0x01) == 0) { kb_interrupt = false; }
        }

        unsigned long now = millis();
        if (navHoldNext && now >= navNextRepeatTime) {
            nextPulse = true;
            navNextRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (navHoldPrev && now >= navPrevRepeatTime) {
            prevPulse = true;
            navPrevRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (navHoldUp && now >= navUpRepeatTime) {
            upPulse = true;
            navUpRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (navHoldDown && now >= navDownRepeatTime) {
            downPulse = true;
            navDownRepeatTime = now + TCA8418_REPEAT_MS;
        }

        if (!keyEventHandled && !nextPulse && !prevPulse && !upPulse && !downPulse && !LongPress) {
            navHoldSel = false;
            navHoldEsc = false;
        }

        if (delPulse) {
            key.del = true;
            key.pressed = true;
            if (fn_key_pressed) {
                key.word.emplace_back(0xD4);
                key.del = false;
                key.fn = false;
                navHoldDel = false;
                fn_key_pressed = false;
            }
            keyPulse = true;
        }

        if (keyPulse) {
            key.pressed = true;
            KeyStroke = key;
        } else if (!nextPulse && !prevPulse && !upPulse && !downPulse) {
            KeyStroke.Clear();
        }

        if (nextPulse || prevPulse || upPulse || downPulse || keyPulse) AnyKeyPress = true;

        NextPress = nextPulse;
        PrevPress = prevPulse;
        UpPress = upPulse;
        DownPress = downPulse;
        if (!SelPress) SelPress = navHoldSel;
        EscPress = navHoldEsc;
        tm = now;
    } else {
        Keyboard.update();
        if (Keyboard.isPressed()) {
            tm = millis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            keyStroke key;
            Keyboard_Class::KeysState status = Keyboard.keysState();
            for (auto i : status.hid_keys) key.hid_keys.emplace_back(i);
            for (auto i : status.word) {
                // Esc is backtick only; KEY_BACKSPACE must not cancel menus/editors.
                if (i == '`') EscPress = true;

                if (i == ';') {
                    arrow_up = true;
                    UpPress = true;
                    PrevPress = true;
                }
                if (i == '.') {
                    arrow_dw = true;
                    DownPress = true;
                    NextPress = true;
                }
                if (i == '/') {
                    arrow_ry = true;
                    NextPress = true;
                    NextPagePress = true;
                }
                if (i == ',') {
                    arrow_le = true;
                    PrevPress = true;
                    PrevPagePress = true;
                }
                if (status.fn && arrow_up) key.word.emplace_back(0xDA);
                else if (status.fn && arrow_dw) key.word.emplace_back(0xD9);
                else if (status.fn && arrow_ry) key.word.emplace_back(0xD7);
                else if (status.fn && arrow_le) key.word.emplace_back(0xD8);
                else if (status.fn && i == '`') key.word.emplace_back(0xB1);
                else key.word.emplace_back(i);
            }
            // Add CTRL, ALT and Tab to keytroke without modifier
            key.alt = status.alt;
            key.ctrl = status.ctrl;
            key.gui = status.opt;
            // Add Tab key
            if (status.tab) key.word.emplace_back(0xB3);

            for (auto i : status.modifier_keys) key.modifier_keys.emplace_back(i);
            if (status.del) key.del = true;
            if (status.enter) {
                key.enter = true;
                key.exit_key = true;
                SelPress = true;
            }
            if (status.fn) key.fn = true;
            if (key.fn && key.del) {
                key.word.emplace_back(0xD4);
                key.del = false;
                key.fn = false;
            }
            key.pressed = true;
            KeyStroke = key;
        } else KeyStroke.Clear();
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() {}

/*********************************************************************
** Function: _setup_codec_speaker
** location: modules/others/audio.cpp
** Handles audio CODEC to enable/disable speaker
**********************************************************************/
void _setup_codec_speaker(bool enable) {
    if (!UseTCA8418) return;

    static constexpr const uint8_t enabled_bulk_data[] = {
        2, 0x00, 0x80, // 0x00 RESET/  CSM POWER ON
        2, 0x01, 0xB5, // 0x01 CLOCK_MANAGER/ MCLK=BCLK
        2, 0x02, 0x18, // 0x02 CLOCK_MANAGER/ MULT_PRE=3
        2, 0x0D, 0x01, // 0x0D SYSTEM/ Power up analog circuitry
        2, 0x12, 0x00, // 0x12 SYSTEM/ power-up DAC - NOT default
        2, 0x13, 0x10, // 0x13 SYSTEM/ Enable output to HP drive - NOT default
        2, 0x32, 0xBF, // 0x32 DAC/ DAC volume (0xBF == ±0 dB )
        2, 0x37, 0x08, // 0x37 DAC/ Bypass DAC equalizer - NOT default
        0
    };
    static constexpr const uint8_t disabled_bulk_data[] = {0};

    i2c_bulk_write(&Wire1, ES8311_ADDR, enable ? enabled_bulk_data : disabled_bulk_data);
}

/*********************************************************************
** Function: isCharging
** Heuristic: USB presence + pack voltage trend (no PMIC current sense).
**********************************************************************/
bool isCharging() {
    ChargeState s = readChargeInfo().state;
    return s == CHARGE_CHARGING || s == CHARGE_FULL;
}

/*********************************************************************
** Function: _setup_codec_mic
** location: modules/others/mic.cpp
** Handles audio CODEC to enable/disable microphone
**********************************************************************/
void _setup_codec_mic(bool enable) {
    if (!UseTCA8418) return;
    // Set microfone pin for ADV
    mic_bclk_pin = (gpio_num_t)41;

    static constexpr const uint8_t enabled_bulk_data[] = {
        2, 0x00, 0x80, // 0x00 RESET/  CSM POWER ON
        2, 0x01, 0xBA, // 0x01 CLOCK_MANAGER/ MCLK=BCLK
        2, 0x02, 0x18, // 0x02 CLOCK_MANAGER/ MULT_PRE=3
        2, 0x0D, 0x01, // 0x0D SYSTEM/ Power up analog circuitry
        2, 0x0E, 0x02, // 0x0E SYSTEM/ : Enable analog PGA, enable ADC modulator
        2, 0x14, 0x10, // ES8311_ADC_REG14 : select Mic1p-Mic1n / PGA GAIN (minimum)
        2, 0x17, 0xBF, // ES8311_ADC_REG17 : ADC_VOLUME 0xBF == ± 0 dB
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

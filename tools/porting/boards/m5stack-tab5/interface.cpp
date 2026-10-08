#include "root/hal/bus_HAL.h"
#include "root/app/powerSave.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include "root/ui/kvx_main_menu.h"
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>
#include <cctype>
#include <cstring>
#include <globals.h>
#include <interface.h>

// Touch gestures (Tab5-only — do not call shared touchHeatMap)
static constexpr uint32_t kTouchHoldEscMs = 3000;
static constexpr int16_t kSwipeMinDx = 60; // slightly easier on 1280-wide Tab5
static constexpr float kSwipeHorizRatio = 1.2f;
static constexpr int16_t kTapMaxMove = 40;

static m5::unit::UnitUnified Units;
static m5::unit::UnitTab5Keyboard tab5Kb;
static bool s_kbReady = false;
static TwoWire *s_kbWire = nullptr;

static bool s_touchDown = false;
static bool s_holdEscFired = false;
static uint32_t s_touchDownAt = 0;
static int16_t s_touchStartX = 0;
static int16_t s_touchStartY = 0;
static int16_t s_touchLastX = 0;
static int16_t s_touchLastY = 0;

static void tab5EnableExt5V(bool on) {
    // Prefer M5Unified power API (PI4IOE5V6408-1 P2 EXT5V_EN).
    M5.Power.setExtOutput(on);
}

static TwoWire *pickKeyboardWire() {
    // Sys peripherals (touch/PMIC/…) live on M5.In_I2C — never rebind that bus.
    const i2c_port_t sysPort = M5.In_I2C.getPort();
    if (sysPort == I2C_NUM_0) return &Wire1;
    return &Wire;
}

static bool setupTab5Keyboard() {
    s_kbWire = pickKeyboardWire();
    auto cfg = tab5Kb.config();
    cfg.mode = m5::unit::tab5_keyboard::Mode::Character;
    cfg.start_periodic = true;
    cfg.irq_pin = TAB5_KB_INT;
    tab5Kb.config(cfg);

    pinMode(TAB5_KB_INT, INPUT_PULLUP);

    s_kbWire->begin(TAB5_KB_SDA, TAB5_KB_SCL);
    if (!Units.add(tab5Kb, *s_kbWire) || !Units.begin()) {
        Serial.println("[Tab5] Keyboard probe failed (touch-only OK)");
        s_kbReady = false;
        return false;
    }
    // Ensure Character even if the unit woke in HID/Normal.
    tab5Kb.writeMode(m5::unit::tab5_keyboard::Mode::Character);
    s_kbReady = true;
    Serial.printf("[Tab5] Keyboard ready fw=0x%02X\n", tab5Kb.firmwareVersion());
    return true;
}

static bool eqIgnoreCase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

// Map one Character-mode string (or control byte) into KeyStroke / nav flags.
static void applyCharacterToken(const char *chars, uint8_t length, uint8_t modifier) {
    if (!chars || length == 0) return;

    keyStroke key;
    key.pressed = true;
    key.ctrl = (modifier & 0x01) != 0;
    key.alt = (modifier & 0x04) != 0;

    // Multi-byte name tokens used by some Character firmwares
    if (length > 1 || (length == 1 && !isprint((unsigned char)chars[0]) && chars[0] != '\b' &&
                       chars[0] != 0x7F && chars[0] != '\r' && chars[0] != '\n' && chars[0] != '\t' &&
                       chars[0] != 0x1B)) {
        if (eqIgnoreCase(chars, "Esc") || eqIgnoreCase(chars, "Escape")) {
            EscPress = true;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Enter") || eqIgnoreCase(chars, "Return")) {
            key.enter = true;
            key.exit_key = true;
            SelPress = true;
            KeyStroke = key;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Backspace") || eqIgnoreCase(chars, "Delete") ||
            eqIgnoreCase(chars, "Bksp")) {
            key.del = true;
            KeyStroke = key;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Up") || eqIgnoreCase(chars, "UP")) {
            UpPress = true;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Down") || eqIgnoreCase(chars, "DOWN") || eqIgnoreCase(chars, "Dn")) {
            DownPress = true;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Left") || eqIgnoreCase(chars, "LEFT") || eqIgnoreCase(chars, "Lt")) {
            PrevPress = true;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Right") || eqIgnoreCase(chars, "RIGHT") || eqIgnoreCase(chars, "Rt")) {
            NextPress = true;
            AnyKeyPress = true;
            return;
        }
        if (eqIgnoreCase(chars, "Tab")) {
            key.word.emplace_back((char)0xB3);
            KeyStroke = key;
            AnyKeyPress = true;
            return;
        }
    }

    for (uint8_t i = 0; i < length && chars[i] != '\0'; i++) {
        const char c = chars[i];
        switch (c) {
            case 0x1B:
                EscPress = true;
                break;
            case '\r':
            case '\n':
                key.enter = true;
                key.exit_key = true;
                SelPress = true;
                break;
            case '\b':
            case 0x7F:
                key.del = true;
                break;
            case '\t':
                key.word.emplace_back((char)0xB3);
                break;
            default:
                if (isprint((unsigned char)c)) key.word.emplace_back(c);
                break;
        }
    }

    if (key.enter || key.del || !key.word.empty() || key.ctrl || key.alt) {
        KeyStroke = key;
    }
    AnyKeyPress = true;
}

static void drainKeyboard() {
    if (!s_kbReady) return;
    // INT active-low: skip I2C when high (no pending events).
    if (digitalRead(TAB5_KB_INT) == HIGH) return;

    Units.update();
    while (!tab5Kb.empty()) {
        const auto evt = tab5Kb.oldest();
        if (evt.type == m5::unit::tab5_keyboard::EventType::Character) {
            applyCharacterToken(evt.chr.chars, evt.chr.length, evt.modifier);
        }
        // Character mode only for M1; Normal/HID event shapes differ by library version.
        tab5Kb.discard();
    }
}

static void applyTouchGesture(int16_t x, int16_t y, int16_t dx, int16_t dy, uint32_t heldMs, bool released) {
    if (!released) {
        if (!s_holdEscFired && heldMs >= kTouchHoldEscMs) {
            s_holdEscFired = true;
            EscPress = true;
            AnyKeyPress = true;
        }
        return;
    }

    if (s_holdEscFired) return; // hold already consumed this contact

    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;

    if (adx >= kSwipeMinDx && (float)adx > (float)ady * kSwipeHorizRatio) {
        // Horizontal swipe → left/right navigation (Prev/Next).
        // Main grid: move across columns / pages. Submenus: Prev/Next rows.
        if (kvxMainMenuActive() || menuOptionType == MENU_TYPE_SUBMENU) {
            if (dx < 0) NextPress = true; // swipe left → next
            else PrevPress = true;        // swipe right → previous
            AnyKeyPress = true;
        }
        return;
    }

    if (adx < kTapMaxMove && ady < kTapMaxMove) {
        if (kvxMainMenuActive()) {
            int idx = kvxMainMenuIndexAt(x, y);
            if (idx >= 0) {
                kvxMainMenuSelectIndex(idx);
                SelPress = true;
                AnyKeyPress = true;
            }
        } else if (menuOptionType == MENU_TYPE_SUBMENU) {
            // Row tap handled in loopOptions via touchPoint (see display.cpp)
            touchPoint.x = (uint16_t)x;
            touchPoint.y = (uint16_t)y;
            touchPoint.pressed = true;
            AnyKeyPress = true;
        }
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Display path (docs.m5stack.com/en/core/Tab5): M5Unified → M5GFX MIPI-DSI
** (Panel_ST7121 / Panel_ST7123 / Panel_ILI9881C) with PWM backlight on G22.
***************************************************************************************/
void _setup_gpio() {
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = false; // EXT5V off until IR/module menus need it
    // M1: skip codec/mic bring-up (ES8388/ES7210 share sys I2C with LCD_RST/PI4IO).
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_imu = false;
    // RX8130 is fine under M5Unified; our HAS_RTC BM8563 path stays off.
    cfg.internal_rtc = true;
    cfg.fallback_board = m5::board_t::board_M5Tab5;

    M5.begin(cfg);
    setSysI2CBus(M5.In_I2C.getPort() == I2C_NUM_1 ? &Wire1 : &Wire);

    // Panel is native 720×1280; ROTATION=3 → landscape with keyboard along the bottom.
    M5.Display.setRotation(ROTATION);
    M5.Display.setBrightness(200);

    // Direct-to-panel draws — avoid allocating a full 720p RGB565 canvas (~1.8MB).
    // Kept suppressed for the lifetime of the firmware (canvas re-enable + hosted
    // SDIO WiFi races MIPI-DSI and shows a solid cyan crash screen).
    tft.suppressCanvas(true);

    // C6 WLAN_PWR_EN is raised in M5.Power.begin; give the slave a head start
    // before the user opens WiFi Connect (hosted SDIO handshake).
    delay(50);

    Serial.printf(
        "[Tab5] board=%d display %dx%d panel=%s\n",
        (int)M5.getBoard(),
        M5.Display.width(),
        M5.Display.height(),
        M5.Display.width() > 0 ? "ok" : "FAIL"
    );

    // microSD CS idle-high (SPI mode — not SDIO; C6 owns the SDIO host)
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    kvxConfig.colorInverted = 0;
}

void _post_setup_gpio() {
    // Keyboard after display so G0/G1 Wire probe cannot race PI4IO LCD_RST on sys I2C.
    setupTab5Keyboard();
}

void _setBrightness(uint8_t brightval) {
    // M5GFX PWM on G22 (LEDA); map 0–100 UI scale to 0–255 panel scale.
    uint8_t pwm = (uint8_t)((brightval >= 100) ? 255 : (brightval * 255) / 100);
    if (brightval > 0 && pwm == 0) pwm = 1;
    M5.Display.setBrightness(pwm);
}

int getBattery() {
    int level = M5.Power.getBatteryLevel();
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

bool isCharging() { return M5.Power.isCharging(); }

void powerOff() { M5.Power.powerOff(); }

void checkReboot() {}

/*********************************************************************
** Function: InputHandler
** Keyboard (Character mode) + Tab5 touch gestures (tap / swipe / 3s Esc).
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    const unsigned long now = millis();
    if (now - tm < 30 && !LongPress && !s_touchDown) return;

    // Keyboard path (own I2C bus)
    if (s_kbReady) {
        if (!wakeUpScreen()) {
            drainKeyboard();
        } else {
            // Woke screen — still drain to clear INT, but drop nav pulses
            KeyStroke.Clear();
            drainKeyboard();
            EscPress = NextPress = PrevPress = UpPress = DownPress = SelPress = false;
            AnyKeyPress = false;
            return;
        }
    }

    // Touch path (sys I2C via M5.update)
    if (!trylockSysI2CBus()) return;
    M5.update();
    unlockSysI2CBus();

    auto t = M5.Touch.getDetail();
    const bool pressed = t.isPressed() || t.isHolding();

    if (pressed) {
        tm = now;
        int16_t x = t.x;
        int16_t y = t.y;
        // Match Core2 rotation remap for landscape default ROTATION=1
        if (kvxConfigPins.rotation == 3) {
            y = (tftHeight + 20) - y;
            x = tftWidth - x;
        } else if (kvxConfigPins.rotation == 0) {
            int tmp = x;
            x = tftWidth - y;
            y = tmp;
        } else if (kvxConfigPins.rotation == 2) {
            int tmp = x;
            x = y;
            y = (tftHeight + 20) - tmp;
        }

        if (!s_touchDown) {
            if (wakeUpScreen()) return;
            AnyKeyPress = true;
            s_touchDown = true;
            s_holdEscFired = false;
            s_touchDownAt = now;
            s_touchStartX = x;
            s_touchStartY = y;
        }
        s_touchLastX = x;
        s_touchLastY = y;
        touchPoint.x = (uint16_t)x;
        touchPoint.y = (uint16_t)y;
        touchPoint.pressed = true;

        applyTouchGesture(
            x, y, (int16_t)(x - s_touchStartX), (int16_t)(y - s_touchStartY), now - s_touchDownAt, false
        );
    } else if (s_touchDown) {
        tm = now;
        applyTouchGesture(
            s_touchLastX,
            s_touchLastY,
            (int16_t)(s_touchLastX - s_touchStartX),
            (int16_t)(s_touchLastY - s_touchStartY),
            now - s_touchDownAt,
            true
        );
        s_touchDown = false;
        s_holdEscFired = false;
        touchPoint.Clear();
    }
}

// Called from Infrared menu (Tab5 Grove modules need EXT 5V).
void tab5SetExt5V(bool on) { tab5EnableExt5V(on); }

bool tab5GetExt5V(void) { return M5.Power.getExtOutput(); }

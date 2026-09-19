#include "charge_screen.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/ui/theme.h"
#include "root/app/utils.h"
#include "root/app/powerSave.h"
#include "root/input/mykeyboard.h"
#ifdef HAS_RGB_LED
#include "root/hal/led_control.h"
#endif
#include <globals.h>
#include <interface.h>
#include <string.h>

extern void _tone(unsigned int frequency, unsigned long duration);

static const uint16_t kClockColors[] = {
    DEFAULT_PRICOLOR, // purple
    DEFAULT_SECCOLOR, // green
    0xFD20,           // orange
    0xFC9F,           // pink
    0x07FF,           // cyan
    0xFFE0,           // yellow
    0xF800,           // red
    0xFFFF,           // white
};
static const int kClockColorCount = (int)(sizeof(kClockColors) / sizeof(kClockColors[0]));

static uint8_t stepBrightness(uint8_t cur, int dir) {
    int next = dir > 0 ? (int)cur + 10 : (int)cur - 10;
    if (next < 1) next = 1;
    if (next > 100) next = 100;
    return (uint8_t)next;
}

static uint16_t chargeLevelColor(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent < 25) return 0xF800;           // red
    if (percent < 50) return 0xFD20;           // orange
    if (percent < 75) return 0xFFE0;           // yellow
    if (percent < 95) return 0x07E0;           // green
    return DEFAULT_PRICOLOR;                  // purple (95–100%)
}

static void formatChargeDate(const struct tm &t, char *out, size_t outLen) {
    static const char *kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *kMon[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int wday = t.tm_wday;
    int mon = t.tm_mon;
    if (wday < 0 || wday > 6) wday = 0;
    if (mon < 0 || mon > 11) mon = 0;
    int mday = t.tm_mday;
    if (mday < 1) mday = 1;
    if (mday > 31) mday = 31;
    snprintf(out, outLen, "%s %d %s %d", kDow[wday], mday, kMon[mon], t.tm_year + 1900);
}

static int daysInMonth(int year, int mon0) {
    static const int kDim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon0 < 0 || mon0 > 11) return 31;
    int dim = kDim[mon0];
    if (mon0 == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;
    return dim;
}

static void drawMonthCalendar(int x, int y, int w, int h, const struct tm &t, uint16_t color, uint16_t bg) {
    if (w < 56 || h < 36) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > tftWidth) w = tftWidth - x;
    if (y + h > tftHeight) h = tftHeight - y;
    if (w < 56 || h < 36) return;

    tft.fillRect(x, y, w, h, bg);
    const char *hdr[] = {"S", "M", "T", "W", "T", "F", "S"};
    const int dense = uiDenseFont();
    const int glyphH = uiLineH(dense);
    int cellW = w / 7;
    int headerH = max(8, glyphH + 2);
    int rows = h >= 54 ? 6 : 5;
    int cellH = (h - headerH) / rows;
    if (cellW < 8 || cellH < 6) return;

    tft.setTextSize(dense);
    tft.setTextColor(color, bg);
    for (int c = 0; c < 7; c++) {
        tft.drawCentreString(hdr[c], x + c * cellW + cellW / 2, y, 1);
    }

    int year = t.tm_year + 1900;
    int dim = daysInMonth(year, t.tm_mon);
    int mday = t.tm_mday;
    int wday = t.tm_wday;
    if (mday < 1) mday = 1;
    if (wday < 0 || wday > 6) wday = 0;
    int first = (wday - ((mday - 1) % 7) + 7) % 7;
    int d = 1;
    for (int r = 0; r < rows && d <= dim; r++) {
        for (int c = 0; c < 7; c++) {
            int slot = r * 7 + c;
            if (slot < first || d > dim) continue;
            int cx = x + c * cellW;
            int cy = y + headerH + r * cellH;
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", d);
            int ty = cy + (cellH > glyphH ? (cellH - glyphH) / 2 : 0);
            if (d == mday) {
                int rw = cellW - 2;
                int rh = cellH - 1;
                if (rw > 2 && rh > 2) tft.fillRect(cx + 1, cy, rw, rh, color);
                tft.setTextColor(bg, color);
                tft.drawCentreString(buf, cx + cellW / 2, ty, 1);
                tft.setTextColor(color, bg);
            } else {
                tft.drawCentreString(buf, cx + cellW / 2, ty, 1);
            }
            d++;
        }
    }
}

static void drawField(
    int x, int y, int w, int h, const char *text, char *last, size_t lastLen, uint8_t size, uint16_t color,
    uint16_t bg, bool centre
) {
    if (!text || !last) return;
    if (w < 1 || h < 1) return;
    if (strcmp(text, last) == 0) return;
    tft.fillRect(x, y, w, h, bg);
    tft.setTextSize(size);
    tft.setTextColor(color, bg);
    if (centre) tft.drawCentreString(text, x + w / 2, y, 1);
    else {
        tft.setCursor(x, y);
        tft.print(text);
    }
    strncpy(last, text, lastLen - 1);
    last[lastLen - 1] = '\0';
}

static void drawChargeBar(int x, int y, int w, int h, int percent, uint16_t col, uint16_t bg) {
    if (w < 10 || h < 6) return;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    tft.drawRoundRect(x, y, w, h, 3, col);
    int innerW = w - 4;
    int innerH = h - 4;
    if (innerW < 1 || innerH < 1) return;
    tft.fillRect(x + 2, y + 2, innerW, innerH, bg);
    int fill = innerW * percent / 100;
    if (fill > 0) tft.fillRect(x + 2, y + 2, fill, innerH, col);
}

#ifdef HAS_RGB_LED
static constexpr uint8_t CHARGE_SLEEP_LED_BRIGHT = 51; // ~20% of 255

static void chargeLedUpdate(const ChargeInfo &info, bool ledOn, bool screenOff) {
    ledPauseEffects(true);
    // L toggles LED while blanked; otherwise sleep keeps dim battery color on.
    if (!ledOn) {
        ledShowApp(0, 0, 0, 0);
        return;
    }
    if (screenOff) {
        CRGB base = batteryStatusLedColor(info.percent);
        if (info.percent <= 5 && ((millis() / 500) % 2 == 0)) {
            ledShowApp(0, 0, 0, 0);
            return;
        }
        ledShowApp(base.r, base.g, base.b, CHARGE_SLEEP_LED_BRIGHT);
        return;
    }
    uint8_t bright = kvxConfig.ledBright > 0 ? (uint8_t)(255 * kvxConfig.ledBright / 100) : 128;
    CRGB base = batteryStatusLedColor(info.percent);
    if (info.percent <= 5 && ((millis() / 500) % 2 == 0)) {
        ledShowApp(0, 0, 0, bright);
        return;
    }
    float phase = (sinf(millis() / 1400.0f * PI) + 1.0f) * 0.5f;
    uint8_t scale = (info.state == CHARGE_CHARGING || info.state == CHARGE_FULL)
                        ? (uint8_t)(140 + phase * 115)
                        : (uint8_t)200;
    CRGB c = base;
    c.r = (uint8_t)((uint16_t)c.r * scale / 255);
    c.g = (uint8_t)((uint16_t)c.g * scale / 255);
    c.b = (uint8_t)((uint16_t)c.b * scale / 255);
    ledShowApp(c.r, c.g, c.b, bright);
}
#endif

static void chargeGoIdleOff(bool *ledOn) {
    // Blank panel only; keep LED on at sleep brightness (L still toggles).
    (void)ledOn;
    chargeUserSleep = true;
    isScreenOff = true;
    dimmer = false;
    setBrightness(0, false);
    // Kill sticky '.' auto-repeat so wake/exit cannot keep seeing DownPress.
    resetHeldNavKeys();
}

static void chargeWakeDisplay(uint8_t bright, bool *ledOn) {
    chargeUserSleep = false;
    isScreenOff = false;
    dimmer = false;
    if (bright < 1) bright = 1;
    setBrightness(bright, false);
    resetHeldNavKeys();
    if (ledOn) *ledOn = true;
#ifdef HAS_RGB_LED
    ledPauseEffects(true);
#endif
}

static void chargeExit() {
#ifdef HAS_RGB_LED
    ledPauseEffects(false);
#endif
    chargeUserSleep = false;
    chargeModeActive = false;
    chargeModeBright = -1;
    isScreenOff = false;
    dimmer = false;
    resetHeldNavKeys();
    setBrightness(kvxConfig.bright, false);
#ifdef HAS_RGB_LED
    ledSetup();
#endif
    tft.fillScreen(kvxConfig.bgColor);
    returnToMenu = true;
}

static void chargeClearInput() {
    check(SelPress);
    check(UpPress);
    check(DownPress);
    check(PrevPress);
    check(NextPress);
    check(EscPress);
    check(AnyKeyPress);
    KeyStroke.Clear();
    resetHeldNavKeys();
}

void runChargeLoop() {
    // Arm charge mode BEFORE any brightness write so _setBrightness uses the
    // nightstand ramp and the power-saver cannot fade the panel out.
    chargeModeActive = true;
    chargeUserSleep = false;
    isScreenOff = false;
    dimmer = false;
    previousMillis = millis();

    // Use normal UI brightness (floor 25%) — 10% on a black fill looks "off".
    uint8_t chargeBright = kvxConfig.bright;
    if (chargeBright < 25) chargeBright = 25;
    if (chargeBright > 100) chargeBright = 100;
    chargeModeBright = (int)chargeBright;

    panelSleep(false);
    setBrightness(chargeBright, false);

    unsigned long enteredAt = millis();
    chargeClearInput();
    // Ignore stale blank flags from G0/power-save during hand-off.
    chargeUserSleep = false;
    isScreenOff = false;
    previousMillis = millis();

    unsigned long lastRefresh = 0;
    bool screenOff = false;
    bool ledOn = true;
    bool firstPaint = true;
    bool fullHold = false;
    bool fullBeeped = false;
    ChargeState prevState = CHARGE_BATTERY;
    bool forceRedraw = true;
    bool showCalendar = true;
    int colorIndex = 0;
    int lastCalDay = -1;
    int lastCalMon = -1;
    // Only honor G0 blanking after the user has been in-app for a bit, so a
    // GPIO0 glitch or leftover isScreenOff cannot black the panel on open.
    const unsigned long INPUT_GRACE_MS = 800;
    // After wake, ignore Down briefly so a sticky '.' repeat cannot re-blank.
    unsigned long ignoreDownUntil = 0;

    static char lastClock[16];
    static char lastDate[24];
    static char lastHint[40];
    static char lastFoot[40];
    static char lastPct[8];
    static int lastBarPct = -1;
    lastClock[0] = lastDate[0] = lastHint[0] = lastFoot[0] = lastPct[0] = '\0';
    lastBarPct = -1;

    for (;;) {
        previousMillis = millis();
        const bool pastGrace = (millis() - enteredAt) > INPUT_GRACE_MS;
        const bool ignoreDown = millis() < ignoreDownUntil;

        // G0 tap (InputHandler) sets chargeUserSleep — join after grace only.
        // Sleep blanks the panel; LED stays dim battery-colored (L still toggles).
        if (!screenOff && pastGrace && chargeUserSleep) {
            screenOff = true;
            isScreenOff = true;
            dimmer = false;
            setBrightness(0, false);
            resetHeldNavKeys();
            ignoreDownUntil = millis() + 600;
        }

        if (screenOff) {
            bool wake = false;
            bool toggleLed = false;

            if (check(EscPress)) wake = true;
            else if (check(SelPress)) wake = true;
            else if (check(UpPress) || check(PrevPress)) wake = true;
            else if (!ignoreDown && check(DownPress)) {
                // Down while blank: keep the panel off (LED stays unless L toggled it).
                check(NextPress);
                resetHeldNavKeys();
                ignoreDownUntil = millis() + 600;
            }
#ifdef HAS_KEYBOARD
            else if (AnyKeyPress) {
                char letter = checkLetterShortcutPress();
                if (letter == 's' || letter == 'S') {
                    check(AnyKeyPress);
                    wake = true;
                } else if (letter == 'l' || letter == 'L') {
                    check(AnyKeyPress);
                    toggleLed = true;
                }
            }
#endif
            // G0 wake path clears chargeUserSleep + isScreenOff in InputHandler.
            if (!chargeUserSleep && !isScreenOff) wake = true;

            if (toggleLed) ledOn = !ledOn;

            ChargeInfo idleInfo = readChargeInfo();
#ifdef HAS_RGB_LED
            chargeLedUpdate(idleInfo, ledOn, true);
#endif
            if (wake) {
                chargeWakeDisplay(chargeBright, &ledOn);
                screenOff = false;
                forceRedraw = true;
                ignoreDownUntil = millis() + 600;
                continue; // do not fall through into Down→sleep same frame
            }
            prevState = idleInfo.state;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (pastGrace && (check(EscPress) || forceHome)) {
            chargeExit();
            return;
        }
        if (!pastGrace) {
            check(EscPress);
            check(DownPress);
            check(NextPress);
            check(UpPress);
            check(PrevPress);
            check(SelPress);
            check(AnyKeyPress);
        }

#ifdef HAS_KEYBOARD
        if (pastGrace && AnyKeyPress) {
            char letter = checkLetterShortcutPress();
            if (letter == 's' || letter == 'S') {
                check(AnyKeyPress);
                chargeGoIdleOff(&ledOn);
                screenOff = true;
                ignoreDownUntil = millis() + 600;
                continue;
            } else if (letter == 'c' || letter == 'C') {
                check(AnyKeyPress);
                showCalendar = !showCalendar;
                forceRedraw = true;
            } else if (letter == 'l' || letter == 'L') {
                check(AnyKeyPress);
                ledOn = !ledOn;
            }
        }
#endif

        // Down tap: blank display, stay off until Up/Esc/S/G0 wake.
        if (pastGrace && !ignoreDown && check(DownPress)) {
            check(NextPress); // '.' also pulses Next on Cardputer
            chargeGoIdleOff(&ledOn);
            screenOff = true;
            ignoreDownUntil = millis() + 600;
            continue;
        }

        int brightDir = 0;
        if (pastGrace && (check(UpPress) || check(PrevPress))) brightDir = +1;
        else if (pastGrace && !ignoreDown && check(NextPress)) brightDir = -1;
#ifdef HAS_ENCODER
        int32_t rot = drainRotarySteps();
        if (pastGrace && rot != 0) brightDir = rot > 0 ? +1 : -1;
#endif
        if (brightDir != 0) {
            chargeBright = stepBrightness(chargeBright, brightDir);
            chargeModeBright = chargeBright;
            setBrightness(chargeBright, false);
            if (fullHold) fullHold = false;
        }

        if (pastGrace && check(SelPress)) {
            colorIndex = (colorIndex + 1) % kClockColorCount;
            forceRedraw = true;
            if (fullHold) fullHold = false;
        } else if (pastGrace) {
            check(AnyKeyPress);
        }

        ChargeInfo info = readChargeInfo();
#ifdef HAS_RGB_LED
        chargeLedUpdate(info, ledOn, false);
#endif
        if (info.state == CHARGE_FULL && prevState != CHARGE_FULL) {
            fullHold = true;
#if defined(HAS_NS4168_SPKR)
            if (!fullBeeped) {
                _tone(1800, 180);
                fullBeeped = true;
            }
#endif
        }
        if (info.state != CHARGE_FULL) {
            fullHold = false;
            fullBeeped = false;
        }
        prevState = info.state;

        if (firstPaint || forceRedraw || millis() - lastRefresh > 1000) {
            lastRefresh = millis();
            if (firstPaint) setBrightness(chargeBright, false);
            uint16_t batCol = chargeLevelColor(info.percent);
            uint16_t clockColor = kClockColors[colorIndex];
            uint16_t bg = KVX_DEFAULT_BGCOLOR;

            if (forceRedraw || firstPaint) {
                tft.fillScreen(bg);
                lastClock[0] = lastDate[0] = lastHint[0] = lastFoot[0] = lastPct[0] = '\0';
                lastCalDay = -1;
                lastCalMon = -1;
                lastBarPct = -1;
            }

#if defined(HAS_RTC)
            struct tm nowTm = _rtc.getTimeStruct();
#else
            struct tm nowTm = rtc.getTimeStruct();
#endif
            updateTimeStr(nowTm);

            char dateBuf[24];
            formatChargeDate(nowTm, dateBuf, sizeof(dateBuf));

            int pct = info.percent;
            if (pct < 1) pct = 1;
            if (pct > 100) pct = 100;
            char pctBuf[8];
            snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);

            const int kFootH = 10;
            const int kHintH = 10;
            const int kBarH = 12;
            int hintY = tftHeight - kFootH - kHintH;
            int barY = hintY - kBarH - 3;

            if (showCalendar) {
                drawField(4, 1, tftWidth - 8, 16, dateBuf, lastDate, sizeof(lastDate), FM, clockColor, bg, true);

                uint8_t clockSize = 3;
                int clockLen = (int)strlen(timeStr);
                if (clockLen < 1) clockLen = 8;
                while (clockSize > 1 && clockSize * 6 * clockLen > tftWidth - 56) clockSize--;
                int clockY = 18;
                int clockH = clockSize * 8 + 2;
                drawField(
                    2, clockY, tftWidth - 52, clockH, timeStr, lastClock, sizeof(lastClock), clockSize,
                    clockColor, bg, true
                );
                drawField(
                    tftWidth - 50, clockY + 4, 48, 16, pctBuf, lastPct, sizeof(lastPct), FM, batCol, bg, true
                );

                int calY = clockY + clockH + 6;
                int calH = barY - calY - 3;
                if (showCalendar && calH >= 36 &&
                    (forceRedraw || nowTm.tm_mday != lastCalDay || nowTm.tm_mon != lastCalMon)) {
                    drawMonthCalendar(4, calY, tftWidth - 8, calH, nowTm, clockColor, bg);
                    lastCalDay = nowTm.tm_mday;
                    lastCalMon = nowTm.tm_mon;
                }
                if (forceRedraw) lastBarPct = -1;
                if (forceRedraw || pct != lastBarPct) {
                    drawChargeBar(8, barY, tftWidth - 16, kBarH, pct, batCol, bg);
                    lastBarPct = pct;
                }
            } else {
                lastCalDay = -1;
                uint8_t dateSize = FG;
                while (dateSize > 1 && dateSize * 6 * (int)strlen(dateBuf) > tftWidth - 8) dateSize--;
                int dateH = dateSize * 8 + 2;
                drawField(
                    4, 4, tftWidth - 8, dateH, dateBuf, lastDate, sizeof(lastDate), dateSize, clockColor, bg,
                    true
                );

                uint8_t clockSize = 5;
                int clockLen = (int)strlen(timeStr);
                if (clockLen < 1) clockLen = 8;
                while (clockSize > 1 && clockSize * 6 * clockLen > tftWidth - 8) clockSize--;
                int clockY = 4 + dateH + 4;
                int clockH = clockSize * 8 + 2;
                drawField(
                    2, clockY, tftWidth - 4, clockH, timeStr, lastClock, sizeof(lastClock), clockSize,
                    clockColor, bg, true
                );

                uint8_t pctSize = FG;
                int pctY = clockY + clockH + 6;
                int pctH = pctSize * 8 + 2;
                if (pctY + pctH > barY - 4) {
                    pctSize = FM;
                    pctH = pctSize * 8 + 2;
                }
                drawField(
                    4, pctY, tftWidth - 8, pctH, pctBuf, lastPct, sizeof(lastPct), pctSize, batCol, bg, true
                );

                int bigBarY = pctY + pctH + 6;
                int bigBarH = 18;
                if (bigBarY + bigBarH > hintY - 2) {
                    bigBarY = barY;
                    bigBarH = kBarH;
                }
                if (forceRedraw) lastBarPct = -1;
                if (forceRedraw || pct != lastBarPct) {
                    drawChargeBar(10, bigBarY, tftWidth - 20, bigBarH, pct, batCol, bg);
                    lastBarPct = pct;
                }
            }

            char hintBuf[40];
            uint16_t hintCol = batCol;
            if (info.state == CHARGE_USB) {
                strncpy(hintBuf, "Flip the charge switch", sizeof(hintBuf) - 1);
                hintBuf[sizeof(hintBuf) - 1] = '\0';
                hintCol = TFT_YELLOW;
            } else {
                snprintf(
                    hintBuf,
                    sizeof(hintBuf),
                    "%s  %d.%02dV",
                    chargeStateLabel(info.state),
                    info.milliVolts / 1000,
                    (info.milliVolts % 1000) / 10
                );
            }
            drawField(4, hintY, tftWidth - 8, kHintH, hintBuf, lastHint, sizeof(lastHint), FP, hintCol, bg, true);

            drawField(
                4,
                tftHeight - kFootH,
                tftWidth - 8,
                kFootH,
                "S/. sleep  L LED  C cal  ESC",
                lastFoot,
                sizeof(lastFoot),
                FP,
                TFT_DARKGREY,
                bg,
                true
            );

            forceRedraw = false;
            firstPaint = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

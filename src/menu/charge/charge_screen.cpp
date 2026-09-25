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
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    return (uint8_t)next;
}

#ifdef HAS_KEYBOARD
static bool chargeLetterEdge(char c, bool *wasHeld, bool armed) {
    bool phys = isCardputerKeyHeld(c);
    bool edge = armed && phys && !*wasHeld;
    *wasHeld = phys;
    return edge;
}
#endif

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
                const uint16_t accent = kvxConfig.secColor;
                if (rw > 2 && rh > 2) tft.fillRect(cx + 1, cy, rw, rh, accent);
                tft.setTextColor(bg, accent);
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
static constexpr uint8_t CHARGE_LED_BRIGHT = 51; // 20% of 255; never follows panel brightness

static int chargeLedStablePercent(int percent, bool reset = false) {
    static int held = -1;
    if (reset) {
        held = -1;
        return -1;
    }
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    if (held < 0) {
        held = percent;
        return held;
    }
    // Wide hysteresis so ADC noise around the 95% purple/green band cannot flicker.
    if (percent >= held + 12 || percent <= held - 12) held = percent;
    return held;
}

static void chargeLedPaint(int percent, bool ledOn, bool force) {
    static bool lastOn = false;
    static uint8_t lastR = 255, lastG = 255, lastB = 255;

    CRGB c = CRGB(0, 0, 0);
    if (ledOn) c = batteryStatusLedColor(percent);
    const bool changed = (ledOn != lastOn) || (c.r != lastR) || (c.g != lastG) || (c.b != lastB);
    if (!force && !changed) return;

    lastOn = ledOn;
    lastR = c.r;
    lastG = c.g;
    lastB = c.b;
    // Always 20% while Charge owns the LED; L only toggles on/off, never brightness.
    // Exclusive path only shows when color/on changes — no periodic FastLED.show.
    ledShowApp(c.r, c.g, c.b, ledOn ? CHARGE_LED_BRIGHT : 0);
}

static void chargeLedSync(int percent, bool ledOn, bool force) {
    chargeLedPaint(chargeLedStablePercent(percent), ledOn, force);
}
#endif

static void chargeGoIdleOff(bool *ledOn) {
    // Panel off only. LED stays at current ledOn — L is the only off switch.
    chargeUserSleep = true;
    isScreenOff = true;
    dimmer = false;
    setBrightness(0, false);
#ifdef HAS_RGB_LED
    delay(5);
    ChargeInfo info = readChargeInfo();
    const bool keepLed = (ledOn != nullptr) ? *ledOn : true;
    chargeLedSync(info.percent, keepLed, false);
    ledKeepRequest();
#else
    (void)ledOn;
#endif
    resetHeldNavKeys();
}

static void chargeWakeDisplay(uint8_t bright, bool *ledOn) {
    (void)ledOn;
    chargeUserSleep = false;
    isScreenOff = false;
    dimmer = false;
    if (bright < 1) bright = 1;
    setBrightness(bright, false);
    resetHeldNavKeys();
#ifdef HAS_RGB_LED
    ledPauseEffects(true);
#endif
}

static void chargeExit() {
#ifdef HAS_RGB_LED
    ledReleaseExclusive();
    ledSuppressStatus(false);
    ledPauseEffects(false);
#endif
    tftSuppressCanvas(false);
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
    // Long grace so sticky Down/G0/`.` from the menu cannot blank on open.
    chargeInputGraceUntil = millis() + 2500;

#ifdef HAS_RGB_LED
    ledTakeExclusive();
    ledSuppressStatus(true);
    ledPauseEffects(true);
    chargeLedStablePercent(0, true);
#endif

    uint8_t chargeBright = kvxConfig.bright;
    if (chargeBright < 1) chargeBright = 1;
    if (chargeBright > 100) chargeBright = 100;
    chargeModeBright = (int)chargeBright;

    // Wake the logger/panel first so fillScreen actually hits the TFT.
    panelSleep(false);
    setBrightness(chargeBright, false);

    tftAbortFrame();
    tftSuppressCanvas(true);
    tft.fillScreen(KVX_DEFAULT_BGCOLOR);

#ifdef HAS_RGB_LED
    {
        // LED on at 20% battery color even if settings ledBright is 0.
        ChargeInfo bootInfo = readChargeInfo();
        chargeLedSync(bootInfo.percent, true, true);
    }
#endif

    unsigned long enteredAt = millis();
    chargeClearInput();
    chargeUserSleep = false;
    isScreenOff = false;
    dimmer = false;
    previousMillis = millis();

    unsigned long lastRefresh = 0;
    bool screenOff = false;
    bool ledOn = true;
    bool firstPaint = true;
    bool uiReady = false;
    bool sleepArmed = false; // true only after first paint + grace + keys released
    bool brightTouched = false; // Down-to-0 sleep only after user moved brightness
    bool fullHold = false;
    bool fullBeeped = false;
    ChargeState prevState = CHARGE_BATTERY;
    bool prevSwitchOff = false;
    bool forceRedraw = true;
    bool showCalendar = true;
    int colorIndex = 0;
    int lastCalDay = -1;
    int lastCalMon = -1;
#ifdef HAS_KEYBOARD
    bool lWasHeld = false;
    bool sWasHeld = false;
    bool cWasHeld = false;
#endif
    // Only honor G0/S/Down blanking after the clock is on and keys are idle.
    const unsigned long INPUT_GRACE_MS = 2500;
    // After wake, ignore Down briefly so a sticky '.' repeat cannot re-blank.
    unsigned long ignoreDownUntil = 0;

    static char lastClock[16];
    static char lastDate[24];
    static char lastHint[40];
    static uint16_t lastHintCol = 0;
    static char lastFoot[40];
    static char lastPct[8];
    static int lastBarPct = -1;
    lastClock[0] = lastDate[0] = lastHint[0] = lastFoot[0] = lastPct[0] = '\0';
    lastBarPct = -1;
    lastHintCol = 0;

    for (;;) {
        previousMillis = millis();
        const bool pastGrace = (millis() - enteredAt) > INPUT_GRACE_MS;
        const bool ignoreDown = millis() < ignoreDownUntil;

        // Until the clock is painted and grace ends, eat leftover menu keys and
        // clear any G0 sleep flag so open never lands on a blank panel.
        if (!sleepArmed) {
            chargeUserSleep = false;
            isScreenOff = false;
            dimmer = false;
            screenOff = false;
            check(EscPress);
            check(DownPress);
            check(NextPress);
            check(UpPress);
            check(PrevPress);
            check(SelPress);
            check(AnyKeyPress);
            if (uiReady && pastGrace) {
                sleepArmed = true;
                chargeClearInput();
            }
        }

#ifdef HAS_KEYBOARD
        const bool lEdge = chargeLetterEdge('l', &lWasHeld, uiReady);
        const bool sEdge = chargeLetterEdge('s', &sWasHeld, sleepArmed);
        const bool cEdge = chargeLetterEdge('c', &cWasHeld, sleepArmed);
        bool ledDirty = false;
        if (lEdge) {
            ledOn = !ledOn;
            ledDirty = true;
            check(AnyKeyPress);
        }
#else
        const bool sEdge = false;
        bool ledDirty = false;
#endif

        // G0 tap (InputHandler) sets chargeUserSleep — join only when armed.
        // Panel off only; LED keeper keeps battery color (L toggles).
        if (!screenOff && sleepArmed && chargeUserSleep) {
            chargeGoIdleOff(&ledOn);
            screenOff = true;
            ignoreDownUntil = millis() + 600;
            ledDirty = true;
        } else if (!sleepArmed && chargeUserSleep) {
            chargeUserSleep = false;
        }

        if (screenOff) {
            bool wake = false;

            if (sEdge) wake = true;
            if (check(EscPress)) wake = true;
            else if (check(SelPress)) wake = true;
            else if (check(UpPress) || check(PrevPress)) {
                check(UpPress);
                check(PrevPress);
                // Up from 0% brightness steps back to 10%; S/G0 sleep restores last level.
                if (chargeBright < 10) chargeBright = 10;
                wake = true;
            } else if (!ignoreDown && check(DownPress)) {
                // Already blank: stay off (LED stays unless L toggled it).
                check(NextPress);
                resetHeldNavKeys();
                ignoreDownUntil = millis() + 400;
            }
            // G0 wake: InputHandler only clears chargeUserSleep.
            if (uiReady && !chargeUserSleep) wake = true;

            ChargeInfo idleInfo = readChargeInfo();
#ifdef HAS_RGB_LED
            chargeLedSync(idleInfo.percent, ledOn, false);
#endif
            if (wake) {
                if (chargeBright < 1) chargeBright = 10;
                chargeModeBright = (int)chargeBright;
                chargeWakeDisplay(chargeBright, &ledOn);
#ifdef HAS_RGB_LED
                chargeLedSync(idleInfo.percent, ledOn, false);
                ledKeepRequest();
#endif
                screenOff = false;
                forceRedraw = true;
                ignoreDownUntil = millis() + 400;
                continue;
            }
            prevState = idleInfo.state;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (sleepArmed && (check(EscPress) || forceHome)) {
            chargeExit();
            return;
        }

#ifdef HAS_KEYBOARD
        if (sEdge) {
            chargeGoIdleOff(&ledOn);
            screenOff = true;
            ignoreDownUntil = millis() + 400;
            continue;
        }
        if (cEdge) {
            showCalendar = !showCalendar;
            forceRedraw = true;
        }
#endif

        int brightDir = 0;
        if (sleepArmed && (check(UpPress) || check(PrevPress))) {
            check(UpPress);
            check(PrevPress);
            brightDir = +1;
        } else if (sleepArmed && !ignoreDown && (check(DownPress) || check(NextPress))) {
            check(DownPress);
            check(NextPress); // '.' also pulses Next on Cardputer
            brightDir = -1;
        }
#ifdef HAS_ENCODER
        int32_t rot = drainRotarySteps();
        if (sleepArmed && rot != 0) brightDir = rot > 0 ? +1 : -1;
#endif
        if (brightDir != 0) {
            brightTouched = true;
            chargeBright = stepBrightness(chargeBright, brightDir);
            chargeModeBright = (int)chargeBright;
            // Down-to-0 blanks only after the user has moved brightness this session.
            if (chargeBright == 0 && brightTouched) {
                chargeGoIdleOff(&ledOn);
                screenOff = true;
                ignoreDownUntil = millis() + 400;
                continue;
            }
            if (chargeBright < 1) chargeBright = 1;
            setBrightness(chargeBright, false);
            if (fullHold) fullHold = false;
        }

        if (sleepArmed && check(SelPress)) {
            colorIndex = (colorIndex + 1) % kClockColorCount;
            forceRedraw = true;
            if (fullHold) fullHold = false;
        } else if (sleepArmed) {
            check(AnyKeyPress);
        }

        ChargeInfo info = readChargeInfo();
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
        const bool stateChanged = (info.state != prevState) || (info.chargeSwitchOff != prevSwitchOff);
        prevState = info.state;
        prevSwitchOff = info.chargeSwitchOff;
        if (stateChanged) {
            lastHint[0] = '\0';
            lastRefresh = 0;
        }

        const bool panelWrote = firstPaint;
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

            // Dense HUD under FP=2: body FP glyphs are 16px tall and clip in the
            // bottom strip — use uiDenseFont so "Battery x.xxV" and shortcuts fit.
            const int dense = uiDenseFont();
            const int kFootH = uiLineH(dense) + 2;
            const int kHintH = uiLineH(dense) + 2;
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
            if (info.chargeSwitchOff) {
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
                // Plugged in: "Charging x.xxV" is always yellow.
                if (info.state == CHARGE_CHARGING || info.state == CHARGE_USB) hintCol = TFT_YELLOW;
            }
            if (hintCol != lastHintCol) lastHint[0] = '\0';
            lastHintCol = hintCol;
            drawField(
                4, hintY, tftWidth - 8, kHintH, hintBuf, lastHint, sizeof(lastHint), dense, hintCol, bg, true
            );

            drawField(
                4,
                tftHeight - kFootH,
                tftWidth - 8,
                kFootH,
                ";/. bright  S sleep  L  C",
                lastFoot,
                sizeof(lastFoot),
                dense,
                TFT_DARKGREY,
                bg,
                true
            );

            forceRedraw = false;
            firstPaint = false;
            uiReady = true;
        }

#ifdef HAS_RGB_LED
        chargeLedSync(info.percent, ledOn, ledDirty);
        if (panelWrote) ledKeepRequest();
#endif

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

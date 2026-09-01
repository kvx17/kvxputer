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

static uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static uint16_t chargeLevelColor(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    static const uint16_t stops[] = {0xF800, 0xFD20, 0xFFE0, 0x07E0};
    if (percent >= 100) return stops[3];
    int band = percent * 3 / 100;
    if (band > 2) band = 2;
    int bandStart = band * 100 / 3;
    int bandEnd = (band + 1) * 100 / 3;
    int span = bandEnd - bandStart;
    uint8_t t = span > 0 ? (uint8_t)((percent - bandStart) * 255 / span) : 0;
    return lerp565(stops[band], stops[band + 1], t);
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
    int cellW = w / 7;
    int headerH = 10;
    int rows = h >= 54 ? 6 : 5;
    int cellH = (h - headerH) / rows;
    if (cellW < 8 || cellH < 6) return;

    tft.setTextSize(FP);
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
            int ty = cy + (cellH > 8 ? (cellH - 8) / 2 : 0);
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
static void chargeLedTick(const ChargeInfo &info, bool screenOff) {
    if (!screenOff) return;
    float phase = (sinf(millis() / 1400.0f * PI) + 1.0f) * 0.5f;
    uint8_t v = (uint8_t)(12 + phase * 36);
    if (info.state == CHARGE_FULL) setLedColor(CRGB(0, v, 0));
    else if (info.state == CHARGE_CHARGING) setLedColor(CRGB(v, v / 3, 0));
    else setLedColor(CRGB::Black);
}
#endif

static void chargeGoIdleOff() {
#ifdef HAS_RGB_LED
    ledPauseEffects(true);
#endif
    chargeUserSleep = true;
    setBrightness(0, false);
    dimmer = false;
}

static void chargeWakeDisplay(uint8_t bright) {
    chargeUserSleep = false;
    dimmer = false;
    setBrightness(bright, false);
#ifdef HAS_RGB_LED
    ledPauseEffects(false);
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
    setBrightness(kvxConfig.bright, false);
#ifdef HAS_RGB_LED
    ledSetup();
#endif
    tft.fillScreen(kvxConfig.bgColor);
    returnToMenu = true;
}

void runChargeLoop() {
    chargeModeActive = true;
    chargeUserSleep = false;
    previousMillis = millis();
    isScreenOff = false;
    dimmer = false;

    uint8_t chargeBright = 10;
    chargeModeBright = chargeBright;
    setBrightness(chargeBright, false);

    unsigned long enteredAt = millis();

    // Swallow the OK that opened this app (same idea as Clock).
    check(SelPress);
    check(AnyKeyPress);
    previousMillis = millis();

    unsigned long lastRefresh = 0;
    bool screenOff = false;
    bool firstPaint = true;
    bool fullHold = false;
    bool fullBeeped = false;
    ChargeState prevState = CHARGE_BATTERY;
    bool forceRedraw = true;
    bool showCalendar = true;
    int colorIndex = 0;
    int lastCalDay = -1;
    int lastCalMon = -1;

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

        if (screenOff) {
            bool wake = false;
            if (check(EscPress)) wake = true;
            else if (check(SelPress)) wake = true;
#ifdef HAS_KEYBOARD
            else if (AnyKeyPress) {
                char letter = checkLetterShortcutPress();
                if (letter == 's' || letter == 'S') {
                    check(AnyKeyPress);
                    wake = true;
                }
            }
#endif
            if (wake) {
                chargeWakeDisplay(chargeBright);
                screenOff = false;
                forceRedraw = true;
            } else {
                ChargeInfo idleInfo = readChargeInfo();
#ifdef HAS_RGB_LED
                chargeLedTick(idleInfo, true);
#endif
                prevState = idleInfo.state;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }

        if (check(EscPress)) {
            chargeExit();
            return;
        }

#ifdef HAS_KEYBOARD
        if (AnyKeyPress) {
            char letter = checkLetterShortcutPress();
            if (letter == 's' || letter == 'S') {
                check(AnyKeyPress);
                if (millis() - enteredAt > 600) {
                    chargeGoIdleOff();
                    screenOff = true;
                    continue;
                }
            } else if (letter == 'c' || letter == 'C') {
                check(AnyKeyPress);
                showCalendar = !showCalendar;
                forceRedraw = true;
            }
        }
#endif

        int brightDir = 0;
        if (check(UpPress) || check(PrevPress)) brightDir = +1;
        else if (check(DownPress) || check(NextPress)) brightDir = -1;
#ifdef HAS_ENCODER
        int32_t rot = drainRotarySteps();
        if (rot != 0) brightDir = rot > 0 ? +1 : -1;
#endif
        if (brightDir != 0) {
            chargeBright = stepBrightness(chargeBright, brightDir);
            chargeModeBright = chargeBright;
            setBrightness(chargeBright, false);
            if (fullHold) fullHold = false;
        }

        if (check(SelPress)) {
            colorIndex = (colorIndex + 1) % kClockColorCount;
            forceRedraw = true;
            if (fullHold) fullHold = false;
        } else {
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
                "S sleep  C cal  OK color  ESC",
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

#include "clock_faces.h"
#include "root/ui/display.h"
#include <globals.h>
#include <string.h>

static char s_digitalLastTime[16] = "";
static int s_digitalLastTop = -1;
static uint16_t s_digitalLastColor = 0;

static char s_chargeLastTime[16] = "";
static char s_chargeLastDate[32] = "";
static int s_chargeLastPct = -1;
static int s_chargeLastTop = -1;
static bool s_chargeLastCal = false;
static uint16_t s_chargeLastColor = 0;

void clockFaceInvalidate() {
    s_digitalLastTime[0] = '\0';
    s_digitalLastTop = -1;
    s_chargeLastTime[0] = '\0';
    s_chargeLastDate[0] = '\0';
    s_chargeLastPct = -1;
    s_chargeLastTop = -1;
}

void clockFaceFormatDate(const struct tm &t, char *out, size_t outLen) {
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

void clockFaceDrawMonthCalendar(
    int x, int y, int w, int h, const struct tm &t, uint16_t color, uint16_t bg
) {
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

void clockFaceDrawChargeBar(int x, int y, int w, int h, int percent, uint16_t col, uint16_t bg) {
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

void clockFaceDrawChargeStyle(
    int topY, const struct tm &t, const char *timeStr, int batteryPct, uint16_t color, bool showCalendar
) {
    const uint16_t bg = kvxConfig.bgColor;
    char dateBuf[24];
    clockFaceFormatDate(t, dateBuf, sizeof(dateBuf));

    int pct = batteryPct;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);

    const char *s = timeStr ? timeStr : "--:--";
    const bool layoutChange =
        (s_chargeLastTop != topY || s_chargeLastCal != showCalendar || s_chargeLastColor != color || s_chargeLastPct < 0);
    const bool timeChanged = (strncmp(s, s_chargeLastTime, sizeof(s_chargeLastTime)) != 0);
    const bool dateChanged = (strncmp(dateBuf, s_chargeLastDate, sizeof(s_chargeLastDate)) != 0);
    const bool pctChanged = (pct != s_chargeLastPct);

    if (layoutChange) {
        tft.fillRect(0, topY, tftWidth, tftHeight - topY, bg);
        s_chargeLastTime[0] = '\0';
        s_chargeLastDate[0] = '\0';
        s_chargeLastPct = -1;
    }

    const int kBarH = 10;
    int barY = tftHeight - kBarH - 4;

    if (showCalendar) {
        if (layoutChange || dateChanged) {
            tft.fillRect(0, topY, tftWidth, FM * LH + 4, bg);
            tft.setTextSize(FM);
            tft.setTextColor(color, bg);
            tft.drawCentreString(dateBuf, tftWidth / 2, topY + 2, 1);
            strncpy(s_chargeLastDate, dateBuf, sizeof(s_chargeLastDate) - 1);
            s_chargeLastDate[sizeof(s_chargeLastDate) - 1] = '\0';
        }

        uint8_t clockSize = 3;
        int clockLen = (int)strlen(s);
        if (clockLen < 1) clockLen = 8;
        while (clockSize > 1 && clockSize * 6 * clockLen > tftWidth - 56) clockSize--;
        int clockY = topY + FM * LH + 4;
        if (layoutChange || timeChanged) {
            tft.fillRect(0, clockY, tftWidth - 54, clockSize * 8 + 2, bg);
            tft.setTextSize(clockSize);
            tft.setTextColor(color, bg);
            tft.drawCentreString(s, (tftWidth - 50) / 2, clockY, 1);
            strncpy(s_chargeLastTime, s, sizeof(s_chargeLastTime) - 1);
            s_chargeLastTime[sizeof(s_chargeLastTime) - 1] = '\0';
        }
        if (layoutChange || pctChanged) {
            tft.fillRect(tftWidth - 54, clockY, 54, FM * LH + 2, bg);
            tft.setTextSize(FM);
            tft.setTextColor(color, bg);
            tft.drawRightString(pctBuf, tftWidth - 4, clockY + 2, 1);
            s_chargeLastPct = pct;
        }

        int calY = clockY + clockSize * 8 + 6;
        int calH = barY - calY - 3;
        if (layoutChange || dateChanged) {
            if (calH >= 36) {
                clockFaceDrawMonthCalendar(4, calY, tftWidth - 8, calH, t, color, bg);
            }
        }
    } else {
        if (layoutChange || dateChanged) {
            tft.fillRect(0, topY, tftWidth, FM * LH + 4, bg);
            tft.setTextSize(FM);
            tft.setTextColor(color, bg);
            tft.drawCentreString(dateBuf, tftWidth / 2, topY + 2, 1);
            strncpy(s_chargeLastDate, dateBuf, sizeof(s_chargeLastDate) - 1);
            s_chargeLastDate[sizeof(s_chargeLastDate) - 1] = '\0';
        }

        uint8_t clockSize = 4;
        int clockLen = (int)strlen(s);
        if (clockLen < 1) clockLen = 8;
        while (clockSize > 1 && clockSize * 6 * clockLen > tftWidth - 8) clockSize--;
        int clockY = topY + FM * LH + 6;
        if (layoutChange || timeChanged) {
            tft.fillRect(0, clockY, tftWidth, clockSize * 8 + 2, bg);
            tft.setTextSize(clockSize);
            tft.setTextColor(color, bg);
            tft.drawCentreString(s, tftWidth / 2, clockY, 1);
            strncpy(s_chargeLastTime, s, sizeof(s_chargeLastTime) - 1);
            s_chargeLastTime[sizeof(s_chargeLastTime) - 1] = '\0';
        }
        if (layoutChange || pctChanged) {
            int pctY = clockY + clockSize * 8 + 4;
            tft.fillRect(0, pctY, tftWidth, FM * LH + 2, bg);
            tft.setTextSize(FM);
            tft.setTextColor(color, bg);
            tft.drawCentreString(pctBuf, tftWidth / 2, pctY, 1);
            s_chargeLastPct = pct;
        }
    }

    if (layoutChange || pctChanged) {
        clockFaceDrawChargeBar(8, barY, tftWidth - 16, kBarH, pct, color, bg);
    }

    s_chargeLastTop = topY;
    s_chargeLastCal = showCalendar;
    s_chargeLastColor = color;
}

void clockFaceDrawDigital(int topY, const char *timeStr, uint16_t color) {
    const uint16_t bg = kvxConfig.bgColor;
    const char *s = timeStr ? timeStr : "--:--";
    const bool layoutChange = (s_digitalLastTop != topY || s_digitalLastColor != color || s_digitalLastTime[0] == '\0');
    const bool timeChanged = (strncmp(s, s_digitalLastTime, sizeof(s_digitalLastTime)) != 0);

    if (layoutChange) {
        tft.fillRect(0, topY, tftWidth, tftHeight - topY, bg);
        tft.drawRect(
            BORDER_PAD_X,
            topY + 4,
            tftWidth - 2 * BORDER_PAD_X,
            tftHeight - topY - 8,
            color
        );
    }

    if (layoutChange || timeChanged) {
        uint8_t f_size = 4;
        for (uint8_t i = 4; i > 0; i--) {
            if (i * LW * (int)strlen(s) < (tftWidth - BORDER_PAD_X * 2)) {
                f_size = i;
                break;
            }
        }
        int midY = topY + (tftHeight - topY) / 2 - f_size * LH / 2;
        tft.fillRect(BORDER_PAD_X + 2, midY - 2, tftWidth - 2 * BORDER_PAD_X - 4, f_size * LH + 4, bg);
        tft.setTextSize(f_size);
        tft.setTextColor(color, bg);
        tft.drawCentreString(s, tftWidth / 2, midY, 1);
        strncpy(s_digitalLastTime, s, sizeof(s_digitalLastTime) - 1);
        s_digitalLastTime[sizeof(s_digitalLastTime) - 1] = '\0';
    }

    s_digitalLastTop = topY;
    s_digitalLastColor = color;
}

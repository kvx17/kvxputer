#include "pda_worldclock.h"
#include "pda_alarms.h"
#include "pda_time.h"

#include "menu/clock/clock_faces.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <globals.h>
#include <interface.h>
#include <string.h>
#include <time.h>

namespace {

struct City {
    const char *name;
    float utcOffset; // hours from UTC, no per-city DST
};

const City kCities[] = {
    {"Honolulu",  -10.0f},
    {"LA",        -8.0f },
    {"Chicago",   -6.0f },
    {"New York",  -5.0f },
    {"Sao Paulo", -3.0f },
    {"London",    0.0f  },
    {"Paris",     1.0f  },
    {"Cairo",     2.0f  },
    {"Moscow",    3.0f  },
    {"Dubai",     4.0f  },
    {"Delhi",     5.5f  },
    {"Bangkok",   7.0f  },
    {"Beijing",   8.0f  },
    {"Tokyo",     9.0f  },
    {"Sydney",    10.0f },
    {"Auckland",  12.0f },
};
constexpr int kCityCount = sizeof(kCities) / sizeof(kCities[0]);

const char *kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

enum { FACE_CITIES = 0, FACE_CHARGE = 1, FACE_DIGITAL = 2, FACE_COUNT = 3 };

int32_t localUtcOffsetSec() {
    return (int32_t)(kvxConfig.tmz * 3600.0f) + (kvxConfig.dst ? 3600 : 0);
}

time_t localEpoch() {
    struct tm t = pdaNow();
    return mktime(&t);
}

String formatHm(const struct tm &t) {
    if (kvxConfig.clock24hr) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        return String(buf);
    }
    int hour12 = (t.tm_hour == 0) ? 12 : (t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour);
    const char *ampm = (t.tm_hour < 12) ? "AM" : "PM";
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d%s", hour12, t.tm_min, ampm);
    return String(buf);
}

String formatOffset(float o) {
    char buf[10];
    if (o == (float)(int)o) snprintf(buf, sizeof(buf), "%+d", (int)o);
    else snprintf(buf, sizeof(buf), "%+.1f", o);
    return String(buf);
}

struct tm cityTm(float utcOffset) {
    time_t utc = localEpoch() - localUtcOffsetSec();
    time_t city = utc + (time_t)(utcOffset * 3600.0f);
    struct tm t;
    gmtime_r(&city, &t);
    return t;
}

String cityLine(const City &c) {
    struct tm t = cityTm(c.utcOffset);
    char dateBuf[12];
    int mon = t.tm_mon;
    if (mon < 0 || mon > 11) mon = 0;
    snprintf(dateBuf, sizeof(dateBuf), "%02d%s", t.tm_mday, kMon[mon]);

    String name = String(c.name);
    while (name.length() < 9) name += ' ';
    if (name.length() > 9) name = name.substring(0, 9);
    return name + " " + formatHm(t) + " " + formatOffset(c.utcOffset) + " " + dateBuf;
}

void drawChromeOnce() {
    // Cities/charge faces redraw chrome inside their own frame.
}

void drawFooter() {
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.fillRect(0, tftHeight - FP * LH - 2, tftWidth, FP * LH + 2, kvxConfig.bgColor);
    tft.drawCentreString("OK set TZ  [] face  ESC", tftWidth / 2, tftHeight - FP * LH - 2, 1);
}

void drawCitiesFace(int sel, bool full) {
    TftFrame frame;
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;

    const City &selected = kCities[sel];
    String status = String(selected.name) + " " + formatHm(cityTm(selected.utcOffset)) + " " +
                    formatOffset(selected.utcOffset);
    drawKvxTopBar("World Clock", status.c_str());

    const int bodyY = KVX_TOPBAR_H + 2;
    if (full) tft.fillRect(0, bodyY, tftWidth, tftHeight - bodyY - FP * LH - 2, bg);

    struct tm now = pdaNow();
    String localLine = "Local  " + formatHm(now);
    if (!pdaClockIsSet()) localLine += "  (unset)";

    tft.setTextSize(FP);
    tft.setTextColor(pri, bg);
    tft.fillRect(BORDER_PAD_X, bodyY, tftWidth - 2 * BORDER_PAD_X, FP * LH, bg);
    tft.setCursor(BORDER_PAD_X, bodyY);
    tft.print(localLine);

    char tzBuf[24];
    snprintf(tzBuf, sizeof(tzBuf), "TZ UTC%+.1f%s", kvxConfig.tmz, kvxConfig.dst ? " DST" : "");
    tft.setTextColor(sec, bg);
    tft.fillRect(BORDER_PAD_X, bodyY + FP * LH, tftWidth - 2 * BORDER_PAD_X, FP * LH, bg);
    tft.setCursor(BORDER_PAD_X, bodyY + FP * LH);
    tft.print(tzBuf);

    const int rowH = FP * LH + 2;
    const int listY = bodyY + FP * LH * 2 + 4;
    const int maxRows = max(1, (tftHeight - listY - FP * LH - 4) / rowH);

    int start = sel;
    if (start > kCityCount - maxRows) start = max(0, kCityCount - maxRows);
    if (start < 0) start = 0;
    if (sel < start) start = sel;
    if (sel >= start + maxRows) start = sel - maxRows + 1;

    tft.fillRect(0, listY - 1, tftWidth, maxRows * rowH + 2, bg);
    const int maxChars = max(1, (tftWidth - 2 * BORDER_PAD_X) / (FP * LW));
    for (int i = 0; i < maxRows && start + i < kCityCount; i++) {
        int idx = start + i;
        const City &c = kCities[idx];
        String line = cityLine(c);
        if ((int)line.length() > maxChars) line = line.substring(0, maxChars);
        int y = listY + i * rowH;
        bool hi = (idx == sel);
        if (hi) {
            tft.fillRect(BORDER_PAD_X - 2, y - 1, tftWidth - 2 * BORDER_PAD_X + 4, rowH, sec);
            tft.setTextColor(bg, sec);
        } else {
            tft.setTextColor(pri, bg);
        }
        tft.setTextSize(FP);
        tft.setCursor(BORDER_PAD_X, y);
        tft.print(line);
    }
    drawFooter();
}

void drawChargeFace(int sel) {
    const City &c = kCities[sel];
    struct tm t = cityTm(c.utcOffset);
    String hm = formatHm(t);
    // Prefer HH:MM:SS-style for digital; formatHm is HH:MM — extend with seconds.
    char timeBuf[16];
    if (kvxConfig.clock24hr) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int hour12 = (t.tm_hour == 0) ? 12 : (t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour);
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour12, t.tm_min);
    }
    (void)hm;
    int bat = (int)getBattery();
    if (bat <= 0) bat = 50;
    clockFaceDrawChargeStyle(KVX_TOPBAR_H + 1, t, timeBuf, bat, kvxConfig.priColor, true);
    // City caption
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawString(c.name, 6, KVX_TOPBAR_H + 2, 1);
    drawFooter();
}

void drawDigitalFace(int sel) {
    const City &c = kCities[sel];
    struct tm t = cityTm(c.utcOffset);
    char timeBuf[16];
    if (kvxConfig.clock24hr) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int hour12 = (t.tm_hour == 0) ? 12 : (t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour);
        const char *ampm = (t.tm_hour < 12) ? "AM" : "PM";
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d%s", hour12, t.tm_min, ampm);
    }
    clockFaceDrawDigital(KVX_TOPBAR_H + 1, timeBuf, kvxConfig.priColor);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString(c.name, tftWidth / 2, KVX_TOPBAR_H + 6, 1);
    drawFooter();
}

void drawFace(int face, int sel, bool full) {
    // Cities face owns its TftFrame. Other faces still need one present pass.
    if (face == FACE_CITIES) {
        drawCitiesFace(sel, full);
        return;
    }
    TftFrame frame;
    if (full) {
        tft.fillRect(0, KVX_TOPBAR_H + 1, tftWidth, tftHeight - KVX_TOPBAR_H - 1, kvxConfig.bgColor);
    }
    drawKvxTopBar("World Clock");
    if (face == FACE_CHARGE) drawChargeFace(sel);
    else drawDigitalFace(sel);
}

} // namespace

void pdaWorldClock() {
    int sel = 0;
    int face = kvxConfig.pdaWcFace;
    if (face < 0 || face >= FACE_COUNT) face = FACE_CITIES;
    bool chromeDirty = true;
    bool redraw = true;
    unsigned long lastTick = 0;
    char lastTimeKey[48] = "";

    for (;;) {
        if (forceHome) break;
        if (pdaAlarmsPoll()) {
            chromeDirty = true;
            redraw = true;
        }

        if (chromeDirty) {
            drawChromeOnce();
            chromeDirty = false;
            redraw = true;
            lastTimeKey[0] = '\0';
        }

        // Dirty-update times once per second (or on nav) without clearing the top bar.
        bool tick = (millis() - lastTick > 1000);
        if (redraw || tick) {
            lastTick = millis();
            // Skip redraw if nothing visual changed on city list (minute boundary).
            struct tm now = pdaNow();
            char key[48];
            snprintf(
                key,
                sizeof(key),
                "%d-%d-%02d%02d%02d-%d",
                face,
                sel,
                now.tm_hour,
                now.tm_min,
                (face == FACE_CITIES) ? 0 : now.tm_sec,
                redraw ? 1 : 0
            );
            if (redraw || strcmp(key, lastTimeKey) != 0) {
                drawFace(face, sel, redraw);
                strncpy(lastTimeKey, key, sizeof(lastTimeKey) - 1);
                lastTimeKey[sizeof(lastTimeKey) - 1] = '\0';
            }
            redraw = false;
        }

        if (check(EscPress)) break;

#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            for (char raw : key.word) {
                if (raw == '[') {
                    face = (face + FACE_COUNT - 1) % FACE_COUNT;
                    redraw = true;
                    lastTimeKey[0] = '\0';
                } else if (raw == ']') {
                    face = (face + 1) % FACE_COUNT;
                    redraw = true;
                    lastTimeKey[0] = '\0';
                }
            }
        }
#endif

        if (check(UpPress) || check(PrevPress)) {
            if (sel > 0) sel--;
            redraw = true;
        } else if (check(DownPress) || check(NextPress)) {
            if (sel < kCityCount - 1) sel++;
            redraw = true;
        } else if (check(SelPress)) {
            const City &c = kCities[sel];
            String msg = String("Set TZ from ") + c.name + "?";
            int8_t choice = displayMessage(msg.c_str(), "No", nullptr, "Yes", kvxConfig.priColor);
            if (choice == 1) {
                kvxConfig.setTmz(c.utcOffset);
                // setTmz() already saveFile()'s to LittleFS + SD userSettings.json
                displaySuccess("TZ set", true);
            }
            chromeDirty = true;
            redraw = true;
        }

        delay(20);
    }

    if (face != kvxConfig.pdaWcFace) kvxConfig.setPdaWcFace(face);
    tft.fillScreen(kvxConfig.bgColor);
}

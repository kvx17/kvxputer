#include "kremote_ui.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include <algorithm>
#include <globals.h>

static const uint16_t KR_PURPLE = DEFAULT_PRICOLOR;
static const uint16_t KR_GREEN = DEFAULT_SECCOLOR;
static const uint16_t KR_ORANGE = 0xFD20;
static const uint16_t KR_BG = KVX_DEFAULT_BGCOLOR;
static const int KR_HEADER_H = 26;
static const int KR_FOOTER_H = 18;

KremoteVertDisplayScope::KremoteVertDisplayScope() {
#if defined(HAS_SCREEN)
    _savedRot = kvxConfigPins.rotation;
#ifndef ROTATION
#define ROTATION 1
#endif
    const int vertRot = (_savedRot + 1) % 4;
    tft.setRotation(vertRot);
    tft.setRotation(vertRot);
    tftWidth = tft.width();
    tftHeight = tft.height();
    tft.fillScreen(KR_BG);
    _active = true;
#endif
}

KremoteVertDisplayScope::~KremoteVertDisplayScope() {
#if defined(HAS_SCREEN)
    if (!_active) return;
    tft.setRotation(_savedRot);
    tft.setRotation(_savedRot);
    tftWidth = tft.width();
    tftHeight = tft.height();
#endif
}

void kremoteDrawHeader(const char *title) {
    tft.fillRect(0, 0, tftWidth, KR_HEADER_H, KR_BG);
    tft.setTextSize(FP);
    tft.setTextColor(KR_PURPLE, KR_BG);
    tft.drawString("kvxputer universal remote", 6, 4);
    tft.setTextColor(KR_GREEN, KR_BG);
    if (title != nullptr && title[0] != '\0') tft.drawString(title, 6, 14);
    tft.drawFastHLine(0, KR_HEADER_H, tftWidth, KR_PURPLE);
}

void kremoteDrawFooter(const char *hints) {
    const int y = tftHeight - KR_FOOTER_H;
    tft.fillRect(0, y, tftWidth, KR_FOOTER_H, KR_BG);
    tft.drawFastHLine(0, y, tftWidth, KR_PURPLE);
    tft.setTextSize(1);
    tft.setTextColor(KR_GREEN, KR_BG);
    if (hints == nullptr) hints = "hold Back=exit  x2=Power";
    tft.drawCentreString(hints, tftWidth / 2, y + 4, 1);
}

void kremoteClearContent() {
    tft.fillRect(0, KR_HEADER_H + 1, tftWidth, tftHeight - KR_HEADER_H - KR_FOOTER_H - 2, KR_BG);
}

static void drawSectorHighlight(int cx, int cy, int rOuter, int rInner, int quad, uint16_t color) {
    // quad: 0=up, 1=down, 2=left, 3=right
    int startDeg = 0, endDeg = 0;
    switch (quad) {
        case 0:
            startDeg = 225;
            endDeg = 315;
            break;
        case 1:
            startDeg = 45;
            endDeg = 135;
            break;
        case 2:
            startDeg = 135;
            endDeg = 225;
            break;
        case 3:
            startDeg = 315;
            endDeg = 45;
            break;
        default: return;
    }
    tft.drawArc(cx, cy, rOuter, rInner, startDeg, endDeg, color, KR_BG);
}

void kremoteDrawPad(bool portrait, bool swapped, int flashId) {
    kremoteClearContent();
    const int top = KR_HEADER_H + 2;
    const int bottom = tftHeight - KR_FOOTER_H - 2;
    const int cx = tftWidth / 2;
    const int cy = (top + bottom) / 2;
    int maxR = std::min((int)tftWidth, bottom - top) / 2 - 4;
    if (maxR < 28) maxR = 28;

    const int rOuter = maxR;
    const int rMid = (maxR * 62) / 100;
    const int rInner = (maxR * 28) / 100;

    tft.drawCircle(cx, cy, rOuter, KR_PURPLE);
    tft.drawCircle(cx, cy, rMid, KR_PURPLE);
    tft.drawCircle(cx, cy, rInner, KR_PURPLE);
    tft.drawFastVLine(cx, cy - rMid, rMid * 2, KR_PURPLE);
    tft.drawFastHLine(cx - rMid, cy, rMid * 2, KR_PURPLE);
    tft.fillCircle(cx, cy, rInner - 2, KR_BG);
    tft.drawCircle(cx, cy, rInner, KR_PURPLE);

    const char *upLab = swapped ? "V+" : "UP";
    const char *dnLab = swapped ? "V-" : "DN";
    const char *leLab = swapped ? "C-" : "LT";
    const char *riLab = swapped ? "C+" : "RT";
    const char *okLab = swapped ? "HM" : "OK";
    const char *ouLab = swapped ? "UP" : "V+";
    const char *odLab = swapped ? "DN" : "V-";
    const char *olLab = swapped ? "LT" : "C-";
    const char *orLab = swapped ? "RT" : "C+";

    tft.setTextSize(1);
    tft.setTextColor(KR_GREEN, KR_BG);
    tft.drawCentreString(upLab, cx, cy - (rMid + rInner) / 2 - 4, 1);
    tft.drawCentreString(dnLab, cx, cy + (rMid + rInner) / 2 - 4, 1);
    tft.drawString(leLab, cx - (rMid + rInner) / 2 - 8, cy - 4);
    tft.drawString(riLab, cx + (rMid + rInner) / 2 - 4, cy - 4);
    tft.setTextColor(KR_ORANGE, KR_BG);
    tft.drawCentreString(ouLab, cx, cy - rOuter + 4, 1);
    tft.drawCentreString(odLab, cx, cy + rOuter - 12, 1);
    tft.drawString(olLab, cx - rOuter + 2, cy - 4);
    tft.drawString(orLab, cx + rOuter - 14, cy - 4);
    tft.setTextColor(KR_GREEN, KR_BG);
    tft.drawCentreString(okLab, cx, cy - 4, 1);

    const uint16_t flashColor = KR_GREEN;
    if (flashId == KREMOTE_FLASH_UP) drawSectorHighlight(cx, cy, rMid, rInner, 0, flashColor);
    if (flashId == KREMOTE_FLASH_DOWN) drawSectorHighlight(cx, cy, rMid, rInner, 1, flashColor);
    if (flashId == KREMOTE_FLASH_LEFT) drawSectorHighlight(cx, cy, rMid, rInner, 2, flashColor);
    if (flashId == KREMOTE_FLASH_RIGHT) drawSectorHighlight(cx, cy, rMid, rInner, 3, flashColor);
    if (flashId == KREMOTE_FLASH_VOL_UP) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 0, flashColor);
    if (flashId == KREMOTE_FLASH_VOL_DOWN) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 1, flashColor);
    if (flashId == KREMOTE_FLASH_CH_DOWN) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 2, flashColor);
    if (flashId == KREMOTE_FLASH_CH_UP) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 3, flashColor);
    if (flashId == KREMOTE_FLASH_OK || flashId == KREMOTE_FLASH_HOME) {
        tft.fillCircle(cx, cy, rInner - 3, flashColor);
        tft.setTextColor(KR_BG, flashColor);
        tft.drawCentreString(flashId == KREMOTE_FLASH_HOME ? "HM" : "OK", cx, cy - 4, 1);
    }
    if (flashId == KREMOTE_FLASH_POWER) {
        tft.setTextColor(KR_ORANGE, KR_BG);
        tft.drawCentreString("POWER", cx, top + 2, 1);
    }
    if (flashId == KREMOTE_FLASH_BACK) {
        tft.setTextColor(KR_ORANGE, KR_BG);
        tft.drawCentreString("BACK", cx, top + 2, 1);
    }

    (void)portrait;
}

void kremoteShowButtonMap() {
    drawMainBorderWithTitle("Button Map");
    tft.setTextSize(FP);
    tft.setCursor(10, 32);
    const bool swapped = kvxConfig.kremoteButtonsSwapped;
    padprintln(swapped ? "Mode: Swapped" : "Mode: Normal");
    padprintln("");
    if (!swapped) {
        padprintln("Up short=UP  hold=Vol+");
        padprintln("Dn short=DN  hold=Vol-");
        padprintln("Lt short=LT  hold=Ch-");
        padprintln("Rt short=RT  hold=Ch+");
        padprintln("OK short=OK  hold=Home");
    } else {
        padprintln("Up short=Vol+ hold=UP");
        padprintln("Dn short=Vol- hold=DN");
        padprintln("Lt short=Ch-  hold=LT");
        padprintln("Rt short=Ch+  hold=RT");
        padprintln("OK short=Home hold=OK");
    }
    padprintln("Back short=Back hold=Exit");
    padprintln("Back x2 = Power");
    padprintln("");
    padprintln("Press any key...");
    while (!check(AnyKeyPress) && !check(EscPress) && !check(SelPress) && !forceHome) delay(20);
    delay(150);
    while (!forceHome && (check(AnyKeyPress) || check(EscPress) || check(SelPress))) delay(10);
}

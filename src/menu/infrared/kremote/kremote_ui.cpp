#include "kremote_ui.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/theme.h"
#include <algorithm>
#include <cstdio>
#include <globals.h>

static const uint16_t KR_PURPLE = DEFAULT_PRICOLOR;
static const uint16_t KR_GREEN = DEFAULT_SECCOLOR;
static const uint16_t KR_ORANGE = 0xFD20;
static const uint16_t KR_DIM = 0x4208;
static const uint16_t KR_BG = KVX_DEFAULT_BGCOLOR;
static const int KR_FOOTER_H = 18;
static const int KR_BIND_STRIP_H = 42;

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

void kremoteDrawFooter(const char *hints) {
    const int y = tftHeight - KR_FOOTER_H;
    tft.fillRect(0, y, tftWidth, KR_FOOTER_H, KR_BG);
    tft.drawFastHLine(0, y, tftWidth, KR_PURPLE);
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(KR_GREEN, KR_BG);
    if (hints == nullptr) hints = "Del=exit";
    tft.drawCentreString(hints, tftWidth / 2, y + 4, 1);
}

static void drawSectorHighlight(int cx, int cy, int rOuter, int rInner, int quad, uint16_t color) {
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

static uint16_t bindColor(bool present, bool flash) {
    if (flash) return KR_ORANGE;
    return present ? KR_GREEN : KR_DIM;
}

static void drawBindLabel(const char *text, int x, int y, bool present, bool flash) {
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(bindColor(present, flash), KR_BG);
    tft.drawString(text, x, y);
}

void kremoteDrawVirtualRemote(const char *remoteName, const bool present[KREMOTE_ACT_COUNT], int flashId,
                              int flashDigit) {
#if !defined(HAS_SCREEN)
    (void)remoteName;
    (void)present;
    (void)flashId;
    (void)flashDigit;
    return;
#else
    tft.fillScreen(KR_BG);
    drawKvxTopBar(remoteName && remoteName[0] ? remoteName : "Remote");

    const int top = KVX_TOPBAR_H + 2;
    const int bottom = tftHeight - KR_FOOTER_H - KR_BIND_STRIP_H - 2;
    const int cx = tftWidth / 2;
    const int cy = (top + bottom) / 2;
    int maxR = std::min((int)tftWidth, bottom - top) / 2 - 4;
    if (maxR < 24) maxR = 24;

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

    tft.setTextSize(uiDenseFont());
    tft.setTextColor(bindColor(present[KREMOTE_ACT_UP], flashId == KREMOTE_FLASH_UP), KR_BG);
    tft.drawCentreString("UP", cx, cy - (rMid + rInner) / 2 - 4, 1);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_DOWN], flashId == KREMOTE_FLASH_DOWN), KR_BG);
    tft.drawCentreString("DN", cx, cy + (rMid + rInner) / 2 - 4, 1);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_LEFT], flashId == KREMOTE_FLASH_LEFT), KR_BG);
    tft.drawString("LT", cx - (rMid + rInner) / 2 - 8, cy - 4);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_RIGHT], flashId == KREMOTE_FLASH_RIGHT), KR_BG);
    tft.drawString("RT", cx + (rMid + rInner) / 2 - 4, cy - 4);

    tft.setTextColor(bindColor(present[KREMOTE_ACT_VOL_UP], flashId == KREMOTE_FLASH_VOL_UP), KR_BG);
    tft.drawCentreString("V+", cx, cy - rOuter + 4, 1);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_VOL_DOWN], flashId == KREMOTE_FLASH_VOL_DOWN), KR_BG);
    tft.drawCentreString("V-", cx, cy + rOuter - 12, 1);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_CH_DOWN], flashId == KREMOTE_FLASH_CH_DOWN), KR_BG);
    tft.drawString("C-", cx - rOuter + 2, cy - 4);
    tft.setTextColor(bindColor(present[KREMOTE_ACT_CH_UP], flashId == KREMOTE_FLASH_CH_UP), KR_BG);
    tft.drawString("C+", cx + rOuter - 14, cy - 4);

    if (flashId == KREMOTE_FLASH_OK) {
        tft.fillCircle(cx, cy, rInner - 3, KR_GREEN);
        tft.setTextColor(KR_BG, KR_GREEN);
        tft.drawCentreString("OK", cx, cy - 4, 1);
    } else {
        tft.setTextColor(bindColor(present[KREMOTE_ACT_OK], false), KR_BG);
        tft.drawCentreString("OK", cx, cy - 4, 1);
    }

    if (flashId == KREMOTE_FLASH_UP) drawSectorHighlight(cx, cy, rMid, rInner, 0, KR_GREEN);
    if (flashId == KREMOTE_FLASH_DOWN) drawSectorHighlight(cx, cy, rMid, rInner, 1, KR_GREEN);
    if (flashId == KREMOTE_FLASH_LEFT) drawSectorHighlight(cx, cy, rMid, rInner, 2, KR_GREEN);
    if (flashId == KREMOTE_FLASH_RIGHT) drawSectorHighlight(cx, cy, rMid, rInner, 3, KR_GREEN);
    if (flashId == KREMOTE_FLASH_VOL_UP) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 0, KR_GREEN);
    if (flashId == KREMOTE_FLASH_VOL_DOWN) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 1, KR_GREEN);
    if (flashId == KREMOTE_FLASH_CH_DOWN) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 2, KR_GREEN);
    if (flashId == KREMOTE_FLASH_CH_UP) drawSectorHighlight(cx, cy, rOuter, rMid + 2, 3, KR_GREEN);

    // Extra bindings strip under the pad
    const int stripY = tftHeight - KR_FOOTER_H - KR_BIND_STRIP_H + 2;
    const int dense = uiDenseFont();
    const int lineH = uiLineH(dense);
    int x = 4;
    int y = stripY;

    auto digitPresent = [&](int d) -> bool {
        int act = (d == 0) ? KREMOTE_ACT_DIGIT_0 : (KREMOTE_ACT_DIGIT_1 + d - 1);
        return present[act];
    };
    auto digitFlash = [&](int d) -> bool {
        return flashId == KREMOTE_FLASH_DIGIT && flashDigit == d;
    };

    for (int d = 1; d <= 9; d++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", d);
        drawBindLabel(buf, x, y, digitPresent(d), digitFlash(d));
        x += dense * LW + 2;
    }
    drawBindLabel("0", x, y, digitPresent(0), digitFlash(0));

    y += lineH + 1;
    x = 4;
    drawBindLabel("`off", x, y, present[KREMOTE_ACT_POWER_OFF], flashId == KREMOTE_FLASH_POWER_OFF);
    x += 5 * dense * LW;
    drawBindLabel("O on", x, y, present[KREMOTE_ACT_POWER_ON], flashId == KREMOTE_FLASH_POWER_ON);
    x += 5 * dense * LW;
    drawBindLabel("m menu", x, y, present[KREMOTE_ACT_MENU], flashId == KREMOTE_FLASH_MENU);
    x += 7 * dense * LW;
    drawBindLabel("b back", x, y, present[KREMOTE_ACT_BACK], flashId == KREMOTE_FLASH_BACK);

    y += lineH + 1;
    x = 4;
    drawBindLabel("h home", x, y, present[KREMOTE_ACT_HOME], flashId == KREMOTE_FLASH_HOME);
    x += 7 * dense * LW;
    drawBindLabel("u mute", x, y, present[KREMOTE_ACT_MUTE], flashId == KREMOTE_FLASH_MUTE);
    x += 7 * dense * LW;
    drawBindLabel("sp/P play", x, y, present[KREMOTE_ACT_PLAY], flashId == KREMOTE_FLASH_PLAY);
    x += 10 * dense * LW;
    drawBindLabel("f ff", x, y, present[KREMOTE_ACT_FORWARD], flashId == KREMOTE_FLASH_FORWARD);
    x += 5 * dense * LW;
    drawBindLabel("r rw", x, y, present[KREMOTE_ACT_REWIND], flashId == KREMOTE_FLASH_REWIND);
#endif
}

void kremoteShowButtonMap() {
    drawMainBorderWithTitle("Button Map");
    tft.setTextSize(FP);
    tft.setCursor(10, 28);
    padprintln("Virtual Remote keys:");
    padprintln("1-9 0 = digits");
    padprintln("; . , / = arrows");
    padprintln("Enter=OK  -/= vol  [] ch");
    padprintln("`=power off  O=power on");
    padprintln("m menu  b back  h home");
    padprintln("u mute  sp/P play  f ff  r rw");
    padprintln("Del=exit  G0=home");
    padprintln("");
    padprintln("Press any key...");
    while (!check(AnyKeyPress) && !check(EscPress) && !check(SelPress) && !forceHome) delay(20);
    delay(150);
    while (!forceHome && (check(AnyKeyPress) || check(EscPress) || check(SelPress))) delay(10);
}

#include "charge_menu.h"
#include "charge_screen.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#ifdef HAS_RGB_LED
#include "root/hal/led_control.h"
#endif
#include <globals.h>
#include <interface.h>

void ChargeMenu::optionsMenu() {
    chargeModeActive = true;
    chargeUserSleep = false;
    isScreenOff = false;
    dimmer = false;
    previousMillis = millis();
    chargeInputGraceUntil = millis() + 2500;
#ifdef HAS_RGB_LED
    ledTakeExclusive();
    ledSuppressStatus(true);
    ledPauseEffects(true);
#endif

    check(SelPress);
    check(UpPress);
    check(DownPress);
    check(PrevPress);
    check(NextPress);
    check(AnyKeyPress);
    KeyStroke.Clear();
    resetHeldNavKeys();
    runChargeLoop();
}

void ChargeMenu::drawIcon(float scale) {
    clearIconArea();

    int w = (int)(scale * 28);
    int h = (int)(scale * 44);
    int x = iconCenterX - w / 2;
    int y = iconCenterY - h / 2 + (int)(scale * 2);
    int nubW = w / 3;
    int nubH = (int)(scale * 5);
    uint16_t color = kvxConfig.priColor;
    uint16_t bg = kvxConfig.bgColor;

    tft.fillRect(iconCenterX - nubW / 2, y - nubH, nubW, nubH, color);
    tft.drawRoundRect(x, y, w, h, (int)(scale * 3), color);
    tft.fillRoundRect(x + 2, y + h / 3, w - 4, h * 2 / 3 - 2, (int)(scale * 2), color);

    int bx = iconCenterX;
    int by = iconCenterY - (int)(scale * 4);
    int s = (int)(scale * 6);
    tft.fillTriangle(bx + s, by - s, bx - s / 2, by + s / 4, bx, by + s / 4, bg);
    tft.fillTriangle(bx - s, by + s, bx + s / 2, by - s / 4, bx, by - s / 4, bg);
}

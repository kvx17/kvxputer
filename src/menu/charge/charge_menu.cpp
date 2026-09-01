#include "charge_menu.h"
#include "charge_screen.h"
#include "root/app/utils.h"
#include "root/ui/settings.h"
#include "root/ui/theme.h"
#include <globals.h>

void ChargeMenu::optionsMenu() {
    // Arm before runChargeLoop so the input-task power saver cannot fade out
    // the panel during menu hand-off (chargeModeActive visibility + stale timer).
    chargeModeActive = true;
    chargeModeBright = 10;
    chargeUserSleep = false;
    previousMillis = millis();
    isScreenOff = false;
    dimmer = false;
    setBrightness(10, false);
    runChargeLoop();
}

static uint16_t chargeMenuBatColor() {
    int pct = getBattery();
    if (pct < 25) return 0xF800; // red
    if (pct < 50) return 0xFD20; // orange
    if (pct < 75) return 0xFFE0; // yellow
    return 0x07E0;               // green
}

void ChargeMenu::drawIcon(float scale) {
    clearIconArea();

    int w = (int)(scale * 28);
    int h = (int)(scale * 44);
    int x = iconCenterX - w / 2;
    int y = iconCenterY - h / 2 + (int)(scale * 2);
    int nubW = w / 3;
    int nubH = (int)(scale * 5);
    uint16_t color = chargeMenuBatColor();

    tft.fillRect(iconCenterX - nubW / 2, y - nubH, nubW, nubH, color);
    tft.drawRoundRect(x, y, w, h, (int)(scale * 3), color);
    tft.fillRoundRect(x + 2, y + h / 3, w - 4, h * 2 / 3 - 2, (int)(scale * 2), color);

    int bx = iconCenterX;
    int by = iconCenterY - (int)(scale * 4);
    int s = (int)(scale * 6);
    tft.fillTriangle(bx + s, by - s, bx - s / 2, by + s / 4, bx, by + s / 4, KVX_DEFAULT_BGCOLOR);
    tft.fillTriangle(bx - s, by + s, bx + s / 2, by - s / 4, bx, by - s / 4, KVX_DEFAULT_BGCOLOR);
}

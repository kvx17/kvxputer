#include "pc_connect_menu.h"
#include "pc_connect.h"
#include "root/ui/display.h"
#include <globals.h>

void PcConnectMenu::optionsMenu() {
#if !defined(LITE_VERSION)
    pcConnectMenu();
#else
    displayError("Not in lite build.", true);
#endif
}

void PcConnectMenu::drawIcon(float scale) {
    clearIconArea();
    // Link / host icon: two nodes with a cable.
    int r = (int)(scale * 7);
    if (r < 3) r = 3;
    int dx = (int)(scale * 18);
    int cy = iconCenterY;
    uint16_t c = kvxConfig.priColor;
    tft.fillCircle(iconCenterX - dx, cy, r, c);
    tft.fillCircle(iconCenterX + dx, cy, r, c);
    int t = (int)(scale * 3);
    if (t < 2) t = 2;
    tft.fillRect(iconCenterX - dx, cy - t / 2, 2 * dx, t, c);
    // Small "PC" tick on the right node
    int tick = (int)(scale * 6);
    tft.drawLine(iconCenterX + dx + r / 2, cy - tick, iconCenterX + dx + r / 2, cy + tick, kvxConfig.bgColor);
}

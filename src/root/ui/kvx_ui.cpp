#include "kvx_ui.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include <WiFi.h>

void drawKvxTopBar(const char *leftLabel) {
    const uint16_t bg = KVX_DEFAULT_BGCOLOR;
    const uint16_t purple = DEFAULT_PRICOLOR;
    const uint16_t green = DEFAULT_SECCOLOR;

    tft.fillRect(0, 0, tftWidth, KVX_TOPBAR_H, bg);
    tft.drawLine(0, KVX_TOPBAR_H, tftWidth, KVX_TOPBAR_H, purple);

    uint8_t bat = getBattery();
    const bool showWifi = (WiFi.getMode() != WIFI_MODE_NULL);
    const bool showBLE = BLEConnected;

    const int IW = 16;
    const int GAP = 4;
    int rightEdge = (bat > 0) ? (tftWidth - 85) : (tftWidth - 6);

    int iconX = rightEdge;
    if (showWifi) iconX -= IW + GAP;
    if (showBLE) iconX -= IW + GAP;

    int titleMaxX = iconX - 4;
    if (titleMaxX < 16) titleMaxX = 16;

    tft.setTextSize(FP);
    tft.setTextColor(green, bg);
    String label = (leftLabel && leftLabel[0] != '\0') ? String(leftLabel) : String("Menu");
    int maxChars = max(1, (titleMaxX - 8) / (FP * LW));
    if ((int)label.length() > maxChars) {
        if (maxChars > 1) label = label.substring(0, maxChars - 1) + ".";
        else label = label.substring(0, maxChars);
    }
    tft.drawString(label, 8, 6, 1);

    int x = iconX;
    int iy = 4;
    if (showBLE) {
        drawBLESmall(x, iy);
        x += IW + GAP;
    }
    if (showWifi) {
        drawWifiSmall(x, iy);
    }

    if (bat > 0) drawBatteryStatus(bat);
}

void drawKvxSubmenu(int index, std::vector<Option> &options, const char *title) {
    const uint16_t bg = KVX_DEFAULT_BGCOLOR;
    const uint16_t purple = DEFAULT_PRICOLOR;
    const uint16_t green = DEFAULT_SECCOLOR;

    tft.fillRect(0, KVX_TOPBAR_H + 1, tftWidth, tftHeight - KVX_TOPBAR_H - 1, bg);
    drawKvxTopBar(title);

    const int lineH = FM * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;

    int nchars = max(1, (tftWidth - 12) / (FM * LW));

    for (int i = scroll; i < (int)options.size() && i < scroll + visible; i++) {
        int y = startY + (i - scroll) * lineH;
        bool sel = (i == index);
        String line = sel ? String("> ") + options[i].label : String("  ") + options[i].label;
        if ((int)line.length() > nchars) line = line.substring(0, nchars);

        tft.setTextSize(FM);
        if (!options[i].enabled) {
            tft.setTextColor(TFT_DARKGREY, bg);
        } else if (sel) {
            tft.setTextColor(purple, bg);
        } else {
            tft.setTextColor(green, bg);
        }
        tft.drawString(line, 6, y, 1);
    }
}

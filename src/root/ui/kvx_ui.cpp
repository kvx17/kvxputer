#include "kvx_ui.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include "root/net/wg.h"
#include <WiFi.h>
#include <interface.h>

void drawKvxTopBar(const char *leftLabel) { drawKvxTopBar(leftLabel, nullptr); }

void drawKvxTopBar(const char *leftLabel, const char *statusLabel) {
    // Only open a present-frame when the caller is not already buffering.
    // Nested top-bar frames used to blit the strip alone mid-redraw and flash.
    const bool ownFrame = !tft.isFraming();
    if (ownFrame) tft.beginFrame();

    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t purple = kvxConfig.priColor;
    const uint16_t green = kvxConfig.secColor;

    tft.fillRect(0, 0, tftWidth, KVX_TOPBAR_H, bg);
    tft.drawLine(0, KVX_TOPBAR_H, tftWidth, KVX_TOPBAR_H, purple);

    uint8_t bat = getBattery();
    const bool showSD = sdcardMounted;
    const bool showGPS = gpsConnected;
    const bool showWifi = (WiFi.getMode() != WIFI_MODE_NULL);
    const bool showWeb = isWebUIActive;
    const bool showBLE = BLEConnected;
    const bool showWG = isConnectedWireguard;

    const int IW = 16;
    const int GAP = 4;
    int rightEdge = (bat > 0) ? (tftWidth - 85) : (tftWidth - 6);

    int iconCount = 0;
    if (showSD) iconCount++;
    if (showGPS) iconCount++;
    if (showWifi) iconCount++;
    if (showWeb) iconCount++;
    if (showBLE) iconCount++;
    if (showWG) iconCount++;

    int iconsWidth = 0;
    if (iconCount > 0) iconsWidth = iconCount * IW + (iconCount - 1) * GAP;
    int iconsLeft = rightEdge - iconsWidth;

    String status = (statusLabel && statusLabel[0]) ? String(statusLabel) : String();
    int statusRight = iconsLeft - 4;
    if (status.length()) {
        int maxStatusChars = max(4, (statusRight - 70) / (FP * LW));
        if ((int)status.length() > maxStatusChars) {
            status = status.substring(0, max(1, maxStatusChars - 1)) + ".";
        }
        tft.setTextSize(FP);
        tft.setTextColor(purple, bg);
        tft.drawRightString(status, statusRight, 6, 1);
        statusRight -= (int)status.length() * FP * LW + 4;
    }

    int titleMaxX = statusRight;
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

    int x = iconsLeft;
    int iy = 4;
    if (showWG) {
        drawWireguardStatus(x, iy);
        x += IW + GAP;
    }
    if (showBLE) {
        drawBLESmall(x, iy);
        x += IW + GAP;
    }
    if (showWeb) {
        drawWebUISmall(x, iy);
        x += IW + GAP;
    }
    if (showWifi) {
        drawWifiSmall(x, iy);
        x += IW + GAP;
    }
    if (showGPS) {
        drawGpsSmall(x, iy);
        x += IW + GAP;
    }
    if (showSD) {
        drawSdSmall(x, iy);
    }

    if (bat > 0) drawBatteryStatus(bat);

    if (ownFrame) tft.endFrame();
}
static int kvxSubLastIndex = -1;
static int kvxSubLastScroll = -1;
static int kvxSubLastCount = -1;
static String kvxSubLastTitle;
static String kvxSubLastFirst;
static String kvxSubLastLast;

void kvxInvalidateSubmenuCache() {
    kvxSubLastIndex = -1;
    kvxSubLastScroll = -1;
    kvxSubLastCount = -1;
    kvxSubLastTitle = "";
    kvxSubLastFirst = "";
    kvxSubLastLast = "";
}

static void drawKvxSubmenuRow(
    int i, int index, int scroll, int startY, int lineH, int nchars, std::vector<Option> &options,
    int marqueeOffset = 0
) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t purple = kvxConfig.priColor;
    const uint16_t green = kvxConfig.secColor;

    int y = startY + (i - scroll) * lineH;
    tft.fillRect(0, y, tftWidth, lineH, bg);

    bool sel = (i == index);
    String prefix = sel ? String("> ") : String("  ");
    String label = options[i].label;
    String line = prefix + label;
    if ((int)line.length() > nchars) {
        if (sel && marqueeOffset > 0) {
            // Slow horizontal scroll of the selected label.
            int avail = nchars - (int)prefix.length();
            if (avail < 1) avail = 1;
            int maxOff = max(0, (int)label.length() - avail);
            int off = marqueeOffset % (maxOff + 8); // pause at ends via extra slack
            if (off > maxOff) off = maxOff;
            line = prefix + label.substring(off, off + avail);
        } else {
            line = line.substring(0, nchars);
        }
    }

    tft.setTextSize(FM);
    if (!options[i].enabled) tft.setTextColor(TFT_DARKGREY, bg);
    else if (sel) tft.setTextColor(purple, bg);
    else tft.setTextColor(green, bg);
    tft.drawString(line, 6, y, 1);
}

void drawKvxSubmenu(int index, std::vector<Option> &options, const char *title) {
    const uint16_t bg = kvxConfig.bgColor;

    const int lineH = FM * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;
    int nchars = max(1, (tftWidth - 12) / (FM * LW));

    String titleStr = (title && title[0] != '\0') ? String(title) : String("Menu");
    String first = options.empty() ? String() : options.front().label;
    String last = options.empty() ? String() : options.back().label;

    bool listChanged = kvxSubLastCount != (int)options.size() || kvxSubLastFirst != first ||
                       kvxSubLastLast != last || kvxSubLastTitle != titleStr;
    bool fullRedraw = kvxSubLastIndex < 0 || kvxSubLastScroll != scroll || listChanged;

    // Marquee for long selected labels (advances while the same row stays selected).
    static int marqueeOffset = 0;
    static int marqueeIndex = -1;
    static unsigned long marqueeTs = 0;
    bool needsMarquee = false;
    bool marqueeAdvance = false;
    if (index >= 0 && index < (int)options.size()) {
        needsMarquee = ((int)options[index].label.length() + 2) > nchars;
    }
    if (marqueeIndex != index || fullRedraw) {
        marqueeIndex = index;
        marqueeOffset = 0;
        marqueeTs = millis();
    } else if (needsMarquee && millis() - marqueeTs > 280) {
        marqueeTs = millis();
        marqueeOffset++;
        marqueeAdvance = true;
    }

    // Nothing changed — skip frame present (avoids whole-screen flicker).
    if (!fullRedraw && kvxSubLastIndex == index && !marqueeAdvance) {
        return;
    }

    TftFrame frame;

    if (fullRedraw) {
        tft.fillScreen(bg);
        drawKvxTopBar(title);
        for (int i = scroll; i < (int)options.size() && i < scroll + visible; i++) {
            drawKvxSubmenuRow(
                i, index, scroll, startY, lineH, nchars, options, (i == index) ? marqueeOffset : 0
            );
        }
    } else if (kvxSubLastIndex != index) {
        if (kvxSubLastIndex >= scroll && kvxSubLastIndex < scroll + visible &&
            kvxSubLastIndex < (int)options.size()) {
            drawKvxSubmenuRow(kvxSubLastIndex, index, scroll, startY, lineH, nchars, options, 0);
        }
        if (index >= scroll && index < scroll + visible && index < (int)options.size()) {
            drawKvxSubmenuRow(index, index, scroll, startY, lineH, nchars, options, marqueeOffset);
        }
    } else if (marqueeAdvance) {
        drawKvxSubmenuRow(index, index, scroll, startY, lineH, nchars, options, marqueeOffset);
    }

    kvxSubLastIndex = index;
    kvxSubLastScroll = scroll;
    kvxSubLastCount = (int)options.size();
    kvxSubLastTitle = titleStr;
    kvxSubLastFirst = first;
    kvxSubLastLast = last;
}

#include "kvx_ui.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include "root/app/utils.h"
#include "root/net/wg.h"
#include <WiFi.h>
#include <interface.h>

static String kvxTopBarTitle = "Menu";
static String kvxTopBarStatus;

const char *kvxTopBarAppName() { return kvxTopBarTitle.c_str(); }

void drawKvxTopBar(const char *leftLabel) { drawKvxTopBar(leftLabel, nullptr); }

void drawKvxTopBar(const char *leftLabel, const char *statusLabel) {
    if (chargeModeActive) return;
    // Only open a present-frame when the caller is not already buffering.
    // Nested top-bar frames used to blit the strip alone mid-redraw and flash.
    const bool ownFrame = !tft.isFraming();
    if (ownFrame) tft.beginFrame();

    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t purple = kvxConfig.priColor;
    const uint16_t green = kvxConfig.secColor;

    if (leftLabel && leftLabel[0] != '\0') kvxTopBarTitle = leftLabel;
    kvxTopBarStatus = (statusLabel && statusLabel[0]) ? String(statusLabel) : String();

    tft.fillRect(0, 0, tftWidth, KVX_TOPBAR_H, bg);
    tft.drawLine(0, KVX_TOPBAR_H, tftWidth, KVX_TOPBAR_H, purple);

    uint8_t bat = getBattery();
    const bool showWifi = (WiFi.getMode() != WIFI_MODE_NULL);
    const bool showGPS = gpsConnected;
    const bool showBLE = BLEConnected;
    const bool showWeb = isWebUIActive;
    const bool showWG = isConnectedWireguard;
    const bool showSD = sdcardMounted;

    // Top-bar labels stay compact (GLCD dense).
    const int barSize = uiDenseFont();
    const int barTextY = max(4, (KVX_TOPBAR_H - barSize * LH) / 2);

    const int IW = 16;
    const int GAP = 4;
    // Battery slot is always reserved so the layout does not jump when percent is 0.
    const int batReserve = 42 + max(42, 4 * barSize * LW + 4);
    int rightEdge = tftWidth - batReserve;

    int iconCount = 0;
    if (showWifi) iconCount++;
    if (showGPS) iconCount++;
    if (showBLE) iconCount++;
    if (showWeb) iconCount++;
    if (showWG) iconCount++;
    if (showSD) iconCount++;

    int iconsWidth = 0;
    if (iconCount > 0) iconsWidth = iconCount * IW + (iconCount - 1) * GAP;
    int iconsLeft = rightEdge - iconsWidth;

    // Compact HH:MM immediately left of the icon pack / battery (right cluster).
    int statusRight = iconsLeft - 4;
    if (clock_set) {
#if defined(HAS_RTC)
        updateTimeStr(_rtc.getTimeStruct());
#else
        updateTimeStr(rtc.getTimeStruct());
#endif
        char hhmm[6];
        snprintf(hhmm, sizeof(hhmm), "%.5s", timeStr);
        tft.setTextSize(barSize);
        tft.setTextColor(purple, bg);
        tft.drawRightString(hhmm, statusRight, barTextY, 1);
        statusRight -= (int)strlen(hhmm) * barSize * LW + 6;
    }

    String status = kvxTopBarStatus;
    if (status.length()) {
        int maxStatusChars = max(4, (statusRight - 70) / (barSize * LW));
        if ((int)status.length() > maxStatusChars) {
            status = status.substring(0, max(1, maxStatusChars - 1)) + ".";
        }
        tft.setTextSize(barSize);
        tft.setTextColor(purple, bg);
        tft.drawRightString(status, statusRight, barTextY, 1);
        statusRight -= (int)status.length() * barSize * LW + 4;
    }

    int titleMaxX = statusRight;
    if (titleMaxX < 16) titleMaxX = 16;

    tft.setTextSize(barSize);
    tft.setTextColor(green, bg);
    String label = kvxTopBarTitle;
    // Hard cap 10 characters, then clip further if icons leave less room.
    if ((int)label.length() > 10) label = label.substring(0, 9) + ".";
    int maxChars = max(1, min(10, (titleMaxX - 8) / (barSize * LW)));
    if ((int)label.length() > maxChars) {
        if (maxChars > 1) label = label.substring(0, maxChars - 1) + ".";
        else label = label.substring(0, maxChars);
    }
    tft.drawString(label, 8, barTextY, 1);

    // Active modules packed against the battery. Order: Wi-Fi, GPS, BLE, Web, WG, SD.
    int x = iconsLeft;
    int iy = max(4, (KVX_TOPBAR_H - IW) / 2);
    if (showWifi) {
        drawWifiSmall(x, iy);
        x += IW + GAP;
    }
    if (showGPS) {
        drawGpsSmall(x, iy);
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
    if (showWG) {
        drawWireguardStatus(x, iy);
        x += IW + GAP;
    }
    if (showSD) {
        drawSdSmall(x, iy);
    }

    drawBatteryStatus(bat);

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
    if (chargeModeActive) return;
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

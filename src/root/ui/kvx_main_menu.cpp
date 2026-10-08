#include "kvx_main_menu.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/theme.h"
#include "root/input/mykeyboard.h"
#include "root/app/powerSave.h"
#include "root/app/utils.h"
#include "root/hal/led_control.h"
#include "root/app/app_catalog.h"
#ifndef LITE_VERSION
#include "menu/others/pda/pda_alarms.h"
#endif
#include <globals.h>

#if defined(ARDUINO_M5STACK_TAB5)
// 1280×720 landscape: denser channel grid (3 rows × 6 cols = 18 per page).
static constexpr int KVX_COLS = 6;
static constexpr int KVX_ROWS = 3;
static constexpr int KVX_FOOTER_H = 56;
static constexpr int KVX_GAP = 6;
static constexpr int KVX_RADIUS = 10;
#else
static constexpr int KVX_COLS = 3;
static constexpr int KVX_ROWS = 2;
static constexpr int KVX_FOOTER_H = 26;
static constexpr int KVX_GAP = 4;
static constexpr int KVX_RADIUS = 6;
#endif
static constexpr int KVX_SLOTS = KVX_COLS * KVX_ROWS;

// Page changes and returns from a submenu must repaint the whole grid.
// A selection change only repaints the two tiles and the footer.
static int s_lastIndex = -1;
static int s_lastPage = -1;

#if defined(HAS_TOUCH)
static bool s_kvxMenuActive = false;
static int s_kvxMenuIndex = 0;
static int s_kvxMenuCount = 0;
static bool s_touchSelectPending = false;
static std::vector<MenuItemInterface *> *s_kvxMenuItems = nullptr;
#endif

// Column-major layout:
//   0  2  4
//   1  3  5
static void kvxRowColFromIndex(int index, int &row, int &col, int &pageBase) {
    pageBase = (index / KVX_SLOTS) * KVX_SLOTS;
    int slot = index - pageBase;
    row = slot % KVX_ROWS;
    col = slot / KVX_ROWS;
}

static int kvxIndexFromRowCol(int pageBase, int row, int col, int count) {
    int idx = pageBase + col * KVX_ROWS + row;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

static void kvxMoveNext(int &index, int count) {
    index = (index + 1) % count;
}

static void kvxMovePrev(int &index, int count) {
    index = (index - 1 + count) % count;
}

static void drawArrow(int x, int y, bool right, uint16_t color) {
    if (right) {
        tft.fillTriangle(x, y, x, y + 14, x + 10, y + 7, color);
    } else {
        tft.fillTriangle(x + 10, y, x + 10, y + 14, x, y + 7, color);
    }
}

static void drawChannelTile(int x, int y, int w, int h, bool selected, MenuItemInterface *item, float scale) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const uint16_t dark = getColorVariation(pri, 10, -1);
    uint16_t fill = selected ? pri : dark;
    uint16_t border = selected ? sec : pri;
    const int r = KVX_RADIUS;
    tft.fillRoundRect(x, y, w, h, r, fill);
    tft.drawRoundRect(x, y, w, h, r, border);
    if (selected) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 1, sec);

    if (!item) return;
    item->drawIconAt(scale, x + w / 2, y + h / 2, w - 6, h - 6, fill);
}

struct GridGeom {
    int top;
    int gridH;
    int marginX;
    int arrowW;
    int originX;
    int cellW;
    int cellH;
    float scale;
    bool hasPrev;
    bool hasNext;
    int pageStart;
};

static GridGeom computeGridGeom(int globalIndex, int count) {
    GridGeom g;
    const int page = globalIndex / KVX_SLOTS;
    g.pageStart = page * KVX_SLOTS;
    g.top = KVX_TOPBAR_H + 4;
    const int bottom = tftHeight - KVX_FOOTER_H;
    g.gridH = bottom - g.top - 4;
    g.marginX = 8;
    g.arrowW = 14;
    g.hasPrev = page > 0;
    g.hasNext = g.pageStart + KVX_SLOTS < count;

    int usableW = tftWidth - g.marginX * 2;
    if (g.hasPrev) usableW -= g.arrowW;
    if (g.hasNext) usableW -= g.arrowW;
    g.originX = g.marginX + (g.hasPrev ? g.arrowW : 0);
    g.cellW = (usableW - (KVX_COLS - 1) * KVX_GAP) / KVX_COLS;
    g.cellH = (g.gridH - (KVX_ROWS - 1) * KVX_GAP) / KVX_ROWS;
#if defined(ARDUINO_M5STACK_TAB5)
    // Fit icons to the denser 6×3 cells (Cardputer scale is huge on 720p).
    const float cellScale = (float)(g.cellW < g.cellH ? g.cellW : g.cellH) / 80.0f;
    g.scale = cellScale > 0.4f ? cellScale : 0.4f;
#else
    g.scale = (float)tftWidth / 240.0f;
    if (kvxConfigPins.rotation & 0b01) g.scale = (float)tftHeight / 135.0f;
#endif
    return g;
}

static void tileXY(const GridGeom &g, int itemIdx, int &x, int &y) {
    int slot = itemIdx - g.pageStart;
    int row = slot % KVX_ROWS;
    int col = slot / KVX_ROWS;
    x = g.originX + col * (g.cellW + KVX_GAP);
    y = g.top + row * (g.cellH + KVX_GAP);
}

static void drawFooter(int globalIndex, std::vector<MenuItemInterface *> &items) {
    String label = items[globalIndex]->getName();
    tft.fillRect(0, tftHeight - KVX_FOOTER_H, tftWidth, KVX_FOOTER_H, kvxConfig.secColor);
    tft.setTextSize(FM);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.secColor);
    tft.drawCentreString(label, tftWidth / 2, tftHeight - KVX_FOOTER_H + 6, 1);
}

static void drawOneTile(
    const GridGeom &g, int itemIdx, int selectedIdx, int count, std::vector<MenuItemInterface *> &items
) {
    int x, y;
    tileXY(g, itemIdx, x, y);
    // Icon arcs are larger than a tile. Clip so a selection change cannot
    // repaint the neighbor and shimmer the grid.
    tft.setClipRect(x, y, g.cellW, g.cellH);
    bool selected = (itemIdx == selectedIdx);
    MenuItemInterface *item = (itemIdx < count) ? items[itemIdx] : nullptr;
#if defined(ARDUINO_M5STACK_TAB5)
    const float iconScale = g.scale;
#else
    const float iconScale = g.scale * 0.5f;
#endif
    if (item) drawChannelTile(x, y, g.cellW, g.cellH, selected, item, iconScale);
    else {
        tft.fillRoundRect(x, y, g.cellW, g.cellH, KVX_RADIUS, kvxConfig.bgColor);
        tft.drawRoundRect(
            x, y, g.cellW, g.cellH, KVX_RADIUS, getColorVariation(kvxConfig.priColor, 10, -1)
        );
    }
    tft.clearClipRect();
}

static void invalidateKvxGridCache() {
    s_lastIndex = -1;
    s_lastPage = -1;
}

static void drawKvxGrid(int globalIndex, std::vector<MenuItemInterface *> &items) {
    const int count = (int)items.size();
    if (count == 0) return;

    const int page = globalIndex / KVX_SLOTS;
    GridGeom g = computeGridGeom(globalIndex, count);
    const bool fullRedraw = (s_lastIndex < 0 || s_lastPage != page);

    if (fullRedraw) {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("kvxputer");
        tft.fillRect(0, g.top, tftWidth, g.gridH + 4, kvxConfig.bgColor);

        for (int slot = 0; slot < KVX_SLOTS; slot++) {
            int itemIdx = g.pageStart + slot;
            drawOneTile(g, itemIdx, globalIndex, count, items);
        }

        int midY = g.top + g.gridH / 2 - 7;
        if (g.hasPrev) drawArrow(g.marginX, midY, false, kvxConfig.secColor);
        if (g.hasNext) drawArrow(tftWidth - g.marginX - 10, midY, true, kvxConfig.secColor);

        drawFooter(globalIndex, items);
    } else if (s_lastIndex != globalIndex) {
        // Separate presents. One frame would union the tiles and the footer
        // into a single blit of almost the whole panel.
        if (s_lastIndex >= g.pageStart && s_lastIndex < g.pageStart + KVX_SLOTS) {
            TftFrame frame;
            drawOneTile(g, s_lastIndex, globalIndex, count, items);
        }
        if (globalIndex >= g.pageStart && globalIndex < g.pageStart + KVX_SLOTS) {
            TftFrame frame;
            drawOneTile(g, globalIndex, globalIndex, count, items);
        }
        TftFrame footer;
        drawFooter(globalIndex, items);
    }

    s_lastIndex = globalIndex;
    s_lastPage = page;
}

#if defined(HAS_TOUCH)
bool kvxMainMenuActive(void) { return s_kvxMenuActive; }

int kvxMainMenuIndexAt(int x, int y) {
    if (!s_kvxMenuActive || s_kvxMenuCount <= 0) return -1;
    GridGeom g = computeGridGeom(s_kvxMenuIndex, s_kvxMenuCount);
    for (int i = g.pageStart; i < s_kvxMenuCount && i < g.pageStart + KVX_SLOTS; i++) {
        int tx, ty;
        tileXY(g, i, tx, ty);
        if (x >= tx && x < tx + g.cellW && y >= ty && y < ty + g.cellH) return i;
    }
    return -1;
}

void kvxMainMenuSelectIndex(int index) {
    if (!s_kvxMenuActive || index < 0 || index >= s_kvxMenuCount) return;
    s_kvxMenuIndex = index;
    s_touchSelectPending = true;
}
#endif

int kvxMainMenuLoop(std::vector<MenuItemInterface *> &items, int startIndex) {
    if (items.empty()) return -1;

    const int count = (int)items.size();
    int index = startIndex;
    if (index < 0 || index >= count) index = 0;

    bool redraw = true;
    unsigned long menuOpenTs = 0;
    unsigned long batTimer = millis();
    int devModeCounter = 0;
    invalidateKvxGridCache();

#if defined(HAS_TOUCH)
    s_kvxMenuActive = true;
    s_kvxMenuItems = &items;
    s_kvxMenuCount = count;
    s_kvxMenuIndex = index;
    s_touchSelectPending = false;
    menuOptionType = MENU_TYPE_MAIN;
#endif

    while (true) {
#if defined(HAS_TOUCH)
        menuOptionType = MENU_TYPE_MAIN;
#endif
        checkReboot();

#ifndef LITE_VERSION
        if (pdaAlarmsPoll()) {
            invalidateKvxGridCache();
            redraw = true;
        }
#endif

        if (redraw) {
            drawKvxGrid(index, items);
            ledSetStatus(LED_STATUS_IDLE);
            menuOpenTs = millis();
            batTimer = millis();
            redraw = false;
        } else if (millis() - batTimer > 30000) {
            batTimer = millis();
            drawKvxTopBar("kvxputer");
        }

        if (forceHome) {
            forceHome = false;
            returnToMenu = false;
            EscPress = false;
            ledSetStatus(LED_STATUS_IDLE);
            invalidateKvxGridCache();
            redraw = true;
            continue;
        }

#ifdef HAS_KEYBOARD
        if (appCatalogHandleMainscreenKeys()) {
            invalidateKvxGridCache();
            redraw = true;
            continue;
        }
#endif

#ifdef HAS_ENCODER
        int32_t rotarySteps = drainRotarySteps();
        if (rotarySteps != 0) {
            while (rotarySteps > 0) {
                kvxMoveNext(index, count);
                if (!kvxConfig.devMode && index == 0) devModeCounter++;
                rotarySteps--;
            }
            while (rotarySteps < 0) {
                kvxMovePrev(index, count);
                rotarySteps++;
            }
            redraw = true;
        }
#endif

        int row = 0, col = 0, pageBase = 0;
        kvxRowColFromIndex(index, row, col, pageBase);

        // Vertical: up / down within column
        if (check(UpPress)) {
            devModeCounter = 0;
            if (row > 0) {
                int ni = kvxIndexFromRowCol(pageBase, row - 1, col, count);
                if (ni >= 0) index = ni;
            } else {
                kvxMovePrev(index, count);
            }
            redraw = true;
        }
        if (check(DownPress)) {
            if (row < KVX_ROWS - 1) {
                int ni = kvxIndexFromRowCol(pageBase, row + 1, col, count);
                if (ni >= 0) index = ni;
                else kvxMoveNext(index, count);
            } else {
                kvxMoveNext(index, count);
                if (!kvxConfig.devMode && index == 0) devModeCounter++;
            }
            redraw = true;
        }

        // Horizontal: left / right between columns; wrap first <-> last when
        // there is no valid tile further in that direction (same as up/down).
        if (check(PrevPress) && !check(UpPress)) {
            devModeCounter = 0;
            int ni = -1;
            if (col > 0) {
                ni = kvxIndexFromRowCol(pageBase, row, col - 1, count);
            } else if (pageBase > 0) {
                ni = kvxIndexFromRowCol(pageBase - KVX_SLOTS, row, KVX_COLS - 1, count);
            }
            if (ni >= 0) index = ni;
            else kvxMovePrev(index, count);
            redraw = true;
        }
        if (check(NextPress) && !check(DownPress)) {
            int ni = -1;
            if (col < KVX_COLS - 1) {
                ni = kvxIndexFromRowCol(pageBase, row, col + 1, count);
            } else if (pageBase + KVX_SLOTS < count) {
                ni = kvxIndexFromRowCol(pageBase + KVX_SLOTS, row, 0, count);
            }
            if (ni >= 0) index = ni;
            else {
                kvxMoveNext(index, count);
                if (!kvxConfig.devMode && index == 0) devModeCounter++;
            }
            redraw = true;
        }

        if (devModeCounter >= 5 && !kvxConfig.devMode) {
            kvxConfig.setDevMode(true);
            displayInfo("Dev Mode Enabled", true);
            invalidateKvxGridCache();
            redraw = true;
        }

#if defined(HAS_TOUCH)
        // Apply tap after arrow/check() may have run InputHandler this frame.
        // Only pending taps override index — keyboard moves must not be undone.
        if (s_touchSelectPending && s_kvxMenuIndex >= 0 && s_kvxMenuIndex < count) {
            index = s_kvxMenuIndex;
            s_touchSelectPending = false;
            redraw = true;
        } else {
            s_kvxMenuIndex = index;
        }
#endif

        static const unsigned long MENU_SELECT_IGNORE_MS = 600;
        if (millis() - menuOpenTs > MENU_SELECT_IGNORE_MS && check(SelPress)) {
            ledSetStatus(LED_STATUS_BUSY);
#if defined(HAS_TOUCH)
            s_kvxMenuActive = false;
#endif
            items[index]->optionsMenu();
#if defined(HAS_TOUCH)
            s_kvxMenuActive = true;
            menuOptionType = MENU_TYPE_MAIN;
#endif
            ledSetStatus(LED_STATUS_IDLE);
            invalidateKvxGridCache();
            redraw = true;
            if (forceHome) {
                forceHome = false;
                returnToMenu = false;
                EscPress = false;
            } else if (returnToMenu) {
                returnToMenu = false;
#if defined(HAS_TOUCH)
                s_kvxMenuActive = false;
                s_kvxMenuItems = nullptr;
#endif
                return index;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
#if defined(HAS_TOUCH)
    s_kvxMenuActive = false;
    s_kvxMenuItems = nullptr;
#endif
}

#include "kvx_main_menu.h"
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/app/powerSave.h"
#include "root/app/utils.h"
#include <globals.h>

static const uint16_t KVX_PURPLE = 0x9818;
static const uint16_t KVX_PURPLE_DARK = 0x600C;
static const uint16_t KVX_GREEN = 0x07E0;
static const uint16_t KVX_BG = 0x0841;

static constexpr int KVX_COLS = 3;
static constexpr int KVX_ROWS = 2;
static constexpr int KVX_SLOTS = KVX_COLS * KVX_ROWS;
static constexpr int KVX_FOOTER_H = 26;

void kvxApplyThemeDefaults() {
    kvxConfig.priColor = KVX_PURPLE;
    kvxConfig.secColor = KVX_GREEN;
    kvxConfig.bgColor = KVX_BG;
}

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
    uint16_t fill = selected ? KVX_PURPLE : KVX_PURPLE_DARK;
    uint16_t border = selected ? KVX_GREEN : KVX_PURPLE;
    tft.fillRoundRect(x, y, w, h, 6, fill);
    tft.drawRoundRect(x, y, w, h, 6, border);
    if (selected) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, KVX_GREEN);

    if (!item) return;
    item->drawIconAt(scale, x + w / 2, y + h / 2, w - 6, h - 6, fill);
}

static void drawKvxGrid(int globalIndex, std::vector<MenuItemInterface *> &items) {
    const int count = (int)items.size();
    if (count == 0) return;

    const int page = globalIndex / KVX_SLOTS;
    const int pageStart = page * KVX_SLOTS;
    const int top = 28;
    const int bottom = tftHeight - KVX_FOOTER_H;
    const int gridH = bottom - top - 4;
    const int marginX = 8;
    const int arrowW = 14;
    const bool hasPrev = page > 0;
    const bool hasNext = pageStart + KVX_SLOTS < count;

    int usableW = tftWidth - marginX * 2;
    if (hasPrev) usableW -= arrowW;
    if (hasNext) usableW -= arrowW;
    int originX = marginX + (hasPrev ? arrowW : 0);

    int cellW = (usableW - (KVX_COLS - 1) * 4) / KVX_COLS;
    int cellH = (gridH - (KVX_ROWS - 1) * 4) / KVX_ROWS;
    float scale = (float)tftWidth / 240.0f;
    if (kvxConfigPins.rotation & 0b01) scale = (float)tftHeight / 135.0f;

    tft.fillRect(0, top, tftWidth, gridH + 4, KVX_BG);

    for (int slot = 0; slot < KVX_SLOTS; slot++) {
        int row = slot % KVX_ROWS;
        int col = slot / KVX_ROWS;
        int itemIdx = pageStart + slot;
        int x = originX + col * (cellW + 4);
        int y = top + row * (cellH + 4);
        bool selected = (itemIdx == globalIndex);
        MenuItemInterface *item = (itemIdx < count) ? items[itemIdx] : nullptr;
        if (item) drawChannelTile(x, y, cellW, cellH, selected, item, scale * 0.5f);
        else {
            tft.fillRoundRect(x, y, cellW, cellH, 6, KVX_BG);
            tft.drawRoundRect(x, y, cellW, cellH, 6, KVX_PURPLE_DARK);
        }
    }

    int midY = top + gridH / 2 - 7;
    if (hasPrev) drawArrow(marginX, midY, false, KVX_GREEN);
    if (hasNext) drawArrow(tftWidth - marginX - 10, midY, true, KVX_GREEN);

    String label = items[globalIndex]->getName();
    tft.fillRect(0, tftHeight - KVX_FOOTER_H, tftWidth, KVX_FOOTER_H, KVX_GREEN);
    tft.setTextSize(FM);
    tft.setTextColor(KVX_PURPLE, KVX_GREEN);
    tft.drawCentreString(label, tftWidth / 2, tftHeight - KVX_FOOTER_H + 6, 1);

    tft.fillRect(0, 0, tftWidth, 24, KVX_BG);
    tft.drawLine(0, 24, tftWidth, 24, KVX_PURPLE);
    tft.setTextSize(FP);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.drawString("kvxputer", 8, 6, 1);
    uint8_t bat = getBattery();
    if (bat > 0) drawBatteryStatus(bat);
}

int kvxMainMenuLoop(std::vector<MenuItemInterface *> &items, int startIndex) {
    if (items.empty()) return -1;

    const int count = (int)items.size();
    int index = startIndex;
    if (index < 0 || index >= count) index = 0;

    bool redraw = true;
    unsigned long menuOpenTs = 0;
    int devModeCounter = 0;

    while (true) {
        checkReboot();

        if (redraw) {
            drawKvxGrid(index, items);
            menuOpenTs = millis();
            redraw = false;
        }

#ifdef HAS_KEYBOARD
        if (checkShortcutPress()) return index;
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

        // Horizontal: left / right between columns (1 <-> 3, not 1 <-> 2)
        if (check(PrevPress) && !check(UpPress)) {
            devModeCounter = 0;
            if (col > 0) {
                int ni = kvxIndexFromRowCol(pageBase, row, col - 1, count);
                if (ni >= 0) index = ni;
            } else if (pageBase > 0) {
                int prevBase = pageBase - KVX_SLOTS;
                int ni = kvxIndexFromRowCol(prevBase, row, KVX_COLS - 1, count);
                if (ni >= 0) index = ni;
            } else {
                kvxMovePrev(index, count);
            }
            redraw = true;
        }
        if (check(NextPress) && !check(DownPress)) {
            if (col < KVX_COLS - 1) {
                int ni = kvxIndexFromRowCol(pageBase, row, col + 1, count);
                if (ni >= 0) index = ni;
            } else if (pageBase + KVX_SLOTS < count) {
                int nextBase = pageBase + KVX_SLOTS;
                int ni = kvxIndexFromRowCol(nextBase, row, 0, count);
                if (ni >= 0) index = ni;
            } else {
                kvxMoveNext(index, count);
                if (!kvxConfig.devMode && index == 0) devModeCounter++;
            }
            redraw = true;
        }

        if (devModeCounter >= 5 && !kvxConfig.devMode) {
            kvxConfig.setDevMode(true);
            displayInfo("Dev Mode Enabled", true);
        }

        static const unsigned long MENU_SELECT_IGNORE_MS = 600;
        if (millis() - menuOpenTs > MENU_SELECT_IGNORE_MS && check(SelPress)) {
            items[index]->optionsMenu();
            redraw = true;
            if (returnToMenu) {
                returnToMenu = false;
                return index;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

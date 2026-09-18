#include "pda_menu.h"
#include "pda_alarms.h"
#include "pda_calendar.h"
#include "pda_common.h"
#include "pda_contacts.h"
#include "pda_memos.h"
#include "pda_notes.h"
#include "pda_todo.h"
#include "pda_worldclock.h"

#include "menu/others/calculator.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include <globals.h>

namespace {

struct PdaChannel {
    const char *label;
    void (*open)();
};

// Wii-style channel grid: soft rounded tiles, glyph icon, label in footer.
static constexpr int PDA_COLS = 4;
static constexpr int PDA_ROWS = 2;
static constexpr int PDA_SLOTS = PDA_COLS * PDA_ROWS;
static constexpr int PDA_FOOTER_H = 22;
static constexpr int PDA_TOP_H = 18;

enum {
    PDA_CH_NOTES = 0,
    PDA_CH_MEMOS,
    PDA_CH_TODO,
    PDA_CH_CALENDAR,
    PDA_CH_CONTACTS,
    PDA_CH_ALARMS,
    PDA_CH_WORLDCLOCK,
    PDA_CH_CALC,
    PDA_CH_COUNT
};

static const PdaChannel kChannels[PDA_CH_COUNT] = {
    {"Notes",       pdaNotes      },
    {"Memos",       pdaMemos      },
    {"To-Do",       pdaTodo       },
    {"Calendar",    pdaCalendar   },
    {"Contacts",    pdaContacts   },
    {"Alarms",      pdaAlarms     },
    {"World Clock", pdaWorldClock },
    {"Calculator",  calculatorApp },
};

// Column-major like the main kvx grid:
//   0  2  4  6
//   1  3  5  7
static void slotRowCol(int slot, int &row, int &col) {
    row = slot % PDA_ROWS;
    col = slot / PDA_ROWS;
}

static int indexFromRowCol(int row, int col) {
    if (row < 0 || row >= PDA_ROWS || col < 0 || col >= PDA_COLS) return -1;
    int idx = col * PDA_ROWS + row;
    if (idx < 0 || idx >= PDA_CH_COUNT) return -1;
    return idx;
}

static void drawGlyph(int id, int cx, int cy, uint16_t color) {
    switch (id) {
        case PDA_CH_NOTES: {
            tft.drawRoundRect(cx - 8, cy - 9, 16, 18, 2, color);
            tft.drawFastHLine(cx - 5, cy - 4, 10, color);
            tft.drawFastHLine(cx - 5, cy, 10, color);
            tft.drawFastHLine(cx - 5, cy + 4, 7, color);
            break;
        }
        case PDA_CH_MEMOS: {
            tft.drawRoundRect(cx - 7, cy - 8, 14, 16, 2, color);
            tft.drawFastHLine(cx - 4, cy - 3, 8, color);
            tft.drawFastHLine(cx - 4, cy + 1, 8, color);
            tft.drawFastHLine(cx - 4, cy + 5, 5, color);
            break;
        }
        case PDA_CH_TODO: {
            tft.drawRect(cx - 9, cy - 5, 8, 8, color);
            tft.drawLine(cx - 7, cy - 1, cx - 5, cy + 1, color);
            tft.drawLine(cx - 5, cy + 1, cx - 2, cy - 3, color);
            tft.drawFastHLine(cx + 1, cy - 3, 8, color);
            tft.drawFastHLine(cx + 1, cy + 1, 8, color);
            tft.drawFastHLine(cx + 1, cy + 5, 6, color);
            break;
        }
        case PDA_CH_CALENDAR: {
            tft.drawRoundRect(cx - 9, cy - 8, 18, 16, 2, color);
            tft.drawFastHLine(cx - 9, cy - 3, 18, color);
            tft.fillRect(cx - 6, cy - 11, 2, 4, color);
            tft.fillRect(cx + 4, cy - 11, 2, 4, color);
            tft.fillRect(cx - 5, cy + 1, 3, 3, color);
            tft.fillRect(cx + 1, cy + 1, 3, 3, color);
            break;
        }
        case PDA_CH_CONTACTS: {
            tft.fillCircle(cx, cy - 4, 4, color);
            tft.fillRoundRect(cx - 8, cy + 2, 16, 8, 4, color);
            break;
        }
        case PDA_CH_ALARMS: {
            tft.drawCircle(cx, cy, 8, color);
            tft.drawFastVLine(cx, cy - 5, 5, color);
            tft.drawLine(cx, cy, cx + 4, cy + 3, color);
            tft.drawLine(cx - 6, cy - 8, cx - 4, cy - 6, color);
            tft.drawLine(cx + 6, cy - 8, cx + 4, cy - 6, color);
            break;
        }
        case PDA_CH_WORLDCLOCK: {
            tft.drawCircle(cx, cy, 8, color);
            tft.drawEllipse(cx, cy, 4, 8, color);
            tft.drawFastHLine(cx - 8, cy, 16, color);
            tft.drawFastVLine(cx, cy - 8, 16, color);
            break;
        }
        case PDA_CH_CALC: {
            tft.drawRoundRect(cx - 8, cy - 9, 16, 18, 2, color);
            tft.drawRect(cx - 5, cy - 6, 10, 4, color);
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 3; c++) {
                    tft.fillRect(cx - 5 + c * 4, cy + r * 4, 2, 2, color);
                }
            }
            break;
        }
        default: break;
    }
}

// Hub captions stay at the pre-FP-bump size so tiles/footer fit the grid.
static constexpr int PDA_FONT = 1;

static void drawChannelTile(int x, int y, int w, int h, int id, bool selected) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const uint16_t bg = kvxConfig.bgColor;
    // Soft “channel” fill: dim primary when idle, selected uses secondary glow.
    const uint16_t idleFill = getColorVariation(pri, 14, -1);
    const uint16_t fill = selected ? getColorVariation(sec, 6, -1) : idleFill;
    const uint16_t border = selected ? sec : getColorVariation(pri, 6, -1);
    const int radius = 10;

    tft.fillRoundRect(x, y, w, h, radius, fill);
    tft.drawRoundRect(x, y, w, h, radius, border);
    if (selected) {
        tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, radius - 1, sec);
        tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, radius - 2, pri);
    }

    // Digit that opens this channel (first key bound to it).
    char digit = '?';
    for (int k = 0; k < 8; k++) {
        if (kvxConfig.pdaKeyBind[k] == (uint8_t)id) {
            digit = (char)('1' + k);
            break;
        }
    }
    tft.setTextSize(PDA_FONT);
    tft.setTextColor(selected ? pri : sec, fill);
    tft.setCursor(x + 4, y + 3);
    tft.print(digit);

    drawGlyph(id, x + w / 2, y + h / 2 - 2, selected ? pri : sec);

    // Tiny label under the glyph inside the tile (Wii-like channel caption).
    tft.setTextSize(PDA_FONT);
    tft.setTextColor(selected ? pri : sec, fill);
    String shortLabel = kChannels[id].label;
    if (shortLabel.length() > 8) shortLabel = shortLabel.substring(0, 8);
    tft.drawCentreString(shortLabel, x + w / 2, y + h - PDA_FONT * LH - 3, 1);
    (void)bg;
}

static int s_pdaLastIndex = -1;

static void pdaHubGeom(int &top, int &marginX, int &gap, int &cellW, int &cellH) {
    top = PDA_TOP_H + 2;
    const int bottom = tftHeight - PDA_FOOTER_H;
    const int gridH = bottom - top - 2;
    marginX = 6;
    gap = 4;
    cellW = (tftWidth - 2 * marginX - (PDA_COLS - 1) * gap) / PDA_COLS;
    cellH = (gridH - (PDA_ROWS - 1) * gap) / PDA_ROWS;
}

static void pdaTileXY(int id, int marginX, int gap, int cellW, int cellH, int top, int &x, int &y) {
    int row = 0, col = 0;
    for (int s = 0; s < PDA_SLOTS; s++) {
        int r = 0, c = 0;
        slotRowCol(s, r, c);
        if (indexFromRowCol(r, c) == id) {
            row = r;
            col = c;
            break;
        }
    }
    x = marginX + col * (cellW + gap);
    y = top + row * (cellH + gap);
}

static void drawPdaFooter(int index) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    tft.fillRoundRect(0, tftHeight - PDA_FOOTER_H, tftWidth, PDA_FOOTER_H, 0, sec);
    tft.setTextSize(PDA_FONT);
    tft.setTextColor(pri, sec);
    String foot = String(kChannels[index].label) + "  1-8 open  9 bind";
    tft.drawCentreString(foot, tftWidth / 2, tftHeight - PDA_FOOTER_H + 6, 1);
}

static void invalidatePdaHubCache() { s_pdaLastIndex = -1; }

static void drawPdaHub(int index) {
    // TftFrame → M5Canvas when PSRAM allows; beginFrame fails soft on tight DRAM.
    TftFrame frame;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t bg = kvxConfig.bgColor;

    int top, marginX, gap, cellW, cellH;
    pdaHubGeom(top, marginX, gap, cellW, cellH);

    const bool fullRedraw = (s_pdaLastIndex < 0);

    if (fullRedraw) {
        tft.fillScreen(bg);

        tft.setTextSize(PDA_FONT + 1); // was FM when FP=1; keep title one step above hub captions
        tft.setTextColor(pri, bg);
        tft.drawCentreString("PDA", tftWidth / 2, 3, 1);
        tft.drawFastHLine(8, PDA_TOP_H - 1, tftWidth - 16, getColorVariation(pri, 8, -1));

        for (int slot = 0; slot < PDA_SLOTS; slot++) {
            int row = 0, col = 0;
            slotRowCol(slot, row, col);
            int id = indexFromRowCol(row, col);
            int x = marginX + col * (cellW + gap);
            int y = top + row * (cellH + gap);
            if (id >= 0) drawChannelTile(x, y, cellW, cellH, id, id == index);
            else {
                tft.drawRoundRect(x, y, cellW, cellH, 10, getColorVariation(pri, 12, -1));
            }
        }
        drawPdaFooter(index);
    } else if (s_pdaLastIndex != index) {
        int x, y;
        if (s_pdaLastIndex >= 0 && s_pdaLastIndex < PDA_CH_COUNT) {
            pdaTileXY(s_pdaLastIndex, marginX, gap, cellW, cellH, top, x, y);
            drawChannelTile(x, y, cellW, cellH, s_pdaLastIndex, false);
        }
        if (index >= 0 && index < PDA_CH_COUNT) {
            pdaTileXY(index, marginX, gap, cellW, cellH, top, x, y);
            drawChannelTile(x, y, cellW, cellH, index, true);
        }
        drawPdaFooter(index);
    }

    s_pdaLastIndex = index;
}

#ifdef HAS_KEYBOARD
static void pdaBindKeys() {
    displayInfo("Press key 1-8 to rebind", true);
    int keyNum = -1;
    unsigned long start = millis();
    while (millis() - start < 8000 && !forceHome) {
        if (check(EscPress)) return;
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            for (char c : key.word) {
                if (c >= '1' && c <= '8') {
                    keyNum = c - '0';
                    break;
                }
            }
            if (keyNum >= 1) break;
        }
        delay(20);
    }
    if (keyNum < 1 || forceHome) return;

    std::vector<Option> opts;
    for (int i = 0; i < PDA_CH_COUNT; i++) {
        opts.push_back({kChannels[i].label, [keyNum, i]() { kvxConfig.setPdaKeyBind(keyNum, (uint8_t)i); }});
    }
    opts.push_back({"Cancel", []() {}});
    String title = String("Key ") + String(keyNum) + " opens";
    loopOptions(opts, MENU_TYPE_SUBMENU, title.c_str());
    invalidatePdaHubCache();
}
#endif

} // namespace

void pdaMenu() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    pdaEnsureDirs(fs);

    int index = 0;
    bool redraw = true;
    invalidatePdaHubCache();

    while (true) {
        if (returnToMenu || forceHome) break;

        if (pdaAlarmsPoll()) {
            invalidatePdaHubCache();
            redraw = true;
        }

        if (redraw) {
            drawPdaHub(index);
            redraw = false;
        }

        if (check(EscPress)) break;

#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            for (char raw : key.word) {
                unsigned char c = (unsigned char)raw;
                if (c == 0xDA) {
                    check(UpPress);
                    int row = 0, col = 0;
                    slotRowCol(index, row, col);
                    int next = indexFromRowCol(row - 1, col);
                    if (next >= 0) {
                        index = next;
                        redraw = true;
                    }
                } else if (c == 0xD9) {
                    check(DownPress);
                    int row = 0, col = 0;
                    slotRowCol(index, row, col);
                    int next = indexFromRowCol(row + 1, col);
                    if (next >= 0) {
                        index = next;
                        redraw = true;
                    }
                } else if (c == 0xD8) {
                    check(PrevPress);
                    int row = 0, col = 0;
                    slotRowCol(index, row, col);
                    int next = indexFromRowCol(row, col - 1);
                    if (next >= 0) {
                        index = next;
                        redraw = true;
                    }
                } else if (c == 0xD7) {
                    check(NextPress);
                    int row = 0, col = 0;
                    slotRowCol(index, row, col);
                    int next = indexFromRowCol(row, col + 1);
                    if (next >= 0) {
                        index = next;
                        redraw = true;
                    }
                } else if (c >= '1' && c <= '8') {
                    int ch = kvxConfig.pdaKeyBind[c - '1'];
                    if (ch < 0 || ch >= PDA_CH_COUNT) ch = c - '1';
                    index = ch;
                    invalidatePdaHubCache();
                    kChannels[ch].open();
                    invalidatePdaHubCache();
                    redraw = true;
                    if (returnToMenu || forceHome) break;
                } else if (c == '9') {
                    pdaBindKeys();
                    invalidatePdaHubCache();
                    redraw = true;
                    if (returnToMenu || forceHome) break;
                }
            }
            if (returnToMenu || forceHome) break;
            if (key.enter) {
                check(SelPress);
                invalidatePdaHubCache();
                kChannels[index].open();
                invalidatePdaHubCache();
                redraw = true;
                if (returnToMenu || forceHome) break;
                continue;
            }
        }
#endif

        if (check(UpPress)) {
            int row = 0, col = 0;
            slotRowCol(index, row, col);
            int next = indexFromRowCol(row - 1, col);
            if (next >= 0) {
                index = next;
                redraw = true;
            }
        } else if (check(DownPress)) {
            int row = 0, col = 0;
            slotRowCol(index, row, col);
            int next = indexFromRowCol(row + 1, col);
            if (next >= 0) {
                index = next;
                redraw = true;
            }
        } else if (check(PrevPress)) {
            int row = 0, col = 0;
            slotRowCol(index, row, col);
            int next = indexFromRowCol(row, col - 1);
            if (next >= 0) {
                index = next;
                redraw = true;
            }
        } else if (check(NextPress)) {
            int row = 0, col = 0;
            slotRowCol(index, row, col);
            int next = indexFromRowCol(row, col + 1);
            if (next >= 0) {
                index = next;
                redraw = true;
            }
        } else if (check(SelPress)) {
            invalidatePdaHubCache();
            kChannels[index].open();
            invalidatePdaHubCache();
            redraw = true;
            if (returnToMenu || forceHome) break;
        }

        delay(20);
    }

    tft.fillScreen(kvxConfig.bgColor);
}

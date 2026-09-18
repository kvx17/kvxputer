#include "hid_remote_ui.h"
#include "hid_remote_transport.h"
#include "root/input/mykeyboard.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/ui/theme.h"
#include "root/hal/led_control.h"
#include <cstring>
#include <globals.h>

static const uint16_t KVX_PURPLE = DEFAULT_PRICOLOR;
static const uint16_t KVX_GREEN = DEFAULT_SECCOLOR;
static const uint16_t KVX_ORANGE = 0xFD20;
static const uint16_t KVX_BG = KVX_DEFAULT_BGCOLOR;

static const int KVX_HEADER_H = 26;
static const int KVX_FOOTER_H = 18;

HidVertDisplayScope::HidVertDisplayScope() {
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
    tft.fillScreen(KVX_BG);
    _active = true;
#endif
}

HidVertDisplayScope::~HidVertDisplayScope() {
#if defined(HAS_SCREEN)
    if (!_active) return;
    tft.setRotation(_savedRot);
    tft.setRotation(_savedRot);
    tftWidth = tft.width();
    tftHeight = tft.height();
#endif
}

void hidRemoteDrawHeader(HidRemoteTransport transport, bool connected, const char *modeLabel) {
    hidRemoteLedTick();
    tft.fillRect(0, 0, tftWidth, KVX_HEADER_H, KVX_BG);
    tft.setTextSize(FP);
    tft.setTextColor(KVX_PURPLE, KVX_BG);
    const char *leftTitle = KVXKEYBOARD_HID_NAME;
    if (connected) {
        gHidRemoteSession.refreshHostLabel();
        const String &host = gHidRemoteSession.getHostLabel();
        if (host.length() > 0) leftTitle = host.c_str();
    }
    int maxChars = (tftWidth - 40) / (LW * FP);
    if (maxChars < 4) maxChars = 4;
    String titleLeft = String(leftTitle).substring(0, maxChars);
    tft.drawString(titleLeft, 6, 4);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.drawRightString(transport == HID_REMOTE_USB ? "USB" : "BLE", tftWidth - 18, 4, 1);
    if (connected) tft.fillCircle(tftWidth - 8, 10, 3, KVX_GREEN);
    else tft.drawCircle(tftWidth - 8, 10, 3, KVX_PURPLE);
    if (modeLabel != nullptr && modeLabel[0] != '\0') {
        tft.setTextColor(KVX_GREEN, KVX_BG);
        tft.drawString(modeLabel, 6, 14);
    }
    tft.drawFastHLine(0, KVX_HEADER_H, tftWidth, KVX_PURPLE);
}

void hidRemoteDrawFooter(const char *hints) {
    const int y = tftHeight - KVX_FOOTER_H;
    tft.fillRect(0, y, tftWidth, KVX_FOOTER_H, KVX_BG);
    tft.drawFastHLine(0, y, tftWidth, KVX_PURPLE);
    tft.setTextSize(1);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    if (hints == nullptr) hints = "fn+Ok back";
    tft.drawCentreString(hints, tftWidth / 2, y + 4, 1);
}

void hidRemoteDrawStatus(const char *line1, const char *line2) {
    tft.fillRect(0, KVX_HEADER_H, tftWidth, tftHeight - KVX_HEADER_H - KVX_FOOTER_H, KVX_BG);
    tft.setTextSize(FM);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    if (line1 != nullptr) tft.drawCentreString(line1, tftWidth / 2, tftHeight / 2 - 12, 1);
    if (line2 != nullptr) tft.drawCentreString(line2, tftWidth / 2, tftHeight / 2 + 8, 1);
}

void hidClearContentArea() {
    tft.fillRect(0, KVX_HEADER_H + 1, tftWidth, tftHeight - KVX_HEADER_H - KVX_FOOTER_H - 2, KVX_BG);
}

static void hidContentBounds(int &top, int &bottom) {
    top = KVX_HEADER_H + 2;
    bottom = tftHeight - KVX_FOOTER_H - 2;
}

static int hidTextW(const char *s) {
    if (s == nullptr || s[0] == '\0') return 0;
    return (int)strlen(s) * 6;
}

void hidDrawKeyBtn(int x, int y, int w, int h, const char *keyLabel, const char *desc, bool highlight) {
    if (w < 8 || h < 10) return;
    const uint16_t fill = KVX_PURPLE;
    const uint16_t border = highlight ? KVX_GREEN : KVX_ORANGE;
    tft.fillRoundRect(x, y, w, h, 3, fill);
    tft.drawRoundRect(x, y, w, h, 3, border);
    if (highlight) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 2, KVX_GREEN);
    tft.setTextSize(1);

    const bool hasDesc = desc != nullptr && desc[0] != '\0';
    const bool twoLine = hasDesc && h >= 22 && hidTextW(keyLabel) <= w - 4 && hidTextW(desc) <= w - 4;
    const int textY1 = twoLine ? y + 2 : y + (h - 8) / 2;
    const int textY2 = y + h - 10;

    if (twoLine) {
        tft.setTextColor(KVX_GREEN, fill);
        tft.drawCentreString(keyLabel, x + w / 2, textY1, 1);
        tft.setTextColor(KVX_ORANGE, fill);
        tft.drawCentreString(desc, x + w / 2, textY2, 1);
        return;
    }

    // One line: green key, orange label beside it when both fit
    int keyW = hidTextW(keyLabel);
    int descW = hasDesc ? hidTextW(desc) : 0;
    int gap = hasDesc ? 4 : 0;
    if (hasDesc && keyW + gap + descW <= w - 4) {
        int tx = x + (w - (keyW + gap + descW)) / 2;
        tft.setTextColor(KVX_GREEN, fill);
        tft.drawString(keyLabel, tx, textY1);
        tft.setTextColor(KVX_ORANGE, fill);
        tft.drawString(desc, tx + keyW + gap, textY1);
    } else {
        tft.setTextColor(KVX_GREEN, fill);
        tft.drawCentreString(keyLabel, x + w / 2, textY1, 1);
    }
}

static void drawArrowGlyph(int cx, int cy, int dir, uint16_t color, int s = 7) {
    // dir: 0=up 1=down 2=left 3=right
    switch (dir) {
        case 0: tft.fillTriangle(cx, cy - s, cx - s + 1, cy + s / 2, cx + s - 1, cy + s / 2, color); break;
        case 1: tft.fillTriangle(cx, cy + s, cx - s + 1, cy - s / 2, cx + s - 1, cy - s / 2, color); break;
        case 2: tft.fillTriangle(cx - s, cy, cx + s / 2, cy - s + 1, cx + s / 2, cy + s - 1, color); break;
        default: tft.fillTriangle(cx + s, cy, cx - s / 2, cy - s + 1, cx - s / 2, cy + s - 1, color); break;
    }
}

static void hidDrawMediaIcon(int cx, int cy, int kind, uint16_t color);

void hidDrawPresenterPad(bool portrait, int flashId) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);

    const int margin = 4;
    const int contentH = contentBottom - contentTop;
    int rowH = contentH >= 90 ? 22 : 16;
    if (rowH > contentH / 5) rowH = contentH / 5;
    if (rowH < 14) rowH = 14;
    const int rowY = contentBottom - rowH;
    const int colW = (tftWidth - margin * 2 - 4) / 5;
    hidDrawKeyBtn(margin, rowY, colW, rowH, "SPC", "Space", flashId == 4);
    hidDrawKeyBtn(margin + colW + 1, rowY, colW, rowH, "[", "PgUp", flashId == 5);
    hidDrawKeyBtn(margin + (colW + 1) * 2, rowY, colW, rowH, "]", "PgDn", flashId == 6);
    hidDrawKeyBtn(margin + (colW + 1) * 3, rowY, colW, rowH, "h", "Home", flashId == 7);
    hidDrawKeyBtn(margin + (colW + 1) * 4, rowY, colW, rowH, "e", "End", flashId == 8);

    const int arrowTop = contentTop + 2;
    const int arrowBottom = rowY - 4;
    const int arrowH = arrowBottom - arrowTop;
    if (arrowH < 28) return;

    const int cx = tftWidth / 2;
    const int cy = arrowTop + arrowH / 2;
    int gap = arrowH / 3;
    if (gap < 18) gap = 18;
    if (gap > 30) gap = 30;
    int bw = gap + 2;
    int bh = gap + 4;
    if (bw > tftWidth / 3) bw = tftWidth / 3;
    if (bh > arrowH / 2) bh = arrowH / 2;
    if (bh < 20) bh = 20;

    int pW = 28;
    int pH = 22;
    if (pW > bw + 4) pW = bw + 4;
    int minGap = bw / 2 + pW / 2 + 2;
    if (gap < minGap) gap = minGap;

    const int ux = cx, uy = cy - gap;
    const int lx = cx - gap, ly = cy;
    const int dx = cx, dy = cy + gap;
    const int rx = cx + gap, ry = cy;

    auto btn = [&](int id, int x, int y, int arrowDir, const char *keyHint) {
        bool hi = (flashId == id);
        int bx = x - bw / 2;
        int by = y - bh / 2;
        if (bx < margin) bx = margin;
        if (bx + bw > tftWidth - margin) bx = tftWidth - margin - bw;
        if (by < arrowTop) by = arrowTop;
        if (by + bh > arrowBottom) by = arrowBottom - bh;
        hidDrawKeyBtn(bx, by, bw, bh, "", nullptr, hi);
        const uint16_t iconC = hi ? KVX_GREEN : KVX_ORANGE;
        drawArrowGlyph(bx + bw / 2, by + (bh - 10) / 2, arrowDir, iconC, 5);
        tft.setTextSize(1);
        tft.setTextColor(KVX_GREEN, KVX_PURPLE);
        tft.drawCentreString(keyHint, bx + bw / 2, by + bh - 10, 1);
    };

    if (portrait) {
        btn(0, ux, uy, 0, "/");
        btn(1, dx, dy, 1, ",");
        btn(2, lx, ly, 2, ";");
        btn(3, rx, ry, 3, ".");
    } else {
        btn(0, ux, uy, 0, ";");
        btn(1, dx, dy, 1, ".");
        btn(2, lx, ly, 2, ",");
        btn(3, rx, ry, 3, "/");
    }

    int pbx = cx - pW / 2;
    int pby = cy - pH / 2;
    if (pby < arrowTop) pby = arrowTop;
    if (pby + pH > arrowBottom) pby = arrowBottom - pH;
    hidDrawKeyBtn(pbx, pby, pW, pH, "", nullptr, flashId == 9);
    hidDrawMediaIcon(cx - 8, cy, 1, flashId == 9 ? KVX_GREEN : KVX_ORANGE);
    tft.setTextSize(1);
    tft.setTextColor(KVX_GREEN, KVX_PURPLE);
    tft.drawString("P", cx + 2, cy - 4);
}

static void hidDrawMediaIcon(int cx, int cy, int kind, uint16_t color) {
    // kind: 1 play, 2 pause, 3 vol+, 4 vol-, 5 prev, 6 next
    switch (kind) {
        case 1: tft.fillTriangle(cx - 4, cy - 5, cx - 4, cy + 5, cx + 5, cy, color); break;
        case 2:
            tft.fillRect(cx - 4, cy - 5, 3, 10, color);
            tft.fillRect(cx + 1, cy - 5, 3, 10, color);
            break;
        case 3: tft.fillTriangle(cx, cy - 5, cx - 5, cy + 4, cx + 5, cy + 4, color); break;
        case 4: tft.fillTriangle(cx, cy + 5, cx - 5, cy - 4, cx + 5, cy - 4, color); break;
        case 5: tft.fillTriangle(cx - 5, cy, cx + 4, cy - 5, cx + 4, cy + 5, color); break;
        case 6: tft.fillTriangle(cx + 5, cy, cx - 4, cy - 5, cx - 4, cy + 5, color); break;
        default: break;
    }
}

void hidDrawMediaPad(int flashId) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int margin = 3;
    const int cols = tftWidth >= 200 ? 4 : 3;
    const int rows = 4;
    const int hgap = 2;
    const int vgap = 2;
    int colW = (tftWidth - margin * 2 - hgap * (cols - 1)) / cols;
    int rowH = (contentBottom - contentTop - vgap * (rows - 1)) / rows;
    if (rowH < 16) rowH = 16;
    if (contentTop + rows * (rowH + vgap) - vgap > contentBottom) {
        rowH = (contentBottom - contentTop - vgap * (rows - 1)) / rows;
    }

    struct MediaCell {
        const char *key;
        const char *effect;
        int icon;
        int id;
    };
    static const MediaCell cells[] = {
        {"SPC", "Play", 1, 13}, {";", "Vol+", 3, 0}, {".", "Vol-", 4, 1}, {"m", "Mute", 0, 2},
        {",", "Prev", 5, 3},    {"/", "Next", 6, 4}, {"s", "Stop", 0, 5}, {"c", "Mic", 0, 6},
        {"v", "Mix", 0, 7},     {"a", "Act", 0, 8},  {"D", "Disp", 0, 9}, {"`", "Desk", 0, 10},
        {"b", "-5s", 0, 11},    {"n", "+5s", 0, 12},
    };

    for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
        const int col = i % cols;
        const int row = i / cols;
        if (row >= rows) break;
        const int x = margin + col * (colW + hgap);
        const int y = contentTop + row * (rowH + vgap);
        if (y + rowH > contentBottom) break;

        const bool hi = flashId == cells[i].id;
        hidDrawKeyBtn(x, y, colW, rowH, "", nullptr, hi);
        const uint16_t fill = KVX_PURPLE;
        const int midY = y + (rowH - 8) / 2;
        const int keyW = hidTextW(cells[i].key);
        const int effectW = hidTextW(cells[i].effect);
        int iconW = (cells[i].icon != 0 && colW >= 40) ? 12 : 0;
        if (3 + iconW + keyW + 4 + effectW > colW - 2) iconW = 0;
        int tx = x + 3;
        if (iconW) {
            hidDrawMediaIcon(tx + 5, y + rowH / 2, cells[i].icon, hi ? KVX_GREEN : KVX_ORANGE);
            tx += iconW;
        }
        tft.setTextSize(1);
        tft.setTextColor(KVX_GREEN, fill);
        tft.drawString(cells[i].key, tx, midY);
        tx += keyW + 4;
        if (tx + effectW <= x + colW - 2) {
            tft.setTextColor(KVX_ORANGE, fill);
            tft.drawString(cells[i].effect, tx, midY);
        }
    }
}

void hidDrawMousePad(int flashId, bool joystickPresent) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int hintH = 12;
    const int padBottom = contentBottom - hintH;
    const int cx = tftWidth / 2;
    const int cy = contentTop + (padBottom - contentTop) / 2;
    int gap = (padBottom - contentTop) / 4;
    if (gap < 22) gap = 22;
    if (gap > 32) gap = 32;
    const int bw = 28;
    const int bh = 22;

    auto btn = [&](int id, int x, int y, const char *label) {
        int bx = x - bw / 2;
        int by = y - bh / 2;
        if (bx < 4) bx = 4;
        if (bx + bw > tftWidth - 4) bx = tftWidth - 4 - bw;
        if (by < contentTop) by = contentTop;
        if (by + bh > padBottom) by = padBottom - bh;
        hidDrawKeyBtn(bx, by, bw, bh, label, nullptr, flashId == id);
    };

    btn(0, cx, cy - gap, ";");
    btn(1, cx, cy + gap, ".");
    btn(2, cx - gap, cy, ",");
    btn(3, cx + gap, cy, "/");
    btn(4, 18 + bw / 2, padBottom - bh / 2, "L");
    btn(5, tftWidth - 18 - bw / 2, padBottom - bh / 2, "'");
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.setTextSize(1);
    tft.drawCentreString(joystickPresent ? "click=L  hold=R  D=invert Y" : "scroll=wheel", tftWidth / 2,
                         padBottom + 1, 1);
}

void hidDrawKeyboardFnPad(int flashKey) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int margin = 2;
    const int cols = 6;
    const int rows = 5;
    const int hgap = 2;
    const int vgap = 2;
    int colW = (tftWidth - margin * 2 - hgap * (cols - 1)) / cols;
    int rowH = (contentBottom - contentTop - vgap * (rows - 1)) / rows;
    if (rowH > 22) rowH = 22;
    if (rowH < 14) rowH = 14;

    struct FnCell {
        const char *key;
        const char *dest;
        int ch;
    };
    static const FnCell cells[] = {
        {"1", "F1", '1'},     {"2", "F2", '2'},     {"3", "F3", '3'},     {"4", "F4", '4'},
        {"5", "F5", '5'},     {"6", "F6", '6'},     {"7", "F7", '7'},     {"8", "F8", '8'},
        {"9", "F9", '9'},     {"0", "F10", '0'},    {"-", "F11", '-'},    {"=", "F12", '='},
        {"i", "Ins", 'i'},    {"p", "Prt", 'p'},    {"u", "Brk", 'u'},    {"h", "Home", 'h'},
        {"e", "End", 'e'},    {"[", "PgUp", '['},   {"]", "PgDn", ']'},   {"`", "Esc", '`'},
        {"n", "Num", 'n'},    {"s", "Scr", 's'},    {"m", "App", 'm'},    {"l", "Del", 'l'},
    };

    const size_t nCells = sizeof(cells) / sizeof(cells[0]);
    for (size_t i = 0; i < nCells; i++) {
        const int col = i % cols;
        const int row = i / cols;
        if (row >= rows) break;
        const int x = margin + col * (colW + hgap);
        const int y = contentTop + row * (rowH + vgap);
        if (y + rowH > contentBottom) break;
        hidDrawKeyBtn(x, y, colW, rowH, cells[i].key, cells[i].dest, flashKey == cells[i].ch);
    }

    const int comboY = contentTop + 4 * (rowH + vgap);
    if (comboY + rowH <= contentBottom) {
        struct ComboCell {
            const char *key;
            const char *dest;
            int ch;
        };
        static const ComboCell combos[] = {
            {"t", "Alt+Tab", 't'},
            {"w", "Win+Tab", 'w'},
            {"x", "C+S+Esc", 'x'},
            {"d", "C+A+Del", 'd'},
        };
        const int ccols = 4;
        const int cW = (tftWidth - margin * 2 - hgap * (ccols - 1)) / ccols;
        for (int i = 0; i < ccols; i++) {
            const int x = margin + i * (cW + hgap);
            hidDrawKeyBtn(x, comboY, cW, rowH, combos[i].key, combos[i].dest, flashKey == combos[i].ch);
        }
    }
}

static void hidDrawKeyChar(char c, char *buf, size_t n) {
    if (n < 2) return;
    if (c == ' ') {
        strncpy(buf, "SPC", n);
        buf[n - 1] = 0;
        return;
    }
    if (c >= 32 && c <= 126) {
        buf[0] = c;
        buf[1] = 0;
        return;
    }
    strncpy(buf, "?", n);
    buf[n - 1] = 0;
}

void hidDrawShortsPad(char upKey, char downKey, int flashId, const char *prompt) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int margin = 4;
    char upBuf[8], dnBuf[8];
    hidDrawKeyChar(upKey, upBuf, sizeof(upBuf));
    hidDrawKeyChar(downKey, dnBuf, sizeof(dnBuf));

    const int colW = (tftWidth - margin * 2 - 4) / 3;
    const int rowH = 28;
    const int y0 = contentTop + 4;
    hidDrawKeyBtn(margin, y0, colW, rowH, upBuf, "Up", flashId == 0);
    hidDrawKeyBtn(margin + colW + 2, y0, colW, rowH, "SPC", "Play", flashId == 2);
    hidDrawKeyBtn(margin + (colW + 2) * 2, y0, colW, rowH, dnBuf, "Down", flashId == 1);

    const int y1 = y0 + rowH + 6;
    hidDrawKeyBtn(margin, y1, tftWidth - margin * 2, 22, "Ok", "Set up/down keys", flashId == 3);

    tft.setTextSize(1);
    tft.setTextColor(KVX_ORANGE, KVX_BG);
    const char *hint = prompt != nullptr ? prompt : "Ok = remap keys";
    tft.drawCentreString(hint, tftWidth / 2, y1 + 26, 1);
}

void hidDrawPttPad(bool talking) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int cx = tftWidth / 2;
    const int cy = contentTop + (contentBottom - contentTop) / 2 - 4;
    const uint16_t micC = talking ? KVX_GREEN : 0xF800;

    tft.fillRoundRect(cx - 7, cy - 18, 14, 20, 7, micC);
    tft.drawLine(cx - 12, cy - 4, cx - 12, cy + 6, micC);
    tft.drawLine(cx + 12, cy - 4, cx + 12, cy + 6, micC);
    tft.drawLine(cx - 12, cy + 6, cx + 12, cy + 6, micC);
    tft.drawLine(cx, cy + 6, cx, cy + 14, micC);
    tft.drawLine(cx - 8, cy + 14, cx + 8, cy + 14, micC);
    if (!talking) {
        tft.drawLine(cx - 14, cy + 16, cx + 14, cy - 20, micC);
        tft.drawLine(cx - 14, cy + 17, cx + 14, cy - 19, micC);
    }

    tft.setTextSize(1);
    tft.setTextColor(talking ? KVX_GREEN : 0xF800, KVX_BG);
    tft.drawCentreString(talking ? "TALKING" : "MUTED", cx, cy + 22, 1);
    tft.setTextColor(KVX_ORANGE, KVX_BG);
    tft.drawCentreString("Hold Space", cx, cy + 34, 1);
}

void hidRemoteDrawHostSlots(HidRemoteTransport transport, bool connected) {
    // Content-only refresh (no full fillScreen) to avoid flicker while idle.
    hidRemoteDrawHeader(transport, connected, "Host slots");

    int top = 0;
    int bottom = 0;
    hidContentBounds(top, bottom);
    tft.fillRect(0, top, tftWidth, bottom - top, KVX_BG);

    const int slotCount = KvxputerConfig::HID_REMOTE_HOST_SLOT_COUNT;
    const int cols = 2;
    const int rows = (slotCount + cols - 1) / cols;
    const int gap = 4;
    const int padX = 6;
    const int areaH = bottom - top;
    const int rowH = (areaH - gap * (rows - 1)) / rows;
    const int colW = (tftWidth - padX * 2 - gap) / cols;

    for (int i = 0; i < slotCount; i++) {
        const int col = i % cols;
        const int row = i / cols;
        const int x = padX + col * (colW + gap);
        const int y = top + row * (rowH + gap);
        const int slot = i + 1;
        String addr = kvxConfig.getHidRemoteHostSlot(slot);

        uint16_t border = 0xF800; // red = empty
        String label = String(slot) + " —";
        if (addr.length()) {
            const bool isLive = connected && gHidRemoteSession.isConnectedToAddr(addr);
            border = isLive ? KVX_GREEN : KVX_ORANGE;
            label = String(slot) + " " + gHidRemoteSession.displayNameForAddr(addr);
        }

        tft.fillRoundRect(x, y, colW, rowH, 3, 0x1082);
        tft.drawRoundRect(x, y, colW, rowH, 3, border);
        tft.fillCircle(x + 8, y + rowH / 2, 3, border);

        tft.setTextSize(1);
        tft.setTextColor(border, 0x1082);
        int maxChars = (colW - 18) / 6;
        if (maxChars < 4) maxChars = 4;
        String shown = label.substring(0, maxChars);
        tft.drawString(shown, x + 14, y + (rowH - 8) / 2);
    }

    hidRemoteDrawFooter("1-6 tap=connect  hold 2s=options  S  Ok  ESC");
}

int hidRemotePickFromList(const char *title, const std::vector<String> &labels, int startIndex) {
    std::vector<Option> options;
    for (const auto &label : labels) options.push_back({label, []() {}});
    if (startIndex < 0 || startIndex >= (int)labels.size()) startIndex = 0;
    return loopOptions(options, MENU_TYPE_SUBMENU, title, startIndex, false);
}

bool hidRemoteWaitBack() {
    while (!check(EscPress)) {
        hidRemoteLedTick();
        delay(20);
    }
    return true;
}

static HidRemoteLedMode hidLedMode = HID_REMOTE_LED_OFF;
static HidRemoteLedMode hidLedAfterFlash = HID_REMOTE_LED_CONNECTING;
static unsigned long hidLedFlashUntil = 0;
static bool hidLedOn = false;
static unsigned long hidLedLastToggle = 0;
static bool hidLedHeld = false;

static bool hidLedAllowed() {
    if (isScreenOff) return false;
#ifdef HAS_RGB_LED
    if (kvxConfig.ledBright <= 0) return false;
#endif
    if (!kvxConfig.hidRemoteLedEnabled) return false;
    return true;
}

static void hidLedPaint(uint8_t r, uint8_t g, uint8_t b, bool dim) {
    if (!hidLedAllowed()) {
        ledShowApp(0, 0, 0, 0);
        return;
    }
    const int cap = dim ? 20 : 100;
#ifdef HAS_RGB_LED
    uint8_t bright = (uint8_t)(255 * kvxConfig.ledBright * cap / 10000);
    if (dim && bright < 8 && kvxConfig.ledBright > 0) bright = 8;
#else
    uint8_t bright = (uint8_t)(255 * cap / 100);
    if (dim && bright < 8) bright = 8;
#endif
    ledShowApp(r, g, b, bright);
}

void hidRemoteLedBegin() {
    hidLedHeld = true;
#ifdef HAS_RGB_LED
    // Drop the LED effect task so BLE can claim a contiguous DMA block.
    ledEffects(false);
#endif
    ledSuppressStatus(true);
    hidLedMode = HID_REMOTE_LED_OFF;
    hidLedFlashUntil = 0;
    hidLedPaint(0, 0, 0, false);
}

void hidRemoteLedEnd() {
    hidLedMode = HID_REMOTE_LED_OFF;
    hidLedFlashUntil = 0;
    hidLedPaint(0, 0, 0, false);
    hidLedHeld = false;
    ledSuppressStatus(false);
    ledSetStatus(LED_STATUS_IDLE);
}

void hidRemoteLedSet(HidRemoteLedMode mode) {
    if (mode == HID_REMOTE_LED_REJECT) {
        hidLedAfterFlash = (hidLedMode == HID_REMOTE_LED_PAIRING) ? HID_REMOTE_LED_PAIRING
                                                                 : HID_REMOTE_LED_CONNECTING;
        hidLedMode = HID_REMOTE_LED_REJECT;
        hidLedFlashUntil = millis() + 180;
        hidLedPaint(255, 0, 0, false);
        return;
    }
    if (mode == HID_REMOTE_LED_FORGET_OK) {
        hidLedAfterFlash = HID_REMOTE_LED_OFF;
        hidLedMode = HID_REMOTE_LED_FORGET_OK;
        hidLedFlashUntil = millis() + 220;
        hidLedPaint(0, 255, 0, false);
        return;
    }
    if (mode == HID_REMOTE_LED_ERROR) {
        for (int i = 0; i < 3; i++) {
            hidLedPaint(255, 0, 0, false);
            delay(120);
            hidLedPaint(0, 0, 0, false);
            delay(120);
        }
        hidLedMode = HID_REMOTE_LED_OFF;
        hidLedFlashUntil = 0;
        return;
    }
    hidLedMode = mode;
    hidLedFlashUntil = 0;
    hidLedLastToggle = millis();
    hidLedOn = true;
    hidRemoteLedTick();
}

void hidRemoteLedTick() {
    if (!hidLedHeld) return;
    if (!hidLedAllowed()) {
        ledShowApp(0, 0, 0, 0);
        return;
    }

    const unsigned long now = millis();
    if (hidLedFlashUntil != 0 && now >= hidLedFlashUntil) {
        hidLedFlashUntil = 0;
        hidLedMode = hidLedAfterFlash;
        hidLedLastToggle = now;
        hidLedOn = true;
    }

    switch (hidLedMode) {
        case HID_REMOTE_LED_CONNECTING: {
            const unsigned long period = 700;
            if (now - hidLedLastToggle >= period) {
                hidLedLastToggle = now;
                hidLedOn = !hidLedOn;
            }
            hidLedPaint(0, 0, hidLedOn ? 255 : 0, false);
            break;
        }
        case HID_REMOTE_LED_PAIRING: {
            const unsigned long period = 280;
            if (now - hidLedLastToggle >= period) {
                hidLedLastToggle = now;
                hidLedOn = !hidLedOn;
            }
            hidLedPaint(0, 0, hidLedOn ? 255 : 0, false);
            break;
        }
        case HID_REMOTE_LED_HANDSHAKE: {
            // Cyan fast blink: host is on the air, link not HID-ready yet.
            const unsigned long period = 180;
            if (now - hidLedLastToggle >= period) {
                hidLedLastToggle = now;
                hidLedOn = !hidLedOn;
            }
            hidLedPaint(0, hidLedOn ? 200 : 0, hidLedOn ? 255 : 40, false);
            break;
        }
        case HID_REMOTE_LED_CONNECTED:
            hidLedPaint(0, 0, 255, true);
            break;
        case HID_REMOTE_LED_DISCONNECTED:
        case HID_REMOTE_LED_REJECT:
            hidLedPaint(255, 0, 0, false);
            break;
        case HID_REMOTE_LED_FORGET_OK:
            hidLedPaint(0, 255, 0, false);
            break;
        case HID_REMOTE_LED_ERROR:
        case HID_REMOTE_LED_OFF:
        default:
            hidLedPaint(0, 0, 0, false);
            break;
    }
}

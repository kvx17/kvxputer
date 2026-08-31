#include "hid_remote_ui.h"
#include "root/input/mykeyboard.h"
#include "root/config/configPins.h"
#include <globals.h>

static const uint16_t KVX_PURPLE = 0x9818;
static const uint16_t KVX_GREEN = 0x07E0;
static const uint16_t KVX_BG = 0x0841;
static const uint16_t KVX_BTN = 0x600C;
static const uint16_t KVX_BTN_HI = 0x9818;

static const int KVX_HEADER_H = 26;
static const int KVX_FOOTER_H = 18;

HidVertDisplayScope::HidVertDisplayScope() {
#if defined(HAS_SCREEN)
    _savedRot = kvxConfigPins.rotation;
#ifndef ROTATION
#define ROTATION 1
#endif
    const int vertRot = _savedRot > 0 ? _savedRot - 1 : 3;
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
    tft.fillRect(0, 0, tftWidth, KVX_HEADER_H, KVX_BG);
    tft.setTextSize(FP);
    tft.setTextColor(KVX_PURPLE, KVX_BG);
    tft.drawString("HID Remote", 6, 4);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.drawRightString(transport == HID_REMOTE_USB ? "USB" : "BLE", tftWidth - 18, 4, 1);
    if (connected) tft.fillCircle(tftWidth - 8, 10, 3, KVX_GREEN);
    else tft.drawCircle(tftWidth - 8, 10, 3, TFT_DARKGREY);
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

void hidDrawKeyBtn(int x, int y, int w, int h, const char *keyLabel, const char *desc, bool highlight) {
    uint16_t fill = highlight ? KVX_BTN_HI : KVX_BTN;
    uint16_t border = highlight ? KVX_GREEN : KVX_PURPLE;
    tft.fillRoundRect(x, y, w, h, 4, fill);
    tft.drawRoundRect(x, y, w, h, 4, border);
    if (highlight) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 3, KVX_GREEN);
    tft.setTextSize(1);
    tft.setTextColor(highlight ? KVX_GREEN : KVX_PURPLE, fill);
    tft.drawCentreString(keyLabel, x + w / 2, y + 3, 1);
    if (desc != nullptr && desc[0] != '\0') {
        tft.setTextColor(KVX_GREEN, fill);
        tft.drawCentreString(desc, x + w / 2, y + h - 11, 1);
    }
}

static void drawArrowGlyph(int cx, int cy, int dir, uint16_t color) {
    // dir: 0=up 1=down 2=left 3=right
    switch (dir) {
        case 0: tft.fillTriangle(cx, cy - 8, cx - 7, cy + 4, cx + 7, cy + 4, color); break;
        case 1: tft.fillTriangle(cx, cy + 8, cx - 7, cy - 4, cx + 7, cy - 4, color); break;
        case 2: tft.fillTriangle(cx - 8, cy, cx + 4, cy - 7, cx + 4, cy + 7, color); break;
        default: tft.fillTriangle(cx + 8, cy, cx - 4, cy - 7, cx - 4, cy + 7, color); break;
    }
}

void hidDrawPresenterPad(bool portrait, int flashId) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);

    const int rowH = 16;
    const int rowY = contentBottom - rowH;
    const int margin = 4;
    const int colW = (tftWidth - margin * 2 - 4) / 5;
    hidDrawKeyBtn(margin, rowY, colW, rowH, "SPC", "Space", flashId == 4);
    hidDrawKeyBtn(margin + colW + 1, rowY, colW, rowH, "[", "PgUp", flashId == 5);
    hidDrawKeyBtn(margin + (colW + 1) * 2, rowY, colW, rowH, "]", "PgDn", flashId == 6);
    hidDrawKeyBtn(margin + (colW + 1) * 3, rowY, colW, rowH, "h", "Home", flashId == 7);
    hidDrawKeyBtn(margin + (colW + 1) * 4, rowY, colW, rowH, "e", "End", flashId == 8);

    const int arrowTop = contentTop + 2;
    const int arrowBottom = rowY - 4;
    const int arrowH = arrowBottom - arrowTop;
    const int cx = tftWidth / 2;
    const int cy = arrowTop + arrowH / 2;
    int gap = arrowH / 3;
    if (gap < 16) gap = 16;
    if (gap > 28) gap = 28;
    const int bw = gap + 4;
    const int bh = gap;

    int ux, uy, dx, dy, lx, ly, rx, ry;
    if (!portrait) {
        ux = cx;
        uy = cy - gap;
        lx = cx - gap;
        ly = cy;
        dx = cx;
        dy = cy + gap;
        rx = cx + gap;
        ry = cy;
    } else {
        ux = cx;
        uy = cy - gap;
        lx = cx - gap;
        ly = cy;
        dx = cx;
        dy = cy + gap;
        rx = cx + gap;
        ry = cy;
    }

    auto btn = [&](int id, int x, int y, int arrowDir, const char *keyHint) {
        bool hi = (flashId == id);
        int bx = x - bw / 2;
        int by = y - bh / 2;
        if (bx < margin) bx = margin;
        if (bx + bw > tftWidth - margin) bx = tftWidth - margin - bw;
        if (by < arrowTop) by = arrowTop;
        if (by + bh > arrowBottom) by = arrowBottom - bh;
        hidDrawKeyBtn(bx, by, bw, bh, keyHint, nullptr, hi);
        drawArrowGlyph(x, y, arrowDir, hi ? KVX_GREEN : KVX_PURPLE);
    };

    btn(0, ux, uy, 0, "fn+;");
    btn(1, dx, dy, 1, "fn+.");
    btn(2, lx, ly, 2, "fn+,");
    btn(3, rx, ry, 3, "fn+/");
}

void hidDrawMediaPad(int flashId) {
    hidClearContentArea();
    int contentTop, contentBottom;
    hidContentBounds(contentTop, contentBottom);
    const int margin = 4;
    const int cols = 4;
    const int rows = 4;
    const int hgap = 2;
    const int vgap = 2;
    const int colW = (tftWidth - margin * 2 - hgap * (cols - 1)) / cols;
    const int rowH = (contentBottom - contentTop - vgap * (rows - 1)) / rows;

    struct MediaCell {
        const char *key;
        const char *desc;
        int id;
    };
    static const MediaCell cells[] = {
        {";^", "Vol+", 0},  {".v", "Vol-", 1},  {"m", "Mute", 2},   {"s", "Stop", 5},
        {",<", "Prev", 3},  {"/>", "Next", 4},  {"c", "Mic", 6},    {"v", "Mix", 7},
        {"a", "Act", 8},    {"D", "Disp", 9},   {"`", "Desk", 10},  {"b", "-5s", 11},
        {"n", "+5s", 12},
    };

    for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
        const int col = i % cols;
        const int row = i / cols;
        const int x = margin + col * (colW + hgap);
        const int y = contentTop + row * (rowH + vgap);
        hidDrawKeyBtn(x, y, colW, rowH, cells[i].key, cells[i].desc, flashId == cells[i].id);
    }
}

void hidDrawMousePad(int flashId) {
    hidClearContentArea();
    const int cx = tftWidth / 2;
    const int cy = tftHeight / 2 - 6;
    const int gap = 32;
    const int bw = 28;
    const int bh = 24;

    auto btn = [&](int id, int x, int y, const char *label) {
        hidDrawKeyBtn(x - bw / 2, y - bh / 2, bw, bh, label, nullptr, flashId == id);
    };

    btn(0, cx, cy - gap, ";");
    btn(1, cx, cy + gap, ".");
    btn(2, cx - gap, cy, ",");
    btn(3, cx + gap, cy, "/");
    btn(4, 20, tftHeight - KVX_FOOTER_H - 30, "L");
    btn(5, tftWidth - 48, tftHeight - KVX_FOOTER_H - 30, "'");
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.setTextSize(1);
    tft.drawCentreString("scroll=wheel", tftWidth / 2, tftHeight - KVX_FOOTER_H - 14, 1);
}

int hidRemotePickFromList(const char *title, const std::vector<String> &labels, int startIndex) {
    std::vector<Option> options;
    for (const auto &label : labels) options.push_back({label, []() {}});
    if (startIndex < 0 || startIndex >= (int)labels.size()) startIndex = 0;
    return loopOptions(options, MENU_TYPE_SUBMENU, title, startIndex, false);
}

bool hidRemoteWaitBack() {
    while (!check(EscPress)) { delay(20); }
    return true;
}

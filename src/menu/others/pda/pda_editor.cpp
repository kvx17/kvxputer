#include "pda_editor.h"

#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/theme.h"
#include <cstring>
#include <globals.h>

#ifdef HAS_KEYBOARD
#include "pda_alarms.h"
#include <vector>
#endif

static const String ESC = String((char)0x1B);

#ifdef HAS_KEYBOARD

static constexpr int kMaxCap = 4096;
static constexpr unsigned long kBlinkMs = 400;

namespace {

struct VisLine {
    int start;
    int len;
};

bool isNavPunct(char c) { return c == ';' || c == ',' || c == '.' || c == '/'; }

bool isHidCmd(unsigned char c) {
    return c == 0xDA || c == 0xD9 || c == 0xD8 || c == 0xD7 || c == 0xB1 || c == 0xD4 || c == 0xB3;
}

void rebuildWrap(const String &text, int cols, std::vector<VisLine> &lines) {
    lines.clear();
    if (cols < 1) cols = 1;
    const int n = (int)text.length();
    int i = 0;
    while (true) {
        int start = i;
        int col = 0;
        while (i < n && text[i] != '\n' && col < cols) {
            i++;
            col++;
        }
        lines.push_back({start, i - start});
        if (i < n && text[i] == '\n') {
            i++;
            if (i == n) {
                lines.push_back({i, 0});
                break;
            }
        } else if (i >= n) {
            break;
        }
    }
    if (lines.empty()) lines.push_back({0, 0});
}

int lineOfCaret(const std::vector<VisLine> &lines, int caret, int textLen) {
    for (size_t i = 0; i < lines.size(); i++) {
        int start = lines[i].start;
        int nextStart = (i + 1 < lines.size()) ? lines[i + 1].start : textLen + 1;
        if (caret >= start && caret < nextStart) return (int)i;
    }
    return (int)lines.size() - 1;
}

void insertChar(String &text, int &caret, char c, int maxLen) {
    if ((int)text.length() >= maxLen) return;
    if (caret < 0) caret = 0;
    if (caret > (int)text.length()) caret = (int)text.length();
    String tail = text.substring(caret);
    text.remove(caret);
    text += c;
    text += tail;
    caret++;
}

void backspaceAt(String &text, int &caret) {
    if (caret <= 0) return;
    caret--;
    text.remove(caret, 1);
}

void forwardDeleteAt(String &text, int caret) {
    if (caret < 0 || caret >= (int)text.length()) return;
    text.remove(caret, 1);
}

void moveHoriz(int &caret, int textLen, int dx) {
    caret += dx;
    if (caret < 0) caret = 0;
    if (caret > textLen) caret = textLen;
}

void moveVert(const std::vector<VisLine> &lines, int &caret, int textLen, int dy) {
    if (lines.empty()) return;
    int line = lineOfCaret(lines, caret, textLen);
    int col = caret - lines[line].start;
    int dest = line + dy;
    if (dest < 0) dest = 0;
    if (dest >= (int)lines.size()) dest = (int)lines.size() - 1;
    if (col > lines[dest].len) col = lines[dest].len;
    caret = lines[dest].start + col;
    if (caret > textLen) caret = textLen;
}

void consumeNavFlags() {
    check(UpPress);
    check(DownPress);
    check(PrevPress);
    check(NextPress);
    check(SelPress);
    check(EscPress);
}

void drawKeyChip(int &x, int y, const char *keyLabel, const char *hint) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    // Dense chips so Ent/Fn+OK/ESC fit on 240px; body text stays FM/FP below.
    const int chipFont = uiDenseFont();
    tft.setTextSize(chipFont);
    int keyW = (int)strlen(keyLabel) * uiCharW(chipFont) + 4;
    int chipH = uiLineH(chipFont) + 2;
    tft.fillRoundRect(x, y, keyW, chipH, 2, sec);
    tft.setTextColor(bg, sec);
    tft.drawCentreString(keyLabel, x + keyW / 2, y + 1, 1);
    x += keyW + 2;
    tft.setTextColor(pri, bg);
    tft.drawString(hint, x, y + 1, 1);
    x += (int)strlen(hint) * uiCharW(chipFont) + 6;
}

void drawChrome(const char *title, bool multiline) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t bg = kvxConfig.bgColor;
    tft.fillScreen(bg);
    drawKvxTopBar(title ? title : "Edit");

    // Shortcut chips below the top bar.
    int x = 6;
    int y = KVX_TOPBAR_H + 2;
    if (multiline) {
        drawKeyChip(x, y, "Ent", "nl");
        drawKeyChip(x, y, "Fn+OK", "save");
        drawKeyChip(x, y, "ESC", "cancel");
    } else {
        drawKeyChip(x, y, "Enter", "save");
        drawKeyChip(x, y, "ESC", "cancel");
    }
    tft.drawFastHLine(4, KVX_TOPBAR_H + uiLineH(uiDenseFont()) + 6, tftWidth - 8, getColorVariation(pri, 10, -1));
    (void)bg;
}

void drawBody(
    const String &text, const std::vector<VisLine> &lines, int caret, int scroll, int maxRows, bool blinkOn,
    bool fullClear
) {
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t bg = kvxConfig.bgColor;
    const int x0 = BORDER_PAD_X;
    const int y0 = KVX_TOPBAR_H + uiLineH(uiDenseFont()) + 10;
    const int w = tftWidth - 2 * BORDER_PAD_X;
    const int rowH = FM * LH;
    const int h = maxRows * rowH;
    if (fullClear) tft.fillRect(x0, y0, w, h, bg);

    tft.setTextSize(FM);
    const int textLen = (int)text.length();
    const int caretLine = lineOfCaret(lines, caret, textLen);

    for (int r = 0; r < maxRows; r++) {
        int li = scroll + r;
        if (li < 0 || li >= (int)lines.size()) break;
        // On blink-only updates, redraw just the caret line to avoid flicker.
        if (!fullClear && li != caretLine) continue;

        const VisLine &vl = lines[li];
        int y = y0 + r * rowH;
        if (!fullClear) tft.fillRect(x0, y, w, rowH, bg);
        String chunk = text.substring(vl.start, vl.start + vl.len);

        if (li != caretLine) {
            tft.setTextColor(pri, bg);
            tft.setCursor(x0, y);
            tft.print(chunk);
            continue;
        }

        int col = caret - vl.start;
        if (col < 0) col = 0;
        if (col > vl.len) col = vl.len;

        tft.setCursor(x0, y);
        if (col > 0) {
            tft.setTextColor(pri, bg);
            tft.print(chunk.substring(0, col));
        }
        if (blinkOn) {
            if (col < vl.len) {
                tft.setTextColor(bg, pri);
                tft.print(chunk.substring(col, col + 1));
                tft.setTextColor(pri, bg);
                if (col + 1 < vl.len) tft.print(chunk.substring(col + 1));
            } else {
                tft.setTextColor(bg, pri);
                tft.print('_');
                tft.setTextColor(pri, bg);
            }
        } else {
            tft.setTextColor(pri, bg);
            tft.print(chunk.substring(col));
        }
    }
}

bool confirmDiscard() {
    int8_t choice = displayMessage("Discard unsaved changes?", "Keep", nullptr, "Discard", TFT_RED);
    return choice == 1;
}

} // namespace

static int runCaretEditor(String &text, const char *title, int maxLen, bool multiline) {
    if (maxLen < 1) maxLen = 1;
    if (maxLen > kMaxCap) maxLen = kMaxCap;
    if ((int)text.length() > maxLen) text.remove(maxLen);

    const String original = text;
    int caret = (int)text.length();

    int cols = (tftWidth - 2 * BORDER_PAD_X) / (FM * LW);
    if (cols < 8) cols = 8;
    const int y0 = KVX_TOPBAR_H + uiLineH(uiDenseFont()) + 10;
    int maxRows = (tftHeight - 2 - y0) / (FM * LH);
    if (maxRows < 1) maxRows = 1;

    std::vector<VisLine> lines;
    rebuildWrap(text, cols, lines);
    int scroll = 0;
    bool wrapDirty = true;
    bool chromeDirty = true;
    bool blinkOn = true;
    unsigned long lastBlink = millis();

    auto ensureCaretVisible = [&]() {
        int caretLine = lineOfCaret(lines, caret, (int)text.length());
        if (caretLine < scroll) scroll = caretLine;
        if (caretLine >= scroll + maxRows) scroll = caretLine - maxRows + 1;
        if (scroll < 0) scroll = 0;
    };

    // Avoid a second full-screen clear before chrome — drawChrome fills once.
    chromeDirty = true;

    for (;;) {
        if (returnToMenu || forceHome) {
            consumeNavFlags();
            text = original;
            return PDA_EDIT_CANCEL;
        }

        if (pdaAlarmsPoll()) chromeDirty = true;

        unsigned long now = millis();
        bool blinkTick = false;
        if (now - lastBlink >= kBlinkMs) {
            lastBlink = now;
            blinkOn = !blinkOn;
            blinkTick = true;
        }

        keyStroke key = _getKeyPress();

        bool hidUp = false, hidDown = false, hidLeft = false, hidRight = false;
        bool skipUp = false, skipDown = false, skipPrev = false, skipNext = false;
        bool wantCancel = false;
        bool wantSave = false;
        bool mutated = false;

        if (key.pressed) {
            if (key.del) {
                backspaceAt(text, caret);
                mutated = true;
                // Defense: Adv used to assert EscPress on Del; clear any leftover.
                EscPress = false;
            }
            if (key.enter) {
                check(SelPress);
                if (multiline && !key.fn) {
                    insertChar(text, caret, '\n', maxLen);
                    mutated = true;
                } else {
                    wantSave = true;
                }
            }
            for (char raw : key.word) {
                unsigned char c = (unsigned char)raw;
                if (c == 0xDA) {
                    hidUp = true;
                    continue;
                }
                if (c == 0xD9) {
                    hidDown = true;
                    continue;
                }
                if (c == 0xD8) {
                    hidLeft = true;
                    continue;
                }
                if (c == 0xD7) {
                    hidRight = true;
                    continue;
                }
                if (c == 0xB1 || c == '`') {
                    wantCancel = true;
                    continue;
                }
                if (c == 0xD4) {
                    forwardDeleteAt(text, caret);
                    mutated = true;
                    continue;
                }
                if (isHidCmd(c)) continue;
                if (c < 32 || c > 126) continue;
                if (!multiline && (c == '\n' || c == '\r')) continue;
                if (c == ':') skipUp = true;
                else if (c == '>') skipDown = true;
                else if (c == '<') skipPrev = true;
                else if (c == '?') skipNext = true;
                if (isNavPunct((char)c) && (UpPress || DownPress || PrevPress || NextPress)) continue;
                insertChar(text, caret, (char)c, maxLen);
                mutated = true;
            }
        }

        // Only treat Esc as cancel when Del was not the cause.
        if (!key.del && check(EscPress)) wantCancel = true;
        if (!key.enter && check(SelPress)) wantSave = true;

        if (check(UpPress)) {
            if (!hidUp && !skipUp) hidUp = true;
        }
        if (check(DownPress)) {
            if (!hidDown && !skipDown) hidDown = true;
        }
        if (check(PrevPress)) {
            if (!hidLeft && !skipPrev) hidLeft = true;
        }
        if (check(NextPress)) {
            if (!hidRight && !skipNext) hidRight = true;
        }

        if (hidUp || hidDown || hidLeft || hidRight) {
            if (wrapDirty || mutated) rebuildWrap(text, cols, lines);
            if (hidLeft) moveHoriz(caret, (int)text.length(), -1);
            if (hidRight) moveHoriz(caret, (int)text.length(), +1);
            if (hidUp) moveVert(lines, caret, (int)text.length(), -1);
            if (hidDown) moveVert(lines, caret, (int)text.length(), +1);
            mutated = true;
        }

        if (wantSave) {
            consumeNavFlags();
            return PDA_EDIT_OK;
        }

        if (wantCancel) {
            if (text != original) {
                if (!confirmDiscard()) {
                    chromeDirty = true;
                    wrapDirty = true;
                    delay(20);
                    continue;
                }
                text = original;
            }
            consumeNavFlags();
            return PDA_EDIT_CANCEL;
        }

        if (mutated) {
            rebuildWrap(text, cols, lines);
            ensureCaretVisible();
            wrapDirty = true;
            blinkOn = true;
            lastBlink = now;
        }

        if (chromeDirty) {
            drawChrome(title, multiline);
            chromeDirty = false;
            wrapDirty = true;
        }
        if (wrapDirty) {
            ensureCaretVisible();
            drawBody(text, lines, caret, scroll, maxRows, blinkOn, true);
            wrapDirty = false;
        } else if (blinkTick) {
            // Blink caret only — avoid full body fillRect flicker.
            ensureCaretVisible();
            drawBody(text, lines, caret, scroll, maxRows, blinkOn, false);
        }

        delay(20);
    }
    return PDA_EDIT_CANCEL;
}

#endif // HAS_KEYBOARD

int pdaTextEditor(String &text, const char *title, int maxLen, bool multiline) {
#ifndef HAS_KEYBOARD
    (void)multiline;
    String result = keyboard(text, maxLen, title ? title : "");
    if (result == ESC) return PDA_EDIT_CANCEL;
    text = result;
    return PDA_EDIT_OK;
#else
    (void)ESC;
    return runCaretEditor(text, title ? title : "", maxLen, multiline);
#endif
}

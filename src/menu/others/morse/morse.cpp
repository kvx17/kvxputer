#include "morse.h"

#include "menu/others/audio.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
#include "menu/others/mic.h"
#endif

#include <Arduino.h>
#include <globals.h>
#include <vector>

#if defined(HAS_KEYBOARD) && (defined(HAS_NS4168_SPKR) || defined(BUZZ_PIN))
#ifndef LITE_VERSION

namespace {

struct MorseEntry {
    char ch;
    const char *code;
};

const MorseEntry kTable[] = {
    {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},    {'E', "."},
    {'F', "..-."},   {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},
    {'K', "-.-"},    {'L', ".-.."},   {'M', "--"},     {'N', "-."},     {'O', "---"},
    {'P', ".--."},   {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
    {'U', "..-"},    {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},   {'Y', "-.--"},
    {'Z', "--.."},   {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
    {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},  {'8', "---.."},
    {'9', "----."},  {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'\'', ".----."},
    {'!', "-.-.--"}, {'/', "-..-."},  {'(', "-.--."},  {')', "-.--.-"}, {'&', ".-..."},
    {':', "---..."}, {';', "-.-.-."}, {'=', "-...-"},  {'+', ".-.-."},  {'-', "-....-"},
    {'_', "..--.-"}, {'"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."},
};

const char *charToMorse(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c == ' ') return " ";
    for (const auto &e : kTable) {
        if (e.ch == c) return e.code;
    }
    return nullptr;
}

char morseToChar(const String &code) {
    if (code == " ") return ' ';
    for (const auto &e : kTable) {
        if (code == e.code) return e.ch;
    }
    return '?';
}

unsigned ditMs() {
    uint8_t wpm = kvxConfig.morseWpm;
    if (wpm < 5) wpm = 5;
    if (wpm > 40) wpm = 40;
    return 1200u / wpm;
}

void playElement(char el) {
    unsigned d = ditMs();
    unsigned dur = (el == '-') ? 3 * d : d;
    _tone(700, dur);
    delay(d);
}

void playMorseString(const String &text) {
    unsigned d = ditMs();
    for (size_t i = 0; i < text.length(); i++) {
        if (returnToMenu || forceHome || check(EscPress)) return;
        char c = text[i];
        if (c == ' ') {
            delay(4 * d);
            continue;
        }
        const char *code = charToMorse(c);
        if (!code) continue;
        for (const char *p = code; *p; p++) {
            if (*p == '.' || *p == '-') playElement(*p);
        }
        delay(2 * d);
    }
}

String textToMorseDisplay(const String &text) {
    String out;
    for (size_t i = 0; i < text.length(); i++) {
        if (i) out += ' ';
        const char *code = charToMorse(text[i]);
        if (code) out += (*code == ' ') ? "/" : code;
        else out += '?';
    }
    return out;
}

void drainSelect() {
    resetHeldNavKeys();
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
    KeyStroke.Clear();
    delay(60);
}

int bodyTop() { return KVX_TOPBAR_H + 1; }
int bodyBot() { return uiFooterY(uiDenseFont()); }

void paintFooter(const char *hint) {
    const int fy = uiFooterY(uiDenseFont());
    tft.fillRect(0, fy - 1, tftWidth, tftHeight - (fy - 1), kvxConfig.bgColor);
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawCentreString(hint, tftWidth / 2, fy, 1);
}

void paintChrome(const char *title, const char *footer) {
    TftFrame frame;
    tft.fillScreen(kvxConfig.bgColor);
    drawKvxTopBar(title);
    paintFooter(footer);
}

void clearBody() {
    tft.fillRect(0, bodyTop(), tftWidth, bodyBot() - bodyTop(), kvxConfig.bgColor);
}

void adjustWpm() {
    if (kvxConfig.morseWpm < 5) kvxConfig.morseWpm = 20;
    int wpm = kvxConfig.morseWpm;
    int last = -1;
    drainSelect();
    paintChrome("Morse WPM", ",/ adjust  Enter=ok  Esc=back");

    bool wasLt = false, wasRt = false;
    unsigned long lastRepeat = 0;
    while (!returnToMenu && !forceHome) {
        if (wpm != last) {
            last = wpm;
            TftFrame frame;
            clearBody();
            tft.setTextSize(FM);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawCentreString(String(wpm) + " WPM", tftWidth / 2, tftHeight / 2 - 8, 1);
        }
        if (check(EscPress)) return;
        if (check(SelPress)) {
            kvxConfig.morseWpm = (uint8_t)wpm;
            kvxConfig.saveFile();
            return;
        }
        bool lt = isCardputerKeyHeld(',') || PrevPagePress;
        bool rt = isCardputerKeyHeld('/') || NextPagePress;
        if (PrevPagePress) PrevPagePress = false;
        if (NextPagePress) NextPagePress = false;
        unsigned long now = millis();
        bool doLt = (lt && !wasLt) || (lt && wasLt && now - lastRepeat > 150);
        bool doRt = (rt && !wasRt) || (rt && wasRt && now - lastRepeat > 150);
        wasLt = lt;
        wasRt = rt;
        if (doLt) {
            lastRepeat = now;
            if (wpm > 5) wpm--;
        } else if (doRt) {
            lastRepeat = now;
            if (wpm < 40) wpm++;
        } else {
            delay(25);
        }
    }
}

String physicalTextInput(const char *title, int maxLen) {
    String text;
    String lastDrawn = "!";
    drainSelect();
    paintChrome(title, "Enter=OK  Esc=cancel");
    while (!returnToMenu && !forceHome) {
        if (text != lastDrawn) {
            lastDrawn = text;
            TftFrame frame;
            clearBody();
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            int y = KVX_TOPBAR_H + 6;
            tft.drawString("Type on keyboard", 8, y);
            y += uiLineH(FP) + 4;
            tft.drawRect(6, y, tftWidth - 12, uiLineH(FP) * 2 + 8, kvxConfig.priColor);
            if (text.length() <= 34) tft.drawString(text, 10, y + 4);
            else {
                tft.drawString(text.substring(0, 34), 10, y + 4);
                tft.drawString(text.substring(34, 68), 10, y + 4 + uiLineH(FP));
            }
            paintFooter((String(text.length()) + "/" + String(maxLen) + "  Enter=OK Esc=cancel").c_str());
        }
        if (check(EscPress)) return "\x1B";
        if (check(SelPress)) {
            drainSelect();
            return text;
        }
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(25);
            continue;
        }
        if (key.del && text.length()) text.remove(text.length() - 1);
        else if (key.enter) {
            drainSelect();
            return text;
        } else {
            for (char c : key.word) {
                if (c >= 32 && c < 127 && (int)text.length() < maxLen) text += c;
            }
        }
        KeyStroke.Clear();
    }
    return "\x1B";
}

void textMode() {
    String text = physicalTextInput("Text to Morse", 120);
    if (text == "\x1B" || text.length() == 0) return;
    String display = textToMorseDisplay(text);
    paintChrome("Morse", "Playing… Esc=stop");
    {
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString(text.substring(0, 36), 8, KVX_TOPBAR_H + 6);
        String d = display;
        if (d.length() > 36) d = d.substring(0, 36);
        tft.drawString(d, 8, KVX_TOPBAR_H + 6 + uiLineH(FP) + 2);
    }
    playMorseString(text);
    paintChrome("Morse", "Enter/Esc=back");
    {
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawCentreString("Done", tftWidth / 2, tftHeight / 2 - 6, 1);
    }
    drainSelect();
    while (!returnToMenu && !forceHome) {
        if (check(EscPress) || check(SelPress)) break;
        delay(30);
    }
}

void keyMode() {
    drainSelect();
    char lastCh = 0;
    const char *lastCode = nullptr;
    char drawnCh = (char)0xFF;

    auto paint = [&]() {
        if (lastCh == drawnCh) return;
        drawnCh = lastCh;
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString("Press a key to see Morse", 8, KVX_TOPBAR_H + 6);
        if (lastCh) {
            String chShow = (lastCh == ' ') ? String("SPACE") : String(lastCh);
            tft.setTextSize(FM);
            tft.drawCentreString(chShow, tftWidth / 2, KVX_TOPBAR_H + 28, 1);
            tft.setTextSize(FP);
            tft.drawCentreString(lastCode ? lastCode : "?", tftWidth / 2, KVX_TOPBAR_H + 52, 1);
        } else {
            tft.drawString("(waiting…)", 8, KVX_TOPBAR_H + 28);
        }
    };

    paintChrome("Morse Key", "Esc=back");
    paint();

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(25);
            continue;
        }
        for (char c : key.word) {
            char look = c;
            if (look >= 'a' && look <= 'z') look = look - 'a' + 'A';
            const char *code = charToMorse(look);
            if (!code && c == ' ') {
                code = " ";
                look = ' ';
            }
            if (code) {
                lastCh = look;
                lastCode = (*code == ' ') ? "/" : code;
                drawnCh = 0;
                paint();
                if (*code && *code != ' ') {
                    String one;
                    one += look;
                    playMorseString(one);
                }
            }
        }
        KeyStroke.Clear();
    }
}

void seeLetters() {
    drainSelect();
    const int n = (int)(sizeof(kTable) / sizeof(kTable[0]));
    int cursor = 0;
    int top = 0;
    int lastCursor = -1;
    int lastTop = -1;
    const int leftPad = 14;
    const int contentTop = KVX_TOPBAR_H + 4;
    const int rowH = uiLineH(FP) + 1;
    const int rows = max(1, (bodyBot() - contentTop) / rowH);

    auto paintRow = [&](int screenRow, int idx, bool sel) {
        int y = contentTop + screenRow * rowH;
        tft.fillRect(0, y, tftWidth, rowH, kvxConfig.bgColor);
        char ch = kTable[idx].ch;
        String line = (ch == ' ' ? String("SPC") : String(ch)) + "   " + kTable[idx].code;
        tft.setTextSize(FP);
        tft.setTextColor(sel ? kvxConfig.bgColor : kvxConfig.priColor,
                         sel ? kvxConfig.priColor : kvxConfig.bgColor);
        tft.drawString(line, leftPad, y);
    };

    auto paintAll = [&]() {
        TftFrame frame;
        clearBody();
        for (int i = 0; i < rows && top + i < n; i++) paintRow(i, top + i, top + i == cursor);
    };

    paintChrome("Letters", ";/. scroll  Esc=back");
    paintAll();
    lastCursor = cursor;
    lastTop = top;

    bool wasUp = false, wasDn = false;
    unsigned long lastRepeat = 0;
    while (!returnToMenu && !forceHome) {
        if (check(EscPress) || check(SelPress)) return;
        bool up = UpPress || isCardputerKeyHeld(';');
        bool dn = DownPress || isCardputerKeyHeld('.');
        if (UpPress) UpPress = false;
        if (DownPress) DownPress = false;
        unsigned long now = millis();
        bool doUp = (up && !wasUp) || (up && wasUp && now - lastRepeat > 150);
        bool doDn = (dn && !wasDn) || (dn && wasDn && now - lastRepeat > 150);
        wasUp = up;
        wasDn = dn;

        if (doUp || doDn) {
            lastRepeat = now;
            int prev = cursor;
            if (doUp && cursor > 0) cursor--;
            else if (doDn && cursor + 1 < n) cursor++;
            if (prev == cursor) continue;

            int oldTop = top;
            if (cursor < top) top = cursor;
            if (cursor >= top + rows) top = cursor - rows + 1;

            if (top != oldTop) {
                paintAll();
            } else {
                TftFrame frame;
                // unhighlight prev if still visible
                if (prev >= top && prev < top + rows) paintRow(prev - top, prev, false);
                if (cursor >= top && cursor < top + rows) paintRow(cursor - top, cursor, true);
            }
            lastCursor = cursor;
            lastTop = top;
        } else {
            delay(25);
        }
    }
    (void)lastCursor;
    (void)lastTop;
}

#if defined(MIC_SPM1423) || defined(MIC_INMP441)

void micMode() {
    drainSelect();
    audioSilenceSpeaker();
    paintChrome("Morse Mic", "Calibrating…");

    String letterCode;
    String decoded;
    bool toneOn = false;
    unsigned long toneStart = 0;
    unsigned long silenceStart = millis();
    unsigned d = ditMs();
    String lastUi = "!";

    int64_t noiseSum = 0;
    int noiseN = 0;
    for (int i = 0; i < 10 && !returnToMenu && !forceHome; i++) {
        audioSilenceSpeaker();
        int16_t *samples = nullptr;
        uint32_t sr = 0;
        if (!mic_capture_samples(512, 8000, 3.0f, &samples, &sr) || !samples) {
            delay(30);
            continue;
        }
        int64_t s = 0;
        for (uint32_t j = 0; j < 512; j++) s += abs(samples[j]);
        free(samples);
        noiseSum += s / 512;
        noiseN++;
    }
    int noiseFloor = noiseN ? (int)(noiseSum / noiseN) : 300;
    int threshold = noiseFloor * 4 + 200;
    if (threshold < 400) threshold = 400;

    paintFooter(("thr=" + String(threshold) + "  Esc=back").c_str());

    auto paint = [&]() {
        String ui = decoded + "|" + letterCode;
        if (ui == lastUi) return;
        lastUi = ui;
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString("Listening…", 8, KVX_TOPBAR_H + 6);
        String show = decoded;
        if (show.length() > 34) show = show.substring(show.length() - 34);
        tft.drawString(show, 8, KVX_TOPBAR_H + 6 + uiLineH(FP) + 2);
        tft.drawString(letterCode, 8, KVX_TOPBAR_H + 6 + 2 * (uiLineH(FP) + 2));
    };
    paint();

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) {
            audioSilenceSpeaker();
            return;
        }
        d = ditMs();
        audioSilenceSpeaker();
        int16_t *samples = nullptr;
        uint32_t sr = 0;
        if (!mic_capture_samples(512, 8000, 3.0f, &samples, &sr) || !samples) {
            delay(20);
            continue;
        }
        int64_t sum = 0;
        int peak = 0;
        for (uint32_t i = 0; i < 512; i++) {
            int a = abs(samples[i]);
            sum += a;
            if (a > peak) peak = a;
        }
        free(samples);
        int avg = (int)(sum / 512);
        bool on = (avg > threshold) || (peak > threshold * 2);
        unsigned long now = millis();

        if (on && !toneOn) {
            toneOn = true;
            toneStart = now;
        } else if (!on && toneOn) {
            unsigned long held = now - toneStart;
            toneOn = false;
            silenceStart = now;
            if (held >= d / 4) {
                if (held >= 2 * d) letterCode += '-';
                else letterCode += '.';
            }
            paint();
        } else if (!on && !toneOn) {
            if (letterCode.length() > 0 && now - silenceStart >= 3 * d) {
                decoded += morseToChar(letterCode);
                letterCode = "";
                silenceStart = now;
                paint();
            } else if (letterCode.length() == 0 && decoded.length() > 0 &&
                       decoded[decoded.length() - 1] != ' ' && now - silenceStart >= 7 * d) {
                decoded += ' ';
                paint();
            }
        }
        delay(5);
    }
    audioSilenceSpeaker();
}
#endif

} // namespace

void morseMenu() {
    if (kvxConfig.morseWpm == 0) kvxConfig.morseWpm = 20;
    while (!returnToMenu && !forceHome) {
        std::vector<Option> opts = {
            {"Text", textMode},
            {"Key", keyMode},
            {"See Letters", seeLetters},
            {"Speed: " + String(kvxConfig.morseWpm) + " WPM", adjustWpm},
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
            {"Mic decode", micMode},
#endif
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Morse");
        if (sel < 0 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}

#else
void morseMenu() {}
#endif
#else
void morseMenu() {}
#endif

#include "dtmf.h"

#include "menu/others/audio.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
#include "menu/others/mic.h"
#endif

#include <Arduino.h>
#include <cmath>
#include <globals.h>
#include <vector>

#if defined(HAS_KEYBOARD) && (defined(HAS_NS4168_SPKR) || defined(BUZZ_PIN))
#ifndef LITE_VERSION

namespace {

struct ToneDef {
    const char *name;
    float f1;
    float f2;
    uint16_t pulses; // 0 = single continuous tone
    uint16_t pulse_ms;
    uint16_t gap_ms;
    char key; // Cardputer key that triggers (0 = menu-only)
};

struct Bank {
    const char *name;
    const ToneDef *tones;
    size_t count;
};

const ToneDef kDialer[] = {
    {"1", 697, 1209, 0, 0, 0, '1'}, {"2", 697, 1336, 0, 0, 0, '2'}, {"3", 697, 1477, 0, 0, 0, '3'},
    {"A", 697, 1633, 0, 0, 0, 'a'}, {"4", 770, 1209, 0, 0, 0, '4'}, {"5", 770, 1336, 0, 0, 0, '5'},
    {"6", 770, 1477, 0, 0, 0, '6'}, {"B", 770, 1633, 0, 0, 0, 'b'}, {"7", 852, 1209, 0, 0, 0, '7'},
    {"8", 852, 1336, 0, 0, 0, '8'}, {"9", 852, 1477, 0, 0, 0, '9'}, {"C", 852, 1633, 0, 0, 0, 'c'},
    {"*", 941, 1209, 0, 0, 0, '*'}, {"0", 941, 1336, 0, 0, 0, '0'}, {"#", 941, 1477, 0, 0, 0, '#'},
    {"D", 941, 1633, 0, 0, 0, 'd'},
};

const ToneDef kBluebox[] = {
    {"1", 700, 900, 0, 0, 0, '1'},   {"2", 700, 1100, 0, 0, 0, '2'}, {"3", 900, 1100, 0, 0, 0, '3'},
    {"4", 700, 1300, 0, 0, 0, '4'},  {"5", 900, 1300, 0, 0, 0, '5'}, {"6", 1100, 1300, 0, 0, 0, '6'},
    {"7", 700, 1500, 0, 0, 0, '7'},  {"8", 900, 1500, 0, 0, 0, '8'}, {"9", 1100, 1500, 0, 0, 0, '9'},
    {"0", 1300, 1500, 0, 0, 0, '0'}, {"KP", 1100, 1700, 0, 0, 0, 'k'}, {"ST", 1500, 1700, 0, 0, 0, 's'},
    {"2600", 2600, 0, 0, 0, 0, 'z'},
};

const ToneDef kRedboxUS[] = {
    {"Nickel", 1700, 2200, 1, 66, 0, '1'},
    {"Dime", 1700, 2200, 2, 66, 66, '2'},
    {"Quarter", 1700, 2200, 5, 33, 33, '3'},
    {"Dollar", 1700, 2200, 1, 650, 0, '4'},
};

const ToneDef kRedboxCA[] = {
    {"Nickel", 2200, 0, 1, 66, 0, '1'},
    {"Dime", 2200, 0, 2, 66, 66, '2'},
    {"Quarter", 2200, 0, 5, 33, 33, '3'},
};

const ToneDef kRedboxUK[] = {
    {"10p", 1000, 0, 1, 200, 0, '1'},
    {"50p", 1000, 0, 1, 350, 0, '2'},
};

const ToneDef kMisc[] = {
    {"CCITT 11", 700, 1700, 0, 0, 0, '1'},
    {"CCITT 12", 900, 1700, 0, 0, 0, '2'},
    {"CCITT KP2", 1300, 1700, 0, 0, 0, '3'},
    {"EAS", 853, 960, 0, 0, 0, '4'},
};

void playToneDef(const ToneDef &t) {
    // Ensure audible pulse length (short redbox pulses stay as-defined).
    if (t.pulses == 0) {
        playDualTone((unsigned)t.f1, (unsigned)t.f2, 280);
        return;
    }
    for (uint16_t i = 0; i < t.pulses; i++) {
        unsigned ms = t.pulse_ms ? t.pulse_ms : 66;
        playDualTone((unsigned)t.f1, (unsigned)t.f2, ms);
        if (t.gap_ms > 0 && i + 1 < t.pulses) delay(t.gap_ms);
    }
}

void paintFooter(const char *hint) {
    const int fy = uiFooterY(uiDenseFont());
    tft.fillRect(0, fy - 1, tftWidth, tftHeight - (fy - 1), kvxConfig.bgColor);
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawCentreString(hint, tftWidth / 2, fy, 1);
}

const ToneDef *findTone(const Bank &bank, char c) {
    char lc = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    for (size_t i = 0; i < bank.count; i++) {
        if (bank.tones[i].key == lc || bank.tones[i].key == c) return &bank.tones[i];
    }
    return nullptr;
}

struct PadGeom {
    int dialY;
    int margin;
    int hgap;
    int vgap;
    int top;
    int colW;
    int rowH;
};

static const char *const kPadLabels[16] = {
    "1", "2", "3", "A", "4", "5", "6", "B", "7", "8", "9", "C", "*", "0", "#", "D",
};
static const char kPadKeys[16] = {
    '1', '2', '3', 'a', '4', '5', '6', 'b', '7', '8', '9', 'c', '*', '0', '#', 'd',
};

PadGeom makePadGeom() {
    PadGeom g{};
    g.dialY = KVX_TOPBAR_H + 2;
    g.margin = 4;
    g.hgap = 3;
    g.vgap = 3;
    g.top = g.dialY + uiLineH(FP) + 2;
    const int bottom = uiFooterY(uiDenseFont()) - 2;
    g.colW = (tftWidth - g.margin * 2 - g.hgap * 3) / 4;
    g.rowH = (bottom - g.top - g.vgap * 3) / 4;
    return g;
}

void drawPadBtn(const PadGeom &g, int idx, bool highlight) {
    int col = idx % 4;
    int row = idx / 4;
    int x = g.margin + col * (g.colW + g.hgap);
    int y = g.top + row * (g.rowH + g.vgap);
    const uint16_t fill = highlight ? kvxConfig.priColor : kvxConfig.secColor;
    const uint16_t ink = highlight ? kvxConfig.bgColor : kvxConfig.priColor;
    tft.fillRoundRect(x, y, g.colW, g.rowH, 3, fill);
    tft.drawRoundRect(x, y, g.colW, g.rowH, 3, kvxConfig.priColor);
    tft.setTextSize(FM);
    tft.setTextColor(ink, fill);
    tft.drawCentreString(kPadLabels[idx], x + g.colW / 2, y + (g.rowH - 12) / 2, 1);
}

void paintDialLine(const PadGeom &g, const String &dialed) {
    String d = dialed.length() ? dialed : String(" ");
    if (d.length() > 22) d = d.substring(d.length() - 22);
    tft.fillRect(0, g.dialY, tftWidth, uiLineH(FP) + 1, kvxConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawCentreString(d, tftWidth / 2, g.dialY, 1);
}

int padIndexForKey(char key) {
    char lc = (key >= 'A' && key <= 'Z') ? (char)(key - 'A' + 'a') : key;
    for (int i = 0; i < 16; i++) {
        if (kPadKeys[i] == lc || kPadKeys[i] == key) return i;
    }
    return -1;
}

void runDialer() {
    Bank bank = {"Dialer", kDialer, sizeof(kDialer) / sizeof(kDialer[0])};
    String dialed;
    PadGeom g = makePadGeom();

    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("Dialer");
        paintDialLine(g, dialed);
        for (int i = 0; i < 16; i++) drawPadBtn(g, i, false);
        paintFooter("keys=tones  Esc=back");
    }

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(20);
            continue;
        }
        for (char c : key.word) {
            const ToneDef *t = findTone(bank, c);
            if (!t) continue;
            int idx = padIndexForKey(t->key);
            {
                TftFrame frame;
                if (idx >= 0) drawPadBtn(g, idx, true);
            }
            playToneDef(*t);
            dialed += t->name;
            {
                TftFrame frame;
                if (idx >= 0) drawPadBtn(g, idx, false);
                paintDialLine(g, dialed);
            }
        }
        KeyStroke.Clear();
    }
}

void runBank(const Bank &bank) {
    String dialed;
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar(bank.name);
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        int y = KVX_TOPBAR_H + 4;
        for (size_t i = 0; i < bank.count; i++) {
            String line = String(bank.tones[i].key) + ": " + bank.tones[i].name;
            tft.drawString(line, 8, y);
            y += uiLineH(FP);
            if (y > uiFooterY(uiDenseFont()) - 2) break;
        }
        paintFooter("keys=tones  Esc=back");
    }

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(20);
            continue;
        }
        for (char c : key.word) {
            const ToneDef *t = findTone(bank, c);
            if (!t) continue;
            playToneDef(*t);
            dialed += t->name;
            String d = dialed;
            if (d.length() > 36) d = d.substring(d.length() - 36);
            TftFrame frame;
            paintFooter(d.c_str());
        }
        KeyStroke.Clear();
    }
}

#if defined(MIC_SPM1423) || defined(MIC_INMP441)

float goertzelPower(const int16_t *samples, int n, float targetHz, float sampleRate) {
    float omega = 2.0f * PI * targetHz / sampleRate;
    float coeff = 2.0f * cosf(omega);
    float s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        s0 = (float)samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    float real = s1 - s2 * cosf(omega);
    float imag = s2 * sinf(omega);
    return real * real + imag * imag;
}

char decodeDtmf(const int16_t *samples, int n, float sampleRate) {
    static const float rowF[] = {697, 770, 852, 941};
    static const float colF[] = {1209, 1336, 1477, 1633};
    static const char map[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'},
    };
    float rowP[4], colP[4];
    for (int i = 0; i < 4; i++) {
        rowP[i] = goertzelPower(samples, n, rowF[i], sampleRate);
        colP[i] = goertzelPower(samples, n, colF[i], sampleRate);
    }
    int bestR = 0, bestC = 0;
    for (int i = 1; i < 4; i++) {
        if (rowP[i] > rowP[bestR]) bestR = i;
        if (colP[i] > colP[bestC]) bestC = i;
    }
    float floor = 1e10f;
    for (int i = 0; i < 4; i++) {
        if (rowP[i] < floor) floor = rowP[i];
        if (colP[i] < floor) floor = colP[i];
    }
    if (rowP[bestR] < floor * 6.0f || colP[bestC] < floor * 6.0f) return 0;
    if (rowP[bestR] < 1e7f || colP[bestC] < 1e7f) return 0;
    return map[bestR][bestC];
}

void runListen() {
    String decoded;
    String lastUi = "!";
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("DTMF Listen");
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString("Listening…", 8, KVX_TOPBAR_H + 6);
        paintFooter("Esc=back");
    }

    auto paintDecoded = [&]() {
        String show = decoded;
        if (show.length() > 36) show = show.substring(show.length() - 36);
        if (show == lastUi) return;
        lastUi = show;
        int y = KVX_TOPBAR_H + 6 + uiLineH(FP) + 2;
        TftFrame frame;
        tft.fillRect(0, y, tftWidth, uiLineH(FP) + 2, kvxConfig.bgColor);
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString(show, 8, y);
    };

    char last = 0;
    unsigned long lastMs = 0;
    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;
        audioSilenceSpeaker();
        int16_t *samples = nullptr;
        uint32_t sr = 0;
        if (!mic_capture_samples(512, 8000, 2.5f, &samples, &sr) || !samples) {
            delay(30);
            continue;
        }
        char d = decodeDtmf(samples, 512, (float)sr);
        free(samples);
        unsigned long now = millis();
        if (d && (d != last || now - lastMs > 400)) {
            decoded += d;
            last = d;
            lastMs = now;
            paintDecoded();
        } else if (!d) {
            last = 0;
        }
        delay(15);
    }
}
#endif

} // namespace

void dtmfMenu() {
    while (!returnToMenu && !forceHome) {
        std::vector<Option> opts = {
            {"Dialer", []() { runDialer(); }},
            {"Bluebox",
             []() {
                 Bank b = {"Bluebox", kBluebox, sizeof(kBluebox) / sizeof(kBluebox[0])};
                 runBank(b);
             }},
            {"Redbox US",
             []() {
                 Bank b = {"Redbox US", kRedboxUS, sizeof(kRedboxUS) / sizeof(kRedboxUS[0])};
                 runBank(b);
             }},
            {"Redbox CA",
             []() {
                 Bank b = {"Redbox CA", kRedboxCA, sizeof(kRedboxCA) / sizeof(kRedboxCA[0])};
                 runBank(b);
             }},
            {"Redbox UK",
             []() {
                 Bank b = {"Redbox UK", kRedboxUK, sizeof(kRedboxUK) / sizeof(kRedboxUK[0])};
                 runBank(b);
             }},
            {"Misc",
             []() {
                 Bank b = {"Misc", kMisc, sizeof(kMisc) / sizeof(kMisc[0])};
                 runBank(b);
             }},
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
            {"Listen", []() { runListen(); }},
#endif
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "DTMF");
        if (sel < 0 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}

#else
void dtmfMenu() {}
#endif
#else
void dtmfMenu() {}
#endif

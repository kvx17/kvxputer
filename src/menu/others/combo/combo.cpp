#include "combo.h"

#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"

#include <Arduino.h>
#include <globals.h>
#include <string.h>
#include <vector>

#if defined(HAS_KEYBOARD)

namespace {

constexpr uint8_t LOCK_INDEX_COUNT = 40;
constexpr uint8_t RESISTANCE_INDEX_COUNT = 80;
constexpr uint8_t MAX_VALUES = 10;

struct ComboLockCombination {
    uint8_t second_pin_count;
    uint8_t third_pin_count;
    uint8_t first_pin_index;
    uint8_t second_pin_index[MAX_VALUES];
    uint8_t third_pin_index[MAX_VALUES];
};

void calculateSolution(uint8_t firstLock, uint8_t secondLock, uint8_t resistanceIndex,
                       ComboLockCombination &solution) {
    memset(&solution, 0, sizeof(solution));

    unsigned int pin0 = resistanceIndex / 2u;
    if (resistanceIndex % 2u != 0u) pin0 += 6;
    else pin0 += 5;
    pin0 %= LOCK_INDEX_COUNT;
    solution.first_pin_index = (uint8_t)pin0;

    uint8_t remainder = solution.first_pin_index % 4;
    uint8_t a = firstLock;
    uint8_t b = secondLock;
    for (uint8_t i = 0; i < 4u; i++) {
        if ((a % 4u) == remainder) solution.third_pin_index[solution.third_pin_count++] = a;
        if ((b % 4u) == remainder) solution.third_pin_index[solution.third_pin_count++] = b;
        a = (a + 10u) % 40u;
        b = (b + 10u) % 40u;
    }

    uint8_t row_1 = (remainder + 2) % LOCK_INDEX_COUNT;
    uint8_t row_2 = (row_1 + 4) % LOCK_INDEX_COUNT;
    solution.second_pin_index[solution.second_pin_count++] = row_1;
    solution.second_pin_index[solution.second_pin_count++] = row_2;
    for (uint8_t i = 0; i < 4u; i++) {
        row_1 = (row_1 + 8u) % LOCK_INDEX_COUNT;
        row_2 = (row_2 + 8u) % LOCK_INDEX_COUNT;
        solution.second_pin_index[solution.second_pin_count++] = row_1;
        solution.second_pin_index[solution.second_pin_count++] = row_2;
    }

    for (uint8_t i = 0; i + 1 < solution.second_pin_count; i++) {
        for (uint8_t j = i + 1; j < solution.second_pin_count; j++) {
            if (solution.second_pin_index[i] > solution.second_pin_index[j]) {
                uint8_t t = solution.second_pin_index[i];
                solution.second_pin_index[i] = solution.second_pin_index[j];
                solution.second_pin_index[j] = t;
            }
        }
    }
}

String resistanceLabel(uint8_t idx) {
    if (idx >= RESISTANCE_INDEX_COUNT) idx = 0;
    int whole = idx / 2;
    bool half = (idx % 2) != 0;
    return String(whole) + (half ? ".5" : ".0");
}

void drainSelect() {
    resetHeldNavKeys();
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
    KeyStroke.Clear();
    delay(60);
    while (check(SelPress)) {
        SelPress = false;
        delay(20);
    }
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

void showHelp() {
    static const char *const lines[] = {
        "Master Lock combo helper.",
        "",
        "Find the 2 groove numbers",
        "on the lock dial, and the",
        "resistance (sticky spot).",
        "",
        "In Crack:",
        "  ;/.  move up/down",
        "  ,/   change value",
        "  Enter on Calculate",
        "",
        "Inspired by DoobTheGoober's",
        "ComboCracker-FZ (Flipper).",
        "Ported to kvxputer by kvx.",
        "",
        "Method: Samy Kamkar.",
    };
    const int n = (int)(sizeof(lines) / sizeof(lines[0]));
    const int lineH = uiLineH(FP) + 1;
    const int top = KVX_TOPBAR_H + 4;
    const int rows = max(1, (bodyBot() - top) / lineH);
    int topLine = 0;
    int lastTop = -1;

    auto paint = [&]() {
        if (topLine == lastTop) return;
        lastTop = topLine;
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        for (int i = 0; i < rows && topLine + i < n; i++) {
            tft.drawString(lines[topLine + i], 8, top + i * lineH);
        }
    };

    drainSelect();
    paintChrome("Combo Help", ";/. scroll  Esc/Enter back");
    paint();

    bool wasUp = false, wasDn = false;
    unsigned long lastRepeat = 0;
    while (!returnToMenu && !forceHome) {
        if (check(EscPress) || check(SelPress)) {
            drainSelect();
            return;
        }
        bool up = isCardputerKeyHeld(';') || UpPress || PrevPress;
        bool dn = isCardputerKeyHeld('.') || DownPress || NextPress;
        if (UpPress) UpPress = false;
        if (DownPress) DownPress = false;
        if (PrevPress) PrevPress = false;
        if (NextPress) NextPress = false;

        unsigned long now = millis();
        bool edgeUp = up && !wasUp;
        bool edgeDn = dn && !wasDn;
        bool holdOk = (now - lastRepeat > 160);
        bool doUp = edgeUp || (up && holdOk && wasUp);
        bool doDn = edgeDn || (dn && holdOk && wasDn);
        wasUp = up;
        wasDn = dn;

        if (doUp || doDn) {
            lastRepeat = now;
            if (doUp && topLine > 0) topLine--;
            else if (doDn && topLine + rows < n) topLine++;
            paint();
        } else {
            delay(25);
        }
    }
}

void showResults(const ComboLockCombination &sol) {
    drainSelect();
    paintChrome("Combo", "Esc/Enter=back");
    {
        TftFrame frame;
        clearBody();
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        int y = KVX_TOPBAR_H + 4;
        tft.drawString("First: " + String(sol.first_pin_index), 8, y);
        y += uiLineH(FP) + 2;
        String seconds = "Second:";
        for (uint8_t i = 0; i < sol.second_pin_count; i++) {
            seconds += (i ? "," : " ") + String(sol.second_pin_index[i]);
        }
        while (seconds.length() > 0 && y < bodyBot() - 2) {
            String line = seconds.substring(0, 36);
            tft.drawString(line, 8, y);
            y += uiLineH(FP);
            if (seconds.length() <= 36) break;
            seconds = seconds.substring(36);
        }
        String thirds = "Third:";
        for (uint8_t i = 0; i < sol.third_pin_count; i++) {
            thirds += (i ? "," : " ") + String(sol.third_pin_index[i]);
        }
        tft.drawString(thirds.substring(0, 36), 8, y);
    }
    while (!returnToMenu && !forceHome) {
        if (check(EscPress) || check(SelPress)) break;
        delay(40);
    }
    drainSelect();
}

void crackerScreen() {
    uint8_t firstLock = 0;
    uint8_t secondLock = 0;
    uint8_t resistance = 0;
    int cursor = 0;
    const int nRows = 6;
    const int rowH = uiLineH(FP) + 2;
    const int listTop = KVX_TOPBAR_H + 4;

    auto rowLabel = [&](int i) -> String {
        switch (i) {
            case 0: return "1st groove: " + String(firstLock);
            case 1: return "2nd groove: " + String(secondLock);
            case 2: return "Resistance: " + resistanceLabel(resistance);
            case 3: return "Calculate";
            case 4: return "How to use";
            default: return "Back";
        }
    };

    auto paintRow = [&](int i, bool selected) {
        int y = listTop + i * rowH;
        tft.fillRect(0, y, tftWidth, rowH, kvxConfig.bgColor);
        tft.setTextSize(FP);
        tft.setTextColor(selected ? kvxConfig.bgColor : kvxConfig.priColor,
                         selected ? kvxConfig.priColor : kvxConfig.bgColor);
        tft.drawString(rowLabel(i), 8, y);
    };

    auto paintAllRows = [&]() {
        TftFrame frame;
        clearBody();
        for (int i = 0; i < nRows; i++) paintRow(i, i == cursor);
    };

    drainSelect();
    paintChrome("Combo Cracker", ";/. move  ,/ value  Enter");
    paintAllRows();

    bool wasUp = false, wasDn = false, wasLt = false, wasRt = false;
    unsigned long lastRepeat = 0;

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;

        bool up = isCardputerKeyHeld(';') || UpPress;
        bool dn = isCardputerKeyHeld('.') || DownPress;
        bool lt = isCardputerKeyHeld(',') || PrevPagePress;
        bool rt = isCardputerKeyHeld('/') || NextPagePress;
        if (UpPress) UpPress = false;
        if (DownPress) DownPress = false;
        if (PrevPagePress) PrevPagePress = false;
        if (NextPagePress) NextPagePress = false;

        unsigned long now = millis();
        bool edgeUp = up && !wasUp;
        bool edgeDn = dn && !wasDn;
        bool edgeLt = lt && !wasLt;
        bool edgeRt = rt && !wasRt;
        bool holdOk = (now - lastRepeat > 150);
        bool doUp = edgeUp || (up && holdOk && wasUp);
        bool doDn = edgeDn || (dn && holdOk && wasDn);
        bool doLt = edgeLt || (lt && holdOk && wasLt);
        bool doRt = edgeRt || (rt && holdOk && wasRt);
        wasUp = up;
        wasDn = dn;
        wasLt = lt;
        wasRt = rt;

        if (doUp || doDn) {
            lastRepeat = now;
            int prev = cursor;
            if (doUp && cursor > 0) cursor--;
            else if (doDn && cursor + 1 < nRows) cursor++;
            if (prev != cursor) {
                TftFrame frame;
                paintRow(prev, false);
                paintRow(cursor, true);
            }
            continue;
        }

        if ((doLt || doRt) && cursor <= 2) {
            lastRepeat = now;
            int dir = doRt ? 1 : -1;
            if (cursor == 0) {
                int v = (int)firstLock + dir;
                if (v < 0) v = 39;
                if (v > 39) v = 0;
                firstLock = (uint8_t)v;
            } else if (cursor == 1) {
                int v = (int)secondLock + dir;
                if (v < 0) v = 39;
                if (v > 39) v = 0;
                secondLock = (uint8_t)v;
            } else {
                int v = (int)resistance + dir;
                if (v < 0) v = 79;
                if (v > 79) v = 0;
                resistance = (uint8_t)v;
            }
            TftFrame frame;
            paintRow(cursor, true);
            continue;
        }

        if (check(SelPress)) {
            drainSelect();
            if (cursor == 3) {
                ComboLockCombination sol;
                calculateSolution(firstLock, secondLock, resistance, sol);
                if (sol.second_pin_count < 1 || sol.third_pin_count < 1) {
                    displayError("Bad inputs", true);
                } else {
                    showResults(sol);
                }
                paintChrome("Combo Cracker", ";/. move  ,/ value  Enter");
                paintAllRows();
                wasUp = wasDn = wasLt = wasRt = false;
            } else if (cursor == 4) {
                showHelp();
                paintChrome("Combo Cracker", ";/. move  ,/ value  Enter");
                paintAllRows();
                wasUp = wasDn = wasLt = wasRt = false;
            } else if (cursor == 5) {
                return;
            }
        }
        delay(25);
    }
}

} // namespace

void comboMenu() {
    while (!returnToMenu && !forceHome) {
        std::vector<Option> opts = {
            {"Crack", crackerScreen},
            {"How to use", showHelp},
            {"About",
             []() {
                 displayInfo(
                     "Inspired by DoobTheGoober ComboCracker-FZ. Ported by kvx.",
                     true
                 );
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Combo");
        if (sel < 0 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}

#else

void comboMenu() {}

#endif

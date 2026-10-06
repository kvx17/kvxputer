#include "passgen.h"

#include "root/app/utils.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "menu/others/pda/pda_common.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_random.h>
#include <globals.h>
#include <vector>

#if defined(HAS_KEYBOARD)

namespace {

constexpr int kMinLen = 4;
constexpr int kMaxLen = 64;
constexpr int kMinCount = 1;
constexpr int kMaxCount = 8;

constexpr const char *kDigits = "0123456789";
constexpr const char *kLower = "abcdefghijklmnopqrstuvwxyz";
constexpr const char *kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char *kSymbols = "!#$%^&*.-_";
constexpr const char *kFooter = ";/. len  ,/ cnt  G gen  S save";

struct PassgenState {
    int length = 16;
    int count = 1;
    bool digits = true;
    bool lower = true;
    bool upper = true;
    bool symbols = false;
    std::vector<String> passwords;
};

void drainKeys() {
    resetHeldNavKeys();
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
    KeyStroke.Clear();
    delay(40);
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

String buildAlphabet(const PassgenState &st) {
    String a;
    if (st.digits) a += kDigits;
    if (st.lower) a += kLower;
    if (st.upper) a += kUpper;
    if (st.symbols) a += kSymbols;
    return a;
}

String generateOne(const String &alphabet, int length) {
    String out;
    out.reserve(length);
    const size_t n = alphabet.length();
    if (n == 0 || length <= 0) return out;
    for (int i = 0; i < length; i++) out += alphabet.charAt(esp_random() % n);
    return out;
}

void regenerate(PassgenState &st) {
    String alpha = buildAlphabet(st);
    st.passwords.clear();
    if (alpha.length() == 0) return;
    st.passwords.reserve(st.count);
    for (int i = 0; i < st.count; i++) st.passwords.push_back(generateOne(alpha, st.length));
}

void paintBody(const PassgenState &st) {
    TftFrame frame;
    clearBody();
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);

    int y = KVX_TOPBAR_H + 4;
    String flags = String(st.upper ? "[U]" : " U ") + (st.lower ? "[L]" : " L ") +
                   (st.digits ? "[D]" : " D ") + (st.symbols ? "[C]" : " C ");
    tft.drawString("Len " + String(st.length) + "  Cnt " + String(st.count), 8, y);
    y += uiLineH(FP) + 2;
    tft.drawString(flags, 8, y);
    y += uiLineH(FP) + 6;

    if (st.passwords.empty()) {
        tft.drawString("Press G to generate", 8, y);
    } else {
        for (size_t i = 0; i < st.passwords.size(); i++) {
            String line = String(i + 1) + ") " + st.passwords[i];
            if (line.length() > 36) line = line.substring(0, 33) + "...";
            tft.drawString(line, 8, y);
            y += uiLineH(FP);
            if (y > bodyBot() - 2) break;
        }
    }
}

String physicalLineEdit(const char *title, const String &initial, int maxLen) {
    String text = initial;
    String last;
    drainKeys();
    paintChrome(title, "Enter=ok  Esc=cancel");
    while (!returnToMenu && !forceHome) {
        if (text != last) {
            last = text;
            TftFrame frame;
            clearBody();
            tft.setTextSize(FP);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            int y = KVX_TOPBAR_H + 6;
            tft.drawString("Type…", 8, y);
            y += uiLineH(FP) + 4;
            tft.drawRect(6, y, tftWidth - 12, uiLineH(FP) + 6, kvxConfig.priColor);
            String show = text;
            if (show.length() > 34) show = show.substring(show.length() - 34);
            tft.drawString(show, 10, y + 3);
            paintFooter((String(text.length()) + "/" + String(maxLen) + "  Enter=ok Esc=cancel").c_str());
        }
        if (check(EscPress)) return "\x1B";
        if (check(SelPress) || KeyStroke.enter) {
            drainKeys();
            return text;
        }
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(20);
            continue;
        }
        if (key.del && text.length()) text.remove(text.length() - 1);
        else if (key.enter) {
            drainKeys();
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

bool savePasswords(PassgenState &st) {
    if (st.passwords.empty()) {
        displayError("Generate first (G)", true);
        return false;
    }
    if (!setupSdCard()) {
        displayError("No SD card", true);
        return false;
    }

    String site = physicalLineEdit("Site / App", "", 40);
    if (site == "\x1B") return false;
    String user = physicalLineEdit("Username", "", 40);
    if (user == "\x1B") return false;

    kvx::paths::ensureDir(SD, kvx::paths::PASSGEN);
    String name = physicalLineEdit("File name", site.length() ? site : "password", 40);
    if (name == "\x1B" || name.length() == 0) return false;
    name = pdaSanitizeName(name);
    String lower = name;
    lower.toLowerCase();
    if (!lower.endsWith(".txt")) name += ".txt";
    String path = String(kvx::paths::PASSGEN) + "/" + name;

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        displayError("Save failed", true);
        return false;
    }
    f.println("Site: " + site);
    f.println("Username: " + user);
    f.println("Length: " + String(st.length));
    f.println("---");
    for (const auto &p : st.passwords) f.println(p);
    f.close();
    displaySuccess("Saved", true);
    return true;
}

} // namespace

void passgenMenu() {
    PassgenState st;
    regenerate(st);
    drainKeys();
    paintChrome("Passgen", kFooter);
    paintBody(st);

    // Edge-trigger nav to avoid held-key redraw spam
    bool wasUp = false, wasDn = false, wasLt = false, wasRt = false;
    unsigned long lastRepeat = 0;

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) return;

        bool up = isCardputerKeyHeld(';') || UpPress;
        bool dn = isCardputerKeyHeld('.') || DownPress;
        bool lt = isCardputerKeyHeld(',') || PrevPagePress;
        bool rt = isCardputerKeyHeld('/') || NextPagePress;
        // consume one-shot flags
        if (UpPress) UpPress = false;
        if (DownPress) DownPress = false;
        if (PrevPagePress) PrevPagePress = false;
        if (NextPagePress) NextPagePress = false;

        unsigned long now = millis();
        bool edgeUp = up && !wasUp;
        bool edgeDn = dn && !wasDn;
        bool edgeLt = lt && !wasLt;
        bool edgeRt = rt && !wasRt;
        bool holdOk = (now - lastRepeat > 180);
        bool doUp = edgeUp || (up && holdOk && wasUp);
        bool doDn = edgeDn || (dn && holdOk && wasDn);
        bool doLt = edgeLt || (lt && holdOk && wasLt);
        bool doRt = edgeRt || (rt && holdOk && wasRt);
        wasUp = up;
        wasDn = dn;
        wasLt = lt;
        wasRt = rt;

        if (doUp || doDn || doLt || doRt) {
            lastRepeat = now;
            bool changed = false;
            if (doUp && st.length < kMaxLen) {
                st.length++;
                changed = true;
            } else if (doDn && st.length > kMinLen) {
                st.length--;
                changed = true;
            } else if (doLt && st.count > kMinCount) {
                st.count--;
                changed = true;
            } else if (doRt && st.count < kMaxCount) {
                st.count++;
                changed = true;
            }
            if (changed) {
                regenerate(st);
                paintBody(st);
            }
            continue;
        }

        if (check(SelPress)) {
            savePasswords(st);
            drainKeys();
            paintChrome("Passgen", kFooter);
            paintBody(st);
            wasUp = wasDn = wasLt = wasRt = false;
            continue;
        }

        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(25);
            continue;
        }

        bool redraw = false;
        for (char c : key.word) {
            char lc = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            if (lc == 'u') {
                st.upper = !st.upper;
                regenerate(st);
                redraw = true;
            } else if (lc == 'l') {
                st.lower = !st.lower;
                regenerate(st);
                redraw = true;
            } else if (lc == 'd') {
                st.digits = !st.digits;
                regenerate(st);
                redraw = true;
            } else if (lc == 'c') {
                st.symbols = !st.symbols;
                regenerate(st);
                redraw = true;
            } else if (lc == 'g') {
                regenerate(st);
                redraw = true;
            } else if (lc == 's') {
                KeyStroke.Clear();
                savePasswords(st);
                drainKeys();
                paintChrome("Passgen", kFooter);
                paintBody(st);
                wasUp = wasDn = wasLt = wasRt = false;
                redraw = false;
                break;
            }
        }
        KeyStroke.Clear();
        if (redraw) paintBody(st);
    }
}

#else

void passgenMenu() {}

#endif

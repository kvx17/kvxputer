#include "calc_shell.h"

#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <globals.h>

namespace {

static void consumeNavFlags() {
    check(UpPress);
    check(DownPress);
    check(PrevPress);
    check(NextPress);
    check(SelPress);
    check(EscPress);
}

static bool isWide() { return tftWidth >= 480; }

// Labels: dense. Values: body. Row height follows the taller value text.
static int calcLabelFont() { return uiDenseFont(); }
static int calcValueFont() { return uiBodyFont(); }
static int calcHeroFont() { return uiBodyFont(); }
static int rowH() { return uiLineH(calcValueFont()) + 2; }

static void drawChip(int x, int y, const char *label, bool active) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t sec = kvxConfig.secColor;
    const uint16_t pri = kvxConfig.priColor;
    const int sz = calcLabelFont();
    const int pad = 3;
    const int w = (int)strlen(label) * uiCharW(sz) + pad * 2;
    const int h = uiLineH(sz) + 2;
    uint16_t fill = active ? sec : getColorVariation(pri, 10, -1);
    uint16_t fg = active ? bg : pri;
    tft.fillRoundRect(x, y, w, h, 2, fill);
    tft.drawRoundRect(x, y, w, h, 2, sec);
    tft.setTextSize(sz);
    tft.setTextColor(fg, fill);
    tft.drawCentreString(label, x + w / 2, y + 1, 1);
}

static int chipW(const char *label) {
    return (int)strlen(label) * uiCharW(calcLabelFont()) + 6;
}

static int chipH() { return uiLineH(calcLabelFont()) + 2; }

static void drawRow(int x, int y, int w, int h, const String &left, const String &right, bool focused) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const int labelSz = calcLabelFont();
    const int valueSz = calcValueFont();
    uint16_t fill = focused ? pri : getColorVariation(pri, 10, -1);
    uint16_t fg = focused ? bg : pri;
    tft.fillRoundRect(x, y, w, h - 1, 2, fill);
    tft.drawRoundRect(x, y, w, h - 1, 2, pri);

    // Prefer full value text. Shrink the dense label first; only trim the
    // value if it still cannot fit beside a minimal label.
    const int pad = 3;
    const int gap = 4;
    const int usable = w - pad * 2;
    String R = right;
    String L = left;
    int valuePx = (int)R.length() * uiCharW(valueSz);
    int minLabelPx = min((int)L.length() * uiCharW(labelSz), 3 * uiCharW(labelSz));
    if (valuePx + gap + minLabelPx > usable && R.length()) {
        int maxValChars = max(1, (usable - gap - minLabelPx) / uiCharW(valueSz));
        if ((int)R.length() > maxValChars) R = R.substring(R.length() - maxValChars);
        valuePx = (int)R.length() * uiCharW(valueSz);
    }
    int labelBudget = usable - valuePx - gap;
    int labelMaxChars = max(1, labelBudget / uiCharW(labelSz));
    if ((int)L.length() > labelMaxChars) L = L.substring(0, labelMaxChars);

    // Vertically center dense label next to body value.
    const int labelY = y + max(1, (h - 1 - uiLineH(labelSz)) / 2);
    const int valueY = y + max(1, (h - 1 - uiLineH(valueSz)) / 2);

    tft.setTextSize(labelSz);
    tft.setTextColor(fg, fill);
    tft.setCursor(x + pad, labelY);
    tft.print(L);
    if (R.length()) {
        tft.setTextSize(valueSz);
        tft.setTextColor(focused ? sec : sec, fill);
        tft.drawRightString(R, x + w - pad, valueY, 1);
    }
}

static void drawHeroLine(int x, int y, int w, const String &label, const String &value) {
    const uint16_t bg = kvxConfig.bgColor;
    const uint16_t pri = kvxConfig.priColor;
    const uint16_t sec = kvxConfig.secColor;
    const int labelSz = calcLabelFont();
    const int heroSz = calcHeroFont();
    String L = label;
    String v = value;
    // Hero value on its own line — use full width; trim only if wider than the row.
    int maxC = max(1, (w - 4) / uiCharW(heroSz));
    if ((int)v.length() > maxC) v = v.substring(v.length() - maxC);
    int labelMax = max(1, (w - 4) / uiCharW(labelSz));
    if ((int)L.length() > labelMax) L = L.substring(0, labelMax);
    tft.setTextSize(labelSz);
    tft.setTextColor(sec, bg);
    tft.setCursor(x + 2, y);
    tft.print(L);
    tft.setTextSize(heroSz);
    tft.setTextColor(pri, bg);
    tft.drawRightString(v, x + w - 2, y + uiLineH(labelSz), 1);
}

struct FocusItem {
    enum Kind : uint8_t { Mode, Units, Field } kind;
    int index = 0; // field index when Field
};

static void buildFocus(const CalcSession &s, std::vector<FocusItem> &out) {
    out.clear();
    if (s.modeNames.size() > 0) out.push_back({FocusItem::Mode, 0});
    out.push_back({FocusItem::Units, 0});
    for (int i = 0; i < (int)s.fields.size(); i++) out.push_back({FocusItem::Field, i});
}

static void editField(CalcField &f) {
    if (f.kind == CE_CHOICE) {
        if (f.choices.empty()) return;
        std::vector<Option> opts;
        String chosen = f.value;
        for (const auto &c : f.choices) {
            String label = c;
            opts.push_back({label.c_str(), [&chosen, c]() { chosen = c; }});
        }
        loopOptions(opts, f.label.c_str());
        consumeNavFlags();
        f.value = chosen;
        return;
    }
    String edited;
    if (f.kind == CE_NUM) {
        edited = num_keyboard(f.value, f.maxLen, f.label);
    } else if (f.kind == CE_HEX) {
        edited = hex_keyboard(f.value, f.maxLen, f.label);
    } else {
        edited = keyboard(f.value, f.maxLen, f.label);
    }
    consumeNavFlags();
    f.value = edited;
}

static void pickMode(CalcSession &s) {
    if (s.modeNames.size() <= 1) return;
    if (s.modeNames.size() == 2) {
        s.mode = (s.mode + 1) % (int)s.modeNames.size();
    } else {
        std::vector<Option> opts;
        int chosen = s.mode;
        for (int i = 0; i < (int)s.modeNames.size(); i++) {
            String label = s.modeNames[i];
            opts.push_back({label.c_str(), [&chosen, i]() { chosen = i; }});
        }
        loopOptions(opts, "Mode");
        consumeNavFlags();
        s.mode = chosen;
    }
    if (s.onMode) s.onMode(s);
    if (s.recompute) s.recompute(s);
}

static void toggleUnits(CalcSession &s) {
    kvxConfig.calcUseImperial = !kvxConfig.calcUseImperial;
    kvxConfig.saveFile();
    if (s.onMode) s.onMode(s);
    if (s.recompute) s.recompute(s);
}

static void cycleMode(CalcSession &s) {
    if (s.modeNames.empty()) return;
    s.mode = (s.mode + 1) % (int)s.modeNames.size();
    if (s.onMode) s.onMode(s);
    if (s.recompute) s.recompute(s);
}

} // namespace

// ---- Helpers ---------------------------------------------------------------

String calcTrim(const String &s) {
    String t = s;
    t.trim();
    return t;
}

bool calcParseDouble(const String &s, double &out) {
    String t = calcTrim(s);
    if (!t.length()) return false;
    char *end = nullptr;
    out = strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    // Allow k/M/m/u suffixes (M = mega, m = milli)
    if (*end) {
        char raw = *end;
        char c = (char)tolower((unsigned char)raw);
        if (c == 'k') {
            out *= 1e3;
            end++;
        } else if (raw == 'M') {
            out *= 1e6;
            end++;
        } else if (raw == 'm') {
            out *= 1e-3;
            end++;
        } else if (c == 'u') {
            out *= 1e-6;
            end++;
        } else if (c == 'g') {
            out *= 1e9;
            end++;
        } else {
            return false;
        }
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end) return false;
    }
    if (!isfinite(out)) return false;
    return true;
}

bool calcParseU64(const String &s, int base, uint64_t &out) {
    String t = calcTrim(s);
    if (!t.length()) return false;
    // strip 0x/0b/0o if present when base auto
    int b = base;
    if (t.startsWith("0x") || t.startsWith("0X")) {
        b = 16;
        t = t.substring(2);
    } else if (t.startsWith("0b") || t.startsWith("0B")) {
        b = 2;
        t = t.substring(2);
    } else if (t.startsWith("0o") || t.startsWith("0O")) {
        b = 8;
        t = t.substring(2);
    }
    if (b < 2 || b > 36) return false;
    if (!t.length()) return false;
    out = 0;
    for (size_t i = 0; i < t.length(); i++) {
        char c = t[i];
        if (c == ' ' || c == '_') continue;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'Z') d = 10 + (c - 'A');
        else return false;
        if (d >= b) return false;
        if (out > (UINT64_MAX - (uint64_t)d) / (uint64_t)b) return false;
        out = out * (uint64_t)b + (uint64_t)d;
    }
    return true;
}

void calcSetError(CalcSession &s, const char *msg) {
    s.results.clear();
    s.pagerCount = 1;
    s.pager = 0;
    s.status = msg ? msg : "error";
}

bool calcParseInt(const String &s, long &out) {
    String t = calcTrim(s);
    if (!t.length()) return false;
    char *end = nullptr;
    long v = strtol(t.c_str(), &end, 10);
    if (end == t.c_str()) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false; // reject decimals, suffixes, junk
    out = v;
    return true;
}

bool calcParseIntRange(const String &s, long &out, long lo, long hi) {
    if (!calcParseInt(s, out)) return false;
    return out >= lo && out <= hi;
}

bool calcParseDoubleRange(const String &s, double &out, double lo, double hi) {
    if (!calcParseDouble(s, out)) return false;
    if (!isfinite(out)) return false;
    return out >= lo && out <= hi;
}

bool calcParsePositive(const String &s, double &out) {
    if (!calcParseDouble(s, out)) return false;
    return isfinite(out) && out > 0.0;
}

bool calcParseNonNeg(const String &s, double &out) {
    if (!calcParseDouble(s, out)) return false;
    return isfinite(out) && out >= 0.0;
}

String calcFmtSig(double v, int sig) {
    if (!isfinite(v)) return "nan";
    if (v == 0.0) return "0";
    char buf[48];
    // Use enough digits then trim trailing zeros
    snprintf(buf, sizeof(buf), "%.*g", sig, v);
    String s(buf);
    return s;
}

String calcFmtEng(double v) {
    if (!isfinite(v)) return "nan";
    double a = fabs(v);
    const char *suf = "";
    double scale = 1.0;
    if (a >= 1e9) {
        scale = 1e9;
        suf = "G";
    } else if (a >= 1e6) {
        scale = 1e6;
        suf = "M";
    } else if (a >= 1e3) {
        scale = 1e3;
        suf = "k";
    } else if (a >= 1.0) {
        scale = 1.0;
        suf = "";
    } else if (a >= 1e-3) {
        scale = 1e-3;
        suf = "m";
    } else if (a >= 1e-6) {
        scale = 1e-6;
        suf = "u";
    } else if (a > 0) {
        scale = 1e-9;
        suf = "n";
    }
    return calcFmtSig(v / scale) + suf;
}

static String groupDigits(const String &digits, int group, bool fromRight) {
    String out;
    int n = digits.length();
    if (!n) return "0";
    if (fromRight) {
        int first = n % group;
        if (first == 0) first = group;
        int i = 0;
        while (i < n) {
            if (i) out += ' ';
            int take = (i == 0) ? first : group;
            out += digits.substring(i, i + take);
            i += take;
        }
    } else {
        for (int i = 0; i < n; i++) {
            if (i && (i % group) == 0) out += ' ';
            out += digits[i];
        }
    }
    return out;
}

String calcGroupBin(uint64_t v, int widthBits) {
    if (widthBits <= 0) {
        if (v == 0) return "0";
        widthBits = 0;
        uint64_t t = v;
        while (t) {
            widthBits++;
            t >>= 1;
        }
        if (widthBits < 1) widthBits = 1;
    }
    String digits;
    for (int i = widthBits - 1; i >= 0; i--) digits += ((v >> i) & 1) ? '1' : '0';
    return groupDigits(digits, 4, false);
}

String calcGroupOct(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llo", (unsigned long long)v);
    return groupDigits(String(buf), 3, true);
}

String calcGroupHex(uint64_t v, int nibbles) {
    char buf[24];
    if (nibbles > 0) snprintf(buf, sizeof(buf), "%0*llX", nibbles, (unsigned long long)v);
    else snprintf(buf, sizeof(buf), "%llX", (unsigned long long)v);
    return groupDigits(String(buf), 4, true);
}

String calcGroupHexStr(const String &hex) {
    String t;
    for (size_t i = 0; i < hex.length(); i++) {
        char c = hex[i];
        if (c == ' ' || c == '_') continue;
        t += (char)toupper((unsigned char)c);
    }
    return groupDigits(t.length() ? t : String("0"), 4, true);
}

static const double kE12[] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
static const double kE24[] = {1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
                              3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};

double calcNearestE(double ohms, const double *series, int n) {
    if (!(ohms > 0) || !isfinite(ohms)) return ohms;
    double best = ohms;
    double bestLog = 1e300;
    double logT = log10(ohms);
    for (int exp = -12; exp <= 12; exp++) {
        double decade = pow(10.0, (double)exp);
        for (int i = 0; i < n; i++) {
            double cand = series[i] * decade;
            double d = fabs(log10(cand) - logT);
            if (d < bestLog) {
                bestLog = d;
                best = cand;
            }
        }
    }
    return best;
}

double calcNearestE12(double ohms) { return calcNearestE(ohms, kE12, 12); }
double calcNearestE24(double ohms) { return calcNearestE(ohms, kE24, 24); }

double calcAwgDiameterMm(int gauge) {
    // d_mm = 0.127 * 92^((36-n)/39)
    return 0.127 * pow(92.0, (36.0 - (double)gauge) / 39.0);
}

double calcAwgAreaMm2(int gauge) {
    double d = calcAwgDiameterMm(gauge);
    return M_PI * (d * 0.5) * (d * 0.5);
}

double calcAwgOhmsPerMeter(int gauge) {
    double a_m2 = calcAwgAreaMm2(gauge) * 1e-6;
    if (a_m2 <= 0) return 0;
    return 1.724e-8 / a_m2;
}

int calcNearestAwg(double diameterMm) {
    if (!(diameterMm > 0)) return 0;
    int best = 0;
    double bestDiff = 1e300;
    for (int g = 0; g <= 40; g++) {
        double d = fabs(calcAwgDiameterMm(g) - diameterMm);
        if (d < bestDiff) {
            bestDiff = d;
            best = g;
        }
    }
    return best;
}

double calcResistivity(bool aluminum) { return aluminum ? 2.82e-8 : 1.724e-8; }

// ---- Session UI ------------------------------------------------------------

void runCalcSession(CalcSession &s) {
    if (s.onMode) s.onMode(s);
    if (s.recompute) s.recompute(s);

    bool showResults = isWide(); // wide always shows both; narrow starts on inputs
    int focus = 0;
    int scrollIn = 0;
    int scrollOut = 0;
    std::vector<FocusItem> focusItems;

    auto refreshFocus = [&]() {
        buildFocus(s, focusItems);
        if (focus < 0) focus = 0;
        if (focus >= (int)focusItems.size()) focus = (int)focusItems.size() - 1;
        if (focus < 0) focus = 0;
    };
    refreshFocus();

    auto doEdit = [&]() {
        if (focus < 0 || focus >= (int)focusItems.size()) return;
        FocusItem fi = focusItems[focus];
        if (fi.kind == FocusItem::Mode) {
            pickMode(s);
            refreshFocus();
        } else if (fi.kind == FocusItem::Units) {
            toggleUnits(s);
            refreshFocus();
        } else if (fi.kind == FocusItem::Field && fi.index < (int)s.fields.size()) {
            editField(s.fields[fi.index]);
            if (s.recompute) s.recompute(s);
        }
    };

    auto draw = [&]() {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar(s.title.c_str());

        const uint16_t bg = kvxConfig.bgColor;
        const uint16_t pri = kvxConfig.priColor;
        const uint16_t sec = kvxConfig.secColor;
        const int fsz = calcLabelFont();
        const int top = KVX_TOPBAR_H + 1;
        const int footY = uiFooterY(fsz);
        const int statusH = uiLineH(fsz) + 1;
        const int bodyBot = footY - statusH - 1;
        const int rh = rowH();
        const bool wide = isWide();

        // Chip row
        int chipY = top;
        int cx = 2;
        String modeLabel = s.modeNames.empty() ? String("-") : s.modeNames[s.mode];
        if (modeLabel.length() > 12) modeLabel = modeLabel.substring(0, 12);
        bool modeFocused = false;
        bool unitsFocused = false;
        if (!showResults || wide) {
            if (focus < (int)focusItems.size()) {
                if (focusItems[focus].kind == FocusItem::Mode) modeFocused = true;
                if (focusItems[focus].kind == FocusItem::Units) unitsFocused = true;
            }
        }
        drawChip(cx, chipY, modeLabel.c_str(), modeFocused);
        cx += chipW(modeLabel.c_str()) + 2;
        const char *uLab = kvxConfig.calcUseImperial ? "IMP" : "SI";
        drawChip(cx, chipY, uLab, unitsFocused);

        int listTop = chipY + chipH() + 2;

        auto drawFieldList = [&](int x, int y, int w, int h) {
            int visible = max(1, h / rh);
            // Keep focus field in view
            int fieldFocus = -1;
            if (!showResults || wide) {
                if (focus < (int)focusItems.size() && focusItems[focus].kind == FocusItem::Field)
                    fieldFocus = focusItems[focus].index;
            }
            if (fieldFocus >= 0) {
                if (fieldFocus < scrollIn) scrollIn = fieldFocus;
                if (fieldFocus >= scrollIn + visible) scrollIn = fieldFocus - visible + 1;
            }
            if (scrollIn < 0) scrollIn = 0;
            int maxScroll = max(0, (int)s.fields.size() - visible);
            if (scrollIn > maxScroll) scrollIn = maxScroll;

            int yy = y;
            for (int i = 0; i < visible; i++) {
                int idx = scrollIn + i;
                if (idx >= (int)s.fields.size()) break;
                bool foc = (!showResults || wide) && fieldFocus == idx;
                drawRow(x, yy, w, rh, s.fields[idx].label, s.fields[idx].value, foc);
                yy += rh;
            }
        };

        auto drawResultList = [&](int x, int y, int w, int h) {
            // Count display rows (hero takes 2)
            std::vector<int> map; // result index per visual slot start
            for (int i = 0; i < (int)s.results.size(); i++) {
                map.push_back(i);
            }
            int visible = max(1, h / rh);
            if (scrollOut < 0) scrollOut = 0;
            int maxScroll = max(0, (int)map.size() - visible);
            if (scrollOut > maxScroll) scrollOut = maxScroll;

            int yy = y;
            int drawn = 0;
            for (int i = scrollOut; i < (int)s.results.size() && drawn < visible; i++) {
                const CalcLine &ln = s.results[i];
                if (ln.hero && drawn == 0 && h >= rh + uiLineH(calcHeroFont())) {
                    drawHeroLine(x, yy, w, ln.label, ln.value);
                    yy += uiLineH(calcLabelFont()) + uiLineH(calcHeroFont()) + 1;
                    drawn += 2;
                } else {
                    drawRow(x, yy, w, rh, ln.label, ln.value, false);
                    yy += rh;
                    drawn++;
                }
            }
            if (s.pagerCount > 1) {
                char pg[24];
                snprintf(pg, sizeof(pg), "%d/%d", s.pager + 1, s.pagerCount);
                tft.setTextSize(fsz);
                tft.setTextColor(sec, bg);
                tft.drawRightString(pg, x + w - 2, y + h - uiLineH(fsz), 1);
            }
        };

        if (wide) {
            int gap = 4;
            int colW = (tftWidth - 4 - gap) / 2;
            int listH = bodyBot - listTop;
            drawFieldList(2, listTop, colW, listH);
            drawResultList(2 + colW + gap, listTop, colW, listH);
        } else if (!showResults) {
            drawFieldList(2, listTop, tftWidth - 4, bodyBot - listTop);
        } else {
            drawResultList(2, listTop, tftWidth - 4, bodyBot - listTop);
        }

        // Status
        tft.setTextSize(fsz);
        tft.setTextColor(sec, bg);
        String st = s.status.length() ? s.status : " ";
        int stMax = max(1, (tftWidth - 4) / uiCharW(fsz));
        if ((int)st.length() > stMax) st = st.substring(st.length() - stMax);
        tft.drawRightString(st, tftWidth - 2, footY - statusH, 1);

        // Footer
        tft.setTextSize(fsz);
        tft.setTextColor(sec, bg);
        const char *foot;
        if (wide) {
#ifdef HAS_KEYBOARD
            foot = "Ok=edit UpDn=field </>=mode Esc";
#else
            foot = "OK=edit UpDn=field hold=back";
#endif
        } else if (!showResults) {
#ifdef HAS_KEYBOARD
            foot = "Ok=edit UpDn=field Next=out Esc";
#else
            foot = "OK=edit UpDn Next=out hold=back";
#endif
        } else {
#ifdef HAS_KEYBOARD
            foot = "UpDn=scroll Prev=in Esc";
#else
            foot = "UpDn=scroll Prev=in hold=back";
#endif
        }
        tft.drawCentreString(foot, tftWidth / 2, footY, 1);
    };

    draw();

    for (;;) {
        if (forceHome) break;

#ifdef HAS_KEYBOARD
        if (check(EscPress)) {
            if (showResults && !isWide()) {
                showResults = false;
                draw();
                continue;
            }
            break;
        }
#else
        if (check(EscPress)) {
            if (showResults && !isWide()) {
                showResults = false;
                draw();
                continue;
            }
            break;
        }
#endif

#ifdef HAS_KEYBOARD
        keyStroke key = _getKeyPress();
        if (key.pressed && key.fn && key.exit_key) break;

        if (key.pressed) {
            bool handled = false;
            for (char c : key.word) {
                char lc = (char)tolower((unsigned char)c);
                if (lc == 'm') {
                    cycleMode(s);
                    refreshFocus();
                    handled = true;
                } else if (lc == 'u') {
                    toggleUnits(s);
                    refreshFocus();
                    handled = true;
                }
            }
            if (key.enter && (!showResults || isWide())) {
                doEdit();
                handled = true;
            }
            if (handled) {
                draw();
                continue;
            }
        }
#endif

        if (check(UpPress)) {
            if (!showResults || isWide()) {
                if (focus > 0) focus--;
                else focus = (int)focusItems.size() - 1;
            } else {
                scrollOut--;
            }
            draw();
            continue;
        }
        if (check(DownPress)) {
            if (!showResults || isWide()) {
                if (focus + 1 < (int)focusItems.size()) focus++;
                else focus = 0;
            } else {
                scrollOut++;
            }
            draw();
            continue;
        }
        if (check(NextPress)) {
            if (!isWide() && !showResults) {
                showResults = true;
                scrollOut = 0;
            } else if (showResults && s.pagerCount > 1) {
                s.pager = (s.pager + 1) % s.pagerCount;
                if (s.recompute) s.recompute(s);
            } else if (!showResults || isWide()) {
                // also allow Next to cycle mode on inputs
                cycleMode(s);
                refreshFocus();
            }
            draw();
            continue;
        }
        if (check(PrevPress)) {
            if (!isWide() && showResults) {
                if (s.pagerCount > 1 && s.pager > 0) {
                    s.pager--;
                    if (s.recompute) s.recompute(s);
                } else {
                    showResults = false;
                }
            } else if (showResults && s.pagerCount > 1) {
                s.pager = (s.pager - 1 + s.pagerCount) % s.pagerCount;
                if (s.recompute) s.recompute(s);
            }
            draw();
            continue;
        }
        if (check(SelPress)) {
            if (!showResults || isWide()) doEdit();
            draw();
            continue;
        }

        delay(20);
    }
    consumeNavFlags();
}

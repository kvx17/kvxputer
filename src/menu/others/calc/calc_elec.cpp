#include "calc_menu.h"
#include "calc_shell.h"

#include <cmath>
#include <cstdio>
#include <globals.h>

namespace {

static const char *kColors[] = {
    "black", "brown", "red", "orange", "yellow", "green", "blue", "violet", "grey", "white",
    "gold",  "silver"
};
static const int kColorN = 12;

static int colorIndex(const String &name) {
    String n = calcTrim(name);
    n.toLowerCase();
    for (int i = 0; i < kColorN; i++) {
        if (n == kColors[i]) return i;
    }
    // aliases
    if (n == "gray") return 8;
    if (n == "purple") return 7;
    return -1;
}

static double digitOf(int idx) {
    if (idx >= 0 && idx <= 9) return (double)idx;
    return -1;
}

static double multOf(int idx) {
    if (idx >= 0 && idx <= 9) return pow(10.0, (double)idx);
    if (idx == 10) return 0.1;  // gold
    if (idx == 11) return 0.01; // silver
    return -1;
}

static const char *tolOf(int idx) {
    switch (idx) {
        case 1: return "1%";
        case 2: return "2%";
        case 5: return "0.5%";
        case 6: return "0.25%";
        case 7: return "0.1%";
        case 8: return "0.05%";
        case 10: return "5%";
        case 11: return "10%";
        default: return "?";
    }
}

static const char *tempcoOf(int idx) {
    switch (idx) {
        case 0: return "250 ppm/K";
        case 1: return "100 ppm/K";
        case 2: return "50 ppm/K";
        case 3: return "15 ppm/K";
        case 4: return "25 ppm/K";
        case 5: return "20 ppm/K";
        case 6: return "10 ppm/K";
        case 7: return "5 ppm/K";
        case 8: return "1 ppm/K";
        default: return "?";
    }
}

static String fmtOhms(double r) {
    if (r >= 1e6) return calcFmtSig(r / 1e6) + "M";
    if (r >= 1e3) return calcFmtSig(r / 1e3) + "k";
    return calcFmtSig(r);
}

static bool parseOhmValue(const String &s, double &out) {
    String t = calcTrim(s);
    if (!t.length()) return false;
    // strip trailing ohm / R
    if (t.endsWith("ohm") || t.endsWith("Ohm") || t.endsWith("OHM")) t = t.substring(0, t.length() - 3);
    t.trim();
    if (t.endsWith("R") || t.endsWith("r")) t = t.substring(0, t.length() - 1);
    // Handle 4k7 style
    int kpos = -1;
    for (size_t i = 0; i < t.length(); i++) {
        char c = (char)tolower((unsigned char)t[i]);
        if (c == 'k' || c == 'm') {
            kpos = (int)i;
            break;
        }
    }
    if (kpos > 0 && kpos < (int)t.length() - 0) {
        char suf = (char)tolower((unsigned char)t[kpos]);
        String left = t.substring(0, kpos);
        String right = t.substring(kpos + 1);
        if (right.length() && right[0] >= '0' && right[0] <= '9') {
            // 4k7
            double whole, frac = 0;
            if (!calcParseDouble(left, whole)) return false;
            if (right.length() && !calcParseDouble(right, frac)) return false;
            double mag = (suf == 'm') ? 1e6 : 1e3;
            // frac digits: 4k7 = 4.7 * 1000
            int digits = right.length();
            out = (whole + frac / pow(10.0, digits)) * mag;
            return true;
        }
    }
    return calcParseDouble(t, out);
}

static void bandsFromOhms(double ohms, int digits, double tolPct, std::vector<String> &bands) {
    bands.clear();
    if (!(ohms > 0)) return;
    int exp = (int)floor(log10(ohms)) - (digits - 1);
    int imant = (int)lround(ohms / pow(10.0, exp));
    int maxMant = (int)pow(10.0, digits);
    int minMant = (int)pow(10.0, digits - 1);
    while (imant >= maxMant) {
        imant /= 10;
        exp++;
    }
    while (imant < minMant && imant > 0) {
        imant *= 10;
        exp--;
    }
    for (int d = digits - 1; d >= 0; d--) {
        int dig = (imant / (int)pow(10.0, d)) % 10;
        bands.push_back(kColors[dig]);
    }
    if (exp >= 0 && exp <= 9) bands.push_back(kColors[exp]);
    else if (exp == -1) bands.push_back("gold");
    else if (exp == -2) bands.push_back("silver");
    else bands.push_back("?");
    if (fabs(tolPct - 5.0) < 0.01) bands.push_back("gold");
    else if (fabs(tolPct - 10.0) < 0.01) bands.push_back("silver");
    else if (fabs(tolPct - 1.0) < 0.01) bands.push_back("brown");
    else if (fabs(tolPct - 2.0) < 0.01) bands.push_back("red");
    else bands.push_back("?");
}

static String joinBands(const std::vector<String> &b) {
    String o;
    for (size_t i = 0; i < b.size(); i++) {
        if (i) o += ' ';
        o += b[i];
    }
    return o;
}

static std::vector<String> colorChoices() {
    std::vector<String> c;
    for (int i = 0; i < kColorN; i++) c.push_back(kColors[i]);
    return c;
}

} // namespace

void calcResistor() {
    CalcSession s;
    s.title = "Resistor";
    s.modeNames = {"4-band", "5-band", "6-band", "type colors", "reverse"};
    auto cols = colorChoices();

    s.onMode = [cols](CalcSession &cs) {
        cs.fields.clear();
        auto addBand = [&](const char *lab, const char *def) {
            cs.fields.push_back({lab, def, CE_CHOICE, cols, 10});
        };
        if (cs.mode == 0) {
            addBand("digit1", "brown");
            addBand("digit2", "black");
            addBand("mult", "red");
            addBand("tol", "gold");
        } else if (cs.mode == 1) {
            addBand("digit1", "brown");
            addBand("digit2", "black");
            addBand("digit3", "black");
            addBand("mult", "red");
            addBand("tol", "brown");
        } else if (cs.mode == 2) {
            addBand("digit1", "brown");
            addBand("digit2", "black");
            addBand("digit3", "black");
            addBand("mult", "red");
            addBand("tol", "brown");
            addBand("tempco", "brown");
        } else if (cs.mode == 3) {
            cs.fields.push_back({"colors", "brown black red gold", CE_TEXT, {}, 64});
        } else {
            cs.fields.push_back({"value", "4.7k", CE_TEXT, {}, 20});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();

        if (cs.mode == 4) {
            double ohms;
            if (!parseOhmValue(cs.fields[0].value, ohms) || !(ohms > 0) || !isfinite(ohms)) {
                calcSetError(cs, "bad ohms value");
                return;
            }
            std::vector<String> b4, b5;
            bandsFromOhms(ohms, 2, 5.0, b4);
            bandsFromOhms(ohms, 3, 1.0, b5);
            cs.results.push_back({"ohms", fmtOhms(ohms), true});
            cs.results.push_back({"5% 4-band", joinBands(b4), false});
            cs.results.push_back({"1% 5-band", joinBands(b5), false});
            cs.results.push_back({"E12", fmtOhms(calcNearestE12(ohms)), false});
            cs.results.push_back({"E24", fmtOhms(calcNearestE24(ohms)), false});
            return;
        }

        std::vector<int> bands;
        if (cs.mode == 3) {
            String t = calcTrim(cs.fields[0].value);
            int start = 0;
            while (start <= (int)t.length()) {
                int sp = t.indexOf(' ', start);
                String tok = (sp < 0) ? t.substring(start) : t.substring(start, sp);
                tok.trim();
                if (tok.length()) {
                    int idx = colorIndex(tok);
                    if (idx < 0) {
                        calcSetError(cs, "unknown color name");
                        return;
                    }
                    bands.push_back(idx);
                }
                if (sp < 0) break;
                start = sp + 1;
            }
        } else {
            for (auto &f : cs.fields) {
                int idx = colorIndex(f.value);
                if (idx < 0) {
                    calcSetError(cs, "unknown color name");
                    return;
                }
                bands.push_back(idx);
            }
        }

        int n = (int)bands.size();
        if (n < 4 || n > 6) {
            calcSetError(cs, "need 4..6 bands");
            return;
        }
        int digs = (n >= 5) ? 3 : 2;
        double val = 0;
        for (int i = 0; i < digs; i++) {
            double d = digitOf(bands[i]);
            if (d < 0) {
                calcSetError(cs, "digit band can't be gold/silver");
                return;
            }
            val = val * 10.0 + d;
        }
        double mult = multOf(bands[digs]);
        if (mult < 0) {
            calcSetError(cs, "bad multiplier band");
            return;
        }
        double ohms = val * mult;
        const char *tol = tolOf(bands[digs + 1]);
        cs.results.push_back({"ohms", fmtOhms(ohms), true});
        cs.results.push_back({"tolerance", tol, false});
        if (n >= 6) cs.results.push_back({"tempco", tempcoOf(bands[5]), false});
        cs.results.push_back({"E12", fmtOhms(calcNearestE12(ohms)), false});
        cs.results.push_back({"E24", fmtOhms(calcNearestE24(ohms)), false});
    };

    runCalcSession(s);
}

void calcLed() {
    CalcSession s;
    s.title = "LED resistor";
    s.modeNames = {"series"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"Vs", "5", CE_NUM, {}, 10});
        cs.fields.push_back({"Vf", "2.0", CE_NUM, {}, 10});
        cs.fields.push_back({"I mA", "20", CE_NUM, {}, 10});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        double vs, vf, ima;
        if (!calcParsePositive(cs.fields[0].value, vs)) {
            calcSetError(cs, "Vs must be > 0");
            return;
        }
        if (!calcParseNonNeg(cs.fields[1].value, vf)) {
            calcSetError(cs, "Vf must be >= 0");
            return;
        }
        if (!calcParsePositive(cs.fields[2].value, ima)) {
            calcSetError(cs, "I mA must be > 0");
            return;
        }
        if (vf >= vs) {
            calcSetError(cs, "Vf must be < supply");
            return;
        }
        double i = ima / 1000.0;
        double r = (vs - vf) / i;
        if (r < 0) r = 0;
        double e12 = calcNearestE12(r);
        double e24 = calcNearestE24(r);
        double p = (vs - vf) * i;
        double iActual = (e24 > 0) ? (vs - vf) / e24 : 0;
        cs.results.push_back({"R", fmtOhms(r), true});
        cs.results.push_back({"E12", fmtOhms(e12), false});
        cs.results.push_back({"E24", fmtOhms(e24), false});
        cs.results.push_back({"P watts", calcFmtSig(p), false});
        cs.results.push_back({"I@E24 mA", calcFmtSig(iActual * 1000.0), false});
        if (p > 0.25) cs.status = "warn: P>0.25W";
    };

    runCalcSession(s);
}

void calcDivider() {
    CalcSession s;
    s.title = "Divider";
    s.modeNames = {"R1 R2 Vin", "Vout+R1", "ratio"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.pager = 0;
        cs.pagerCount = 1;
        if (cs.mode == 0) {
            cs.fields.push_back({"R1", "10k", CE_TEXT, {}, 16});
            cs.fields.push_back({"R2", "10k", CE_TEXT, {}, 16});
            cs.fields.push_back({"Vin", "5", CE_NUM, {}, 10});
        } else if (cs.mode == 1) {
            cs.fields.push_back({"Vin", "5", CE_NUM, {}, 10});
            cs.fields.push_back({"Vout", "3.3", CE_NUM, {}, 10});
            cs.fields.push_back({"R1", "10k", CE_TEXT, {}, 16});
        } else {
            cs.fields.push_back({"Vout/Vin", "0.5", CE_NUM, {}, 12});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        if (cs.mode == 0) {
            double r1, r2, vin;
            if (!parseOhmValue(cs.fields[0].value, r1) || !(r1 > 0) ||
                !parseOhmValue(cs.fields[1].value, r2) || !(r2 > 0)) {
                calcSetError(cs, "R1 R2 must be > 0");
                return;
            }
            if (!calcParsePositive(cs.fields[2].value, vin)) {
                calcSetError(cs, "Vin must be > 0");
                return;
            }
            double vout = vin * r2 / (r1 + r2);
            double i = vin / (r1 + r2);
            cs.results.push_back({"Vout", calcFmtSig(vout), true});
            cs.results.push_back({"I", calcFmtEng(i) + "A", false});
            cs.results.push_back({"P R1", calcFmtSig(i * i * r1) + "W", false});
            cs.results.push_back({"P R2", calcFmtSig(i * i * r2) + "W", false});
            return;
        }
        if (cs.mode == 1) {
            double vin, vout, r1;
            if (!calcParsePositive(cs.fields[0].value, vin)) {
                calcSetError(cs, "Vin must be > 0");
                return;
            }
            if (!calcParsePositive(cs.fields[1].value, vout)) {
                calcSetError(cs, "Vout must be > 0");
                return;
            }
            if (!parseOhmValue(cs.fields[2].value, r1) || !(r1 > 0)) {
                calcSetError(cs, "R1 must be > 0");
                return;
            }
            if (vout >= vin) {
                calcSetError(cs, "need 0 < Vout < Vin");
                return;
            }
            double r2 = r1 * vout / (vin - vout);
            double r2s = calcNearestE24(r2);
            double vouts = vin * r2s / (r1 + r2s);
            double i = vin / (r1 + r2s);
            cs.results.push_back({"R2", fmtOhms(r2), false});
            cs.results.push_back({"R2 E24", fmtOhms(r2s), true});
            cs.results.push_back({"Vout@E24", calcFmtSig(vouts), false});
            cs.results.push_back({"I", calcFmtEng(i) + "A", false});
            cs.results.push_back({"P R1", calcFmtSig(i * i * r1) + "W", false});
            cs.results.push_back({"P R2", calcFmtSig(i * i * r2s) + "W", false});
            return;
        }
        // ratio mode: pairs around 10k top
        double ratio;
        if (!calcParseDoubleRange(cs.fields[0].value, ratio, 1e-9, 1.0 - 1e-9)) {
            calcSetError(cs, "ratio must be between 0 and 1");
            return;
        }
        // Vout/Vin = R2/(R1+R2) => R2/R1 = ratio/(1-ratio)
        double rr = ratio / (1.0 - ratio);
        const double r1Target = 10000.0;
        struct Pair {
            double r1, r2;
            bool e24;
        };
        std::vector<Pair> pairs;
        auto addSeries = [&](bool e24) {
            const double *ser = e24 ? nullptr : nullptr;
            // generate candidates near r1Target
            for (int exp = 2; exp <= 5; exp++) {
                double decade = pow(10.0, exp);
                // use E12 or E24 mantissas
                int n = e24 ? 24 : 12;
                for (int i = 0; i < n; i++) {
                    double m = e24 ? calcNearestE24(decade * (1.0 + i * 0.01)) : calcNearestE12(decade);
                    // Better: iterate known series via nearest of target
                    (void)m;
                    (void)ser;
                }
            }
            double r1 = e24 ? calcNearestE24(r1Target) : calcNearestE12(r1Target);
            double r2ideal = r1 * rr;
            double r2 = e24 ? calcNearestE24(r2ideal) : calcNearestE12(r2ideal);
            pairs.push_back({r1, r2, e24});
            // a few neighbors
            for (double scale : {0.47, 0.68, 1.5, 2.2, 4.7}) {
                double r1b = e24 ? calcNearestE24(r1Target * scale) : calcNearestE12(r1Target * scale);
                double r2b = e24 ? calcNearestE24(r1b * rr) : calcNearestE12(r1b * rr);
                pairs.push_back({r1b, r2b, e24});
            }
        };
        addSeries(false);
        addSeries(true);
        cs.pagerCount = (int)pairs.size();
        if (cs.pagerCount < 1) cs.pagerCount = 1;
        if (cs.pager >= cs.pagerCount) cs.pager = cs.pagerCount - 1;
        if (cs.pager < 0) cs.pager = 0;
        const Pair &p = pairs[cs.pager];
        double actual = p.r2 / (p.r1 + p.r2);
        cs.results.push_back({"pair", String(cs.pager + 1) + "/" + String(cs.pagerCount), false});
        cs.results.push_back({"R1", fmtOhms(p.r1), false});
        cs.results.push_back({"R2", fmtOhms(p.r2), true});
        cs.results.push_back({"series", p.e24 ? "E24" : "E12", false});
        cs.results.push_back({"ratio", calcFmtSig(actual), false});
    };

    runCalcSession(s);
}

void calcDbm() {
    CalcSession s;
    s.title = "dBm power";
    s.modeNames = {"convert"};
    static const std::vector<String> units = {"dBm", "mW", "W"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"value", "0", CE_NUM, {}, 16});
        cs.fields.push_back({"unit", "dBm", CE_CHOICE, units, 4});
        cs.fields.push_back({"gain dB", "", CE_NUM, {}, 10});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        double v;
        if (!calcParseDouble(cs.fields[0].value, v) || !isfinite(v)) {
            calcSetError(cs, "need a number");
            return;
        }
        String u = cs.fields[1].value;
        double dbm;
        if (u == "dBm") {
            dbm = v;
        } else if (u == "mW") {
            if (!(v > 0)) {
                calcSetError(cs, "mW must be > 0");
                return;
            }
            dbm = 10.0 * log10(v);
        } else {
            if (!(v > 0)) {
                calcSetError(cs, "W must be > 0");
                return;
            }
            dbm = 10.0 * log10(v * 1000.0);
        }
        double gain = 0;
        String g = calcTrim(cs.fields[2].value);
        if (g.length()) {
            if (!calcParseDouble(g, gain) || !isfinite(gain)) {
                calcSetError(cs, "bad gain dB");
                return;
            }
            if (gain < -200.0 || gain > 200.0) {
                calcSetError(cs, "gain out of range");
                return;
            }
        }
        dbm += gain;
        double mw = pow(10.0, dbm / 10.0);
        double w = mw / 1000.0;
        cs.results.push_back({"dBm", calcFmtSig(dbm), true});
        cs.results.push_back({"mW", calcFmtSig(mw), false});
        cs.results.push_back({"W", calcFmtSig(w), false});
        if (dbm < -120.0 || dbm > 60.0) cs.status = "warn: outside bench range";
    };

    runCalcSession(s);
}

void calcOhm() {
    CalcSession s;
    s.title = "Ohm's law";
    s.modeNames = {"DC", "3-phase"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        if (cs.mode == 0) {
            cs.fields.push_back({"V", "", CE_NUM, {}, 12});
            cs.fields.push_back({"I", "", CE_NUM, {}, 12});
            cs.fields.push_back({"R", "", CE_NUM, {}, 12});
            cs.fields.push_back({"W", "", CE_NUM, {}, 12});
        } else {
            cs.fields.push_back({"V", "208", CE_NUM, {}, 12});
            cs.fields.push_back({"I", "10", CE_NUM, {}, 12});
            cs.fields.push_back({"pf", "0.85", CE_NUM, {}, 8});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        if (cs.mode == 1) {
            double v, i, pf;
            if (!calcParsePositive(cs.fields[0].value, v)) {
                calcSetError(cs, "V must be > 0");
                return;
            }
            if (!calcParsePositive(cs.fields[1].value, i)) {
                calcSetError(cs, "I must be > 0");
                return;
            }
            if (!calcParseDoubleRange(cs.fields[2].value, pf, 0.0, 1.0)) {
                calcSetError(cs, "pf must be 0..1");
                return;
            }
            double w = sqrt(3.0) * v * i * pf;
            cs.results.push_back({"W", calcFmtSig(w), true});
            cs.results.push_back({"hp", calcFmtSig(w / 745.7), false});
            return;
        }
        bool hasV = false, hasI = false, hasR = false, hasW = false;
        double V = 0, I = 0, R = 0, W = 0;
        String sv = calcTrim(cs.fields[0].value);
        String si = calcTrim(cs.fields[1].value);
        String sr = calcTrim(cs.fields[2].value);
        String sw = calcTrim(cs.fields[3].value);
        auto take = [](const String &s, double &out, bool &has, const char *name, CalcSession &cs) -> bool {
            if (!s.length()) return true;
            if (!calcParseDouble(s, out) || !isfinite(out)) {
                calcSetError(cs, (String("bad ") + name).c_str());
                return false;
            }
            if (out < 0) {
                calcSetError(cs, (String(name) + " must be >= 0").c_str());
                return false;
            }
            has = true;
            return true;
        };
        if (!take(sv, V, hasV, "V", cs)) return;
        if (!take(si, I, hasI, "I", cs)) return;
        if (!take(sr, R, hasR, "R", cs)) return;
        if (!take(sw, W, hasW, "W", cs)) return;
        int n = (int)hasV + (int)hasI + (int)hasR + (int)hasW;
        if (n < 2) {
            calcSetError(cs, "enter any two");
            return;
        }
        if (n > 2) {
            calcSetError(cs, "use only two fields");
            return;
        }
        if (hasV && hasI) {
            R = (I != 0) ? V / I : 0;
            W = V * I;
        } else if (hasV && hasR) {
            I = (R != 0) ? V / R : 0;
            W = (R != 0) ? V * V / R : 0;
        } else if (hasV && hasW) {
            I = (V != 0) ? W / V : 0;
            R = (W != 0) ? V * V / W : 0;
        } else if (hasI && hasR) {
            V = I * R;
            W = I * I * R;
        } else if (hasI && hasW) {
            V = (I != 0) ? W / I : 0;
            R = (I != 0) ? W / (I * I) : 0;
        } else if (hasR && hasW) {
            V = sqrt(W * R);
            I = (R != 0) ? sqrt(W / R) : 0;
        }
        cs.results.push_back({"V", calcFmtSig(V), !hasV});
        cs.results.push_back({"I", calcFmtSig(I), !hasI});
        cs.results.push_back({"R", calcFmtSig(R), !hasR});
        cs.results.push_back({"W", calcFmtSig(W), !hasW});
        cs.results.push_back({"hp", calcFmtSig(W / 745.7), false});
    };

    runCalcSession(s);
}

void calcVdrop() {
    CalcSession s;
    s.title = "V drop";
    s.modeNames = {"one-way"};
    static const std::vector<String> mats = {"copper", "aluminum"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        bool imp = kvxConfig.calcUseImperial;
        cs.fields.push_back({"material", "copper", CE_CHOICE, mats, 10});
        cs.fields.push_back({imp ? "length ft" : "length m", imp ? "50" : "15", CE_NUM, {}, 12});
        cs.fields.push_back({"amps", "10", CE_NUM, {}, 10});
        cs.fields.push_back({"volts", "120", CE_NUM, {}, 10});
        cs.fields.push_back({"AWG", "12", CE_NUM, {}, 4});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        bool alum = (cs.fields[0].value == "aluminum");
        double len, amps, volts;
        long gaugeL;
        if (!calcParsePositive(cs.fields[1].value, len)) {
            calcSetError(cs, "length must be > 0");
            return;
        }
        if (!calcParsePositive(cs.fields[2].value, amps)) {
            calcSetError(cs, "amps must be > 0");
            return;
        }
        if (!calcParsePositive(cs.fields[3].value, volts)) {
            calcSetError(cs, "volts must be > 0");
            return;
        }
        if (!calcParseIntRange(cs.fields[4].value, gaugeL, 0, 40)) {
            calcSetError(cs, "AWG must be integer 0..40");
            return;
        }
        double len_m = kvxConfig.calcUseImperial ? len * 0.3048 : len;
        int gauge = (int)gaugeL;
        double rho = calcResistivity(alum);
        double area = calcAwgAreaMm2(gauge) * 1e-6; // m^2
        if (area <= 0) {
            calcSetError(cs, "bad gauge");
            return;
        }
        // one-way length (plan: one-way, not round-trip)
        double r = rho * len_m / area;
        double drop = amps * r;
        double pct = drop / volts * 100.0;
        cs.results.push_back({"drop V", calcFmtSig(drop), true});
        cs.results.push_back({"drop %", calcFmtSig(pct), false});

        auto smallestUnder = [&](double limitPct) -> int {
            // highest AWG number still under limit (thinnest that works)
            int best = -1;
            for (int g = 40; g >= 0; g--) {
                double a = calcAwgAreaMm2(g) * 1e-6;
                if (a <= 0) continue;
                double d = amps * (rho * len_m / a);
                double p = d / volts * 100.0;
                if (p <= limitPct) {
                    best = g;
                    break; // first from thin side that works... wait
                }
            }
            // iterate thick to thin: we want highest AWG (thinnest) still under %
            best = -1;
            for (int g = 0; g <= 40; g++) {
                double a = calcAwgAreaMm2(g) * 1e-6;
                double d = amps * (rho * len_m / a);
                double p = d / volts * 100.0;
                if (p <= limitPct) best = g; // keep going to thinner
                else if (best >= 0) break;
            }
            return best;
        };
        int g3 = smallestUnder(3.0);
        int g5 = smallestUnder(5.0);
        cs.results.push_back({"under 3%", g3 >= 0 ? String(g3) + " AWG" : "none", false});
        cs.results.push_back({"under 5%", g5 >= 0 ? String(g5) + " AWG" : "none", false});
    };

    runCalcSession(s);
}

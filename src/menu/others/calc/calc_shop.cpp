#include "calc_menu.h"
#include "calc_shell.h"

#include "root/config/config.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <globals.h>

namespace {

struct UnitDef {
    const char *name;
    double toSi; // multiply to get SI base
};

struct DimDef {
    const char *name;
    const UnitDef *units;
    int n;
};

// SI bases: m, m^2, m^3, kg, N, Pa, J, W, K, m^3/s, N·m, m/s
static const UnitDef kLen[] = {
    {"m", 1.0}, {"cm", 0.01}, {"mm", 0.001}, {"km", 1000.0},
    {"in", 0.0254}, {"ft", 0.3048}, {"yd", 0.9144}, {"mil", 2.54e-5}, {"mi", 1609.344}
};
static const UnitDef kArea[] = {
    {"m2", 1.0}, {"cm2", 1e-4}, {"mm2", 1e-6}, {"in2", 0.00064516},
    {"ft2", 0.09290304}, {"acre", 4046.8564224}, {"circular mil", 5.067075e-10}
};
static const UnitDef kVol[] = {
    {"m3", 1.0}, {"L", 0.001}, {"mL", 1e-6}, {"gal", 0.003785411784},
    {"qt", 0.000946352946}, {"pt", 0.000473176473}, {"fl oz", 2.95735295625e-5},
    {"in3", 1.6387064e-5}, {"ft3", 0.028316846592}, {"board-foot", 0.002359737216}
};
static const UnitDef kMass[] = {
    {"kg", 1.0}, {"g", 0.001}, {"mg", 1e-6}, {"lb", 0.45359237}, {"oz", 0.028349523125}, {"ton", 907.18474}
};
static const UnitDef kForce[] = {
    {"N", 1.0}, {"kN", 1000.0}, {"lbf", 4.4482216152605}, {"kgf", 9.80665}
};
static const UnitDef kPress[] = {
    {"Pa", 1.0}, {"kPa", 1000.0}, {"MPa", 1e6}, {"bar", 1e5}, {"psi", 6894.757293168},
    {"atm", 101325.0}, {"inH2O", 249.08891}, {"mmHg", 133.322387415}
};
static const UnitDef kEnergy[] = {
    {"J", 1.0}, {"kJ", 1000.0}, {"cal", 4.184}, {"kcal", 4184.0},
    {"Wh", 3600.0}, {"kWh", 3.6e6}, {"BTU", 1055.05585262}
};
static const UnitDef kPower[] = {
    {"W", 1.0}, {"kW", 1000.0}, {"hp", 745.7}, {"BTU/h", 0.29307107}
};
static const UnitDef kTemp[] = {
    {"C", 0}, {"F", 0}, {"K", 0} // special-cased
};
static const UnitDef kFlow[] = {
    {"m3/s", 1.0}, {"L/min", 1.6666666667e-5}, {"L/s", 0.001},
    {"gal/min", 6.30901964e-5}, {"ft3/min", 0.00047194745}
};
static const UnitDef kTorque[] = {
    {"N·m", 1.0}, {"N.m", 1.0}, {"lb·ft", 1.3558179483314}, {"lb.ft", 1.3558179483314}, {"oz·in", 0.0070615518}
};
static const UnitDef kSpeed[] = {
    {"m/s", 1.0}, {"km/h", 1.0 / 3.6}, {"mph", 0.44704}, {"ft/s", 0.3048}, {"knot", 0.514444}
};

static const DimDef kDims[] = {
    {"length", kLen, (int)(sizeof(kLen) / sizeof(kLen[0]))},
    {"area", kArea, (int)(sizeof(kArea) / sizeof(kArea[0]))},
    {"volume", kVol, (int)(sizeof(kVol) / sizeof(kVol[0]))},
    {"mass", kMass, (int)(sizeof(kMass) / sizeof(kMass[0]))},
    {"force", kForce, (int)(sizeof(kForce) / sizeof(kForce[0]))},
    {"pressure", kPress, (int)(sizeof(kPress) / sizeof(kPress[0]))},
    {"energy", kEnergy, (int)(sizeof(kEnergy) / sizeof(kEnergy[0]))},
    {"power", kPower, (int)(sizeof(kPower) / sizeof(kPower[0]))},
    {"temperature", kTemp, 3},
    {"flow", kFlow, (int)(sizeof(kFlow) / sizeof(kFlow[0]))},
    {"torque", kTorque, (int)(sizeof(kTorque) / sizeof(kTorque[0]))},
    {"speed", kSpeed, (int)(sizeof(kSpeed) / sizeof(kSpeed[0]))},
};
static const int kDimN = (int)(sizeof(kDims) / sizeof(kDims[0]));

static double tempToK(double v, const String &u) {
    if (u == "K") return v;
    if (u == "C") return v + 273.15;
    if (u == "F") return (v - 32.0) * 5.0 / 9.0 + 273.15;
    return v;
}

static double tempFromK(double k, const String &u) {
    if (u == "K") return k;
    if (u == "C") return k - 273.15;
    if (u == "F") return (k - 273.15) * 9.0 / 5.0 + 32.0;
    return k;
}

static bool parseDateTime(const String &s, time_t &out) {
    String t = calcTrim(s);
    int y, mo, d, h = 0, mi = 0, se = 0;
    int n = sscanf(t.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (n < 3) return false;
    if (n == 3) {
        h = 0;
        mi = 0;
        se = 0;
    }
    // Portable UTC: days since epoch (no mktime timezone dependency).
    auto isLeap = [](int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    };
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12 || d < 1) return false;
    int dim = mdays[mo - 1];
    if (mo == 2 && isLeap(y)) dim = 29;
    if (d > dim) return false;
    int64_t days = 0;
    for (int year = 1970; year < y; year++) days += isLeap(year) ? 366 : 365;
    for (int m = 1; m < mo; m++) {
        days += mdays[m - 1];
        if (m == 2 && isLeap(y)) days += 1;
    }
    days += d - 1;
    out = (time_t)(days * 86400LL + h * 3600LL + mi * 60LL + se);
    return true;
}

static String fmtIso(time_t sec) {
    struct tm tm;
    gmtime_r(&sec, &tm);
    char buf[32];
    snprintf(
        buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec
    );
    return String(buf);
}

static String fmtLocal(time_t sec) {
    double offH = kvxConfig.tmz;
    if (kvxConfig.dst) offH += 1.0;
    time_t local = sec + (time_t)lround(offH * 3600.0);
    struct tm tm;
    gmtime_r(&local, &tm);
    char buf[40];
    snprintf(
        buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec
    );
    return String(buf);
}

} // namespace

void calcEpoch() {
    CalcSession s;
    s.title = "Epoch";
    s.modeNames = {"unix s", "unix ms", "date-time"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        if (cs.mode == 0) cs.fields.push_back({"seconds", "0", CE_NUM, {}, 16});
        else if (cs.mode == 1) cs.fields.push_back({"millis", "0", CE_NUM, {}, 18});
        else cs.fields.push_back({"date", "1970-01-01 00:00:00", CE_TEXT, {}, 24});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        time_t sec = 0;
        int64_t ms = 0;
        if (cs.mode == 0) {
            long v;
            if (!calcParseInt(cs.fields[0].value, v) || v < 0) {
                calcSetError(cs, "seconds must be integer >= 0");
                return;
            }
            sec = (time_t)v;
            ms = (int64_t)v * 1000;
        } else if (cs.mode == 1) {
            long v;
            if (!calcParseInt(cs.fields[0].value, v) || v < 0) {
                calcSetError(cs, "millis must be integer >= 0");
                return;
            }
            ms = (int64_t)v;
            sec = (time_t)(ms / 1000);
        } else {
            if (!parseDateTime(cs.fields[0].value, sec)) {
                calcSetError(cs, "bad date YYYY-MM-DD HH:MM:SS");
                return;
            }
            ms = (int64_t)sec * 1000;
        }
        cs.results.push_back({"seconds", String((long long)sec), true});
        cs.results.push_back({"millis", String((long long)ms), false});
        cs.results.push_back({"UTC ISO", fmtIso(sec), false});
        if (kvxConfig.tmz == 0 && !kvxConfig.dst) {
            cs.results.push_back({"local", fmtLocal(sec) + " (=UTC zone not set)", false});
        } else {
            cs.results.push_back({"local", fmtLocal(sec), false});
        }
    };

    runCalcSession(s);
}

void calcMeasure() {
    CalcSession s;
    s.title = "Measure";
    s.modeNames = {"temp", "length", "AWG"};
    static const std::vector<String> tempU = {"C", "F", "K"};
    static const std::vector<String> lenU = {"mm", "inch", "mil"};
    static const std::vector<String> diaU = {"mm", "inch", "mil"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        if (cs.mode == 0) {
            cs.fields.push_back({"value", "25", CE_NUM, {}, 12});
            cs.fields.push_back({"unit", kvxConfig.calcUseImperial ? "F" : "C", CE_CHOICE, tempU, 2});
        } else if (cs.mode == 1) {
            cs.fields.push_back({"value", "25.4", CE_NUM, {}, 12});
            cs.fields.push_back({"unit", kvxConfig.calcUseImperial ? "inch" : "mm", CE_CHOICE, lenU, 6});
        } else {
            cs.fields.push_back({"input", "gauge", CE_CHOICE, {"gauge", "diameter"}, 10});
            cs.fields.push_back({"value", "24", CE_NUM, {}, 12});
            cs.fields.push_back({"dia unit", "mm", CE_CHOICE, diaU, 6});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        if (cs.mode == 0) {
            double v;
            if (!calcParseDouble(cs.fields[0].value, v) || !isfinite(v)) {
                calcSetError(cs, "need temperature");
                return;
            }
            String u = cs.fields[1].value;
            double k = tempToK(v, u);
            if (!(k > 0)) {
                calcSetError(cs, "temp must be > 0 K");
                return;
            }
            cs.results.push_back({"C", calcFmtSig(tempFromK(k, "C")), u == "C"});
            cs.results.push_back({"F", calcFmtSig(tempFromK(k, "F")), u == "F"});
            cs.results.push_back({"K", calcFmtSig(tempFromK(k, "K")), u == "K"});
            return;
        }
        if (cs.mode == 1) {
            double v;
            if (!calcParsePositive(cs.fields[0].value, v)) {
                calcSetError(cs, "length must be > 0");
                return;
            }
            String u = cs.fields[1].value;
            double mm;
            if (u == "mm") mm = v;
            else if (u == "inch") mm = v * 25.4;
            else mm = v * 0.0254; // mil
            cs.results.push_back({"mm", calcFmtSig(mm), u == "mm"});
            cs.results.push_back({"inch", calcFmtSig(mm / 25.4), u == "inch"});
            cs.results.push_back({"mil", calcFmtSig(mm / 0.0254), u == "mil"});
            return;
        }
        // AWG
        bool asGauge = (cs.fields[0].value == "gauge");
        int g;
        if (asGauge) {
            long gl;
            if (!calcParseIntRange(cs.fields[1].value, gl, 0, 40)) {
                calcSetError(cs, "AWG must be integer 0..40");
                return;
            }
            g = (int)gl;
        } else {
            double v;
            if (!calcParsePositive(cs.fields[1].value, v)) {
                calcSetError(cs, "diameter must be > 0");
                return;
            }
            String u = cs.fields[2].value;
            double mm;
            if (u == "mm") mm = v;
            else if (u == "inch") mm = v * 25.4;
            else mm = v * 0.0254;
            g = calcNearestAwg(mm);
        }
        if (g < 0) g = 0;
        if (g > 40) g = 40;
        cs.results.push_back({"AWG", String(g), true});
        cs.results.push_back({"neighbors", String(g - 1) + " / " + String(g + 1), false});
        cs.results.push_back({"dia mm", calcFmtSig(calcAwgDiameterMm(g)), false});
        cs.results.push_back({"area mm2", calcFmtSig(calcAwgAreaMm2(g)), false});
        cs.results.push_back({"ohm/m Cu", calcFmtSig(calcAwgOhmsPerMeter(g)), false});
    };

    runCalcSession(s);
}

void calcUnits() {
    CalcSession s;
    s.title = "Units";
    s.modeNames = {"convert"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        std::vector<String> dims;
        for (int i = 0; i < kDimN; i++) dims.push_back(kDims[i].name);
        int dim = kvxConfig.calcUnitDim;
        if (dim < 0 || dim >= kDimN) dim = 0;
        int from = kvxConfig.calcUnitFrom;
        int to = kvxConfig.calcUnitTo;
        if (from < 0 || from >= kDims[dim].n) from = 0;
        if (to < 0 || to >= kDims[dim].n) to = min(1, kDims[dim].n - 1);

        std::vector<String> unitNames;
        for (int i = 0; i < kDims[dim].n; i++) unitNames.push_back(kDims[dim].units[i].name);

        cs.fields.push_back({"value", "1", CE_NUM, {}, 20});
        cs.fields.push_back({"dimension", kDims[dim].name, CE_CHOICE, dims, 16});
        cs.fields.push_back({"from", kDims[dim].units[from].name, CE_CHOICE, unitNames, 16});
        cs.fields.push_back({"to", kDims[dim].units[to].name, CE_CHOICE, unitNames, 16});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        // Rebuild unit choices if dimension changed
        String dimName = cs.fields[1].value;
        int dim = 0;
        for (int i = 0; i < kDimN; i++) {
            if (dimName == kDims[i].name) {
                dim = i;
                break;
            }
        }
        // Refresh from/to choice lists
        std::vector<String> unitNames;
        for (int i = 0; i < kDims[dim].n; i++) unitNames.push_back(kDims[dim].units[i].name);
        cs.fields[2].choices = unitNames;
        cs.fields[3].choices = unitNames;
        // If current from/to not in dim, reset
        bool fromOk = false, toOk = false;
        int fromIdx = 0, toIdx = 1;
        for (int i = 0; i < kDims[dim].n; i++) {
            if (cs.fields[2].value == kDims[dim].units[i].name) {
                fromOk = true;
                fromIdx = i;
            }
            if (cs.fields[3].value == kDims[dim].units[i].name) {
                toOk = true;
                toIdx = i;
            }
        }
        if (!fromOk) {
            cs.fields[2].value = kDims[dim].units[0].name;
            fromIdx = 0;
        }
        if (!toOk) {
            toIdx = min(1, kDims[dim].n - 1);
            cs.fields[3].value = kDims[dim].units[toIdx].name;
        }

        if (kvxConfig.calcUnitDim != dim || kvxConfig.calcUnitFrom != fromIdx ||
            kvxConfig.calcUnitTo != toIdx) {
            kvxConfig.calcUnitDim = dim;
            kvxConfig.calcUnitFrom = fromIdx;
            kvxConfig.calcUnitTo = toIdx;
            kvxConfig.saveFile();
        }

        double v;
        if (!calcParseDouble(cs.fields[0].value, v) || !isfinite(v)) {
            calcSetError(cs, "need a number");
            return;
        }
        String from = cs.fields[2].value;
        String to = cs.fields[3].value;
        double result;
        if (dimName == "temperature") {
            double k = tempToK(v, from);
            if (!(k > 0)) {
                calcSetError(cs, "temp must be > 0 K");
                return;
            }
            result = tempFromK(k, to);
        } else {
            if (!(v >= 0)) {
                calcSetError(cs, "value must be >= 0");
                return;
            }
            double toSi = 1, fromSi = 1;
            bool foundFrom = false, foundTo = false;
            for (int i = 0; i < kDims[dim].n; i++) {
                if (from == kDims[dim].units[i].name) {
                    fromSi = kDims[dim].units[i].toSi;
                    foundFrom = true;
                }
                if (to == kDims[dim].units[i].name) {
                    toSi = kDims[dim].units[i].toSi;
                    foundTo = true;
                }
            }
            if (!foundFrom || !foundTo || toSi == 0) {
                calcSetError(cs, "unknown unit");
                return;
            }
            result = v * fromSi / toSi;
        }
        cs.results.push_back({to, calcFmtSig(result), true});
    };

    runCalcSession(s);
}

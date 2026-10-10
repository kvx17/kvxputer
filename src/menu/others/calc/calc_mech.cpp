#include "calc_menu.h"
#include "calc_shell.h"

#include "root/config/config.h"
#include <cmath>
#include <cstdio>
#include <globals.h>

namespace {

struct GasDens {
    const char *name;
    double kg_m3; // at STP approx 0 C 101.325 kPa — used as relative density ref
    double molarMass; // g/mol for ideal gas mass from P V / (R T)
};

// Use molar mass for ideal-gas mass mode
static const GasDens kGases[] = {
    {"air", 1.292, 28.97},
    {"nitrogen", 1.250, 28.0134},
    {"oxygen", 1.429, 31.998},
    {"CO2", 1.977, 44.01},
    {"argon", 1.784, 39.948},
    {"methane", 0.717, 16.04},
};
static const int kGasN = 6;

static const double kAtmKpa = 101.325;
static const double kR_univ = 8.314462618; // J/(mol·K)
static const double kR_air = 287.05;       // J/(kg·K)

} // namespace

void calcMotor() {
    CalcSession s;
    s.title = "Motor";
    s.modeNames = {"hp/torque/rpm"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        bool imp = kvxConfig.calcUseImperial;
        cs.fields.push_back({"hp", "", CE_NUM, {}, 12});
        cs.fields.push_back({imp ? "torque lb·ft" : "torque N·m", "", CE_NUM, {}, 12});
        cs.fields.push_back({"RPM", "", CE_NUM, {}, 12});
        cs.fields.push_back({"eff %", "", CE_NUM, {}, 8});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        bool imp = kvxConfig.calcUseImperial;
        bool hasHp = false, hasT = false, hasRpm = false;
        double hp = 0, t = 0, rpm = 0;
        String shp = calcTrim(cs.fields[0].value);
        String st = calcTrim(cs.fields[1].value);
        String sr = calcTrim(cs.fields[2].value);
        auto takePos = [&](const String &s, double &out, bool &has, const char *name) -> bool {
            if (!s.length()) return true;
            if (!calcParsePositive(s, out)) {
                calcSetError(cs, (String(name) + " must be > 0").c_str());
                return false;
            }
            has = true;
            return true;
        };
        if (!takePos(shp, hp, hasHp, "hp")) return;
        if (!takePos(st, t, hasT, "torque")) return;
        if (!takePos(sr, rpm, hasRpm, "RPM")) return;
        int n = (int)hasHp + (int)hasT + (int)hasRpm;
        if (n < 2) {
            calcSetError(cs, "enter any two");
            return;
        }
        if (n > 2) {
            calcSetError(cs, "use only two fields");
            return;
        }
        // Work in lb·ft and hp: hp = T_lbft * rpm / 5252
        double t_lbft = t;
        if (!imp) t_lbft = t / 1.3558179483314; // N·m -> lb·ft
        if (hasHp && hasRpm) {
            t_lbft = hp * 5252.0 / rpm;
            t = imp ? t_lbft : t_lbft * 1.3558179483314;
        } else if (hasHp && hasT) {
            rpm = hp * 5252.0 / t_lbft;
        } else if (hasT && hasRpm) {
            hp = t_lbft * rpm / 5252.0;
        }
        cs.results.push_back({"hp", calcFmtSig(hp), !hasHp});
        cs.results.push_back({imp ? "lb·ft" : "N·m", calcFmtSig(t), !hasT});
        cs.results.push_back({"RPM", calcFmtSig(rpm), !hasRpm});

        String se = calcTrim(cs.fields[3].value);
        if (se.length()) {
            double eff;
            if (!calcParseDoubleRange(se, eff, 1e-9, 100.0)) {
                calcSetError(cs, "eff must be 0..100 %");
                return;
            }
            double shaftW = hp * 745.7;
            double elecW = shaftW / (eff / 100.0);
            cs.results.push_back({"shaft W", calcFmtSig(shaftW), false});
            cs.results.push_back({"elec W", calcFmtSig(elecW), false});
            cs.results.push_back({"shaft hp", calcFmtSig(hp), false});
        }
    };

    runCalcSession(s);
}

void calcGas() {
    CalcSession s;
    s.title = "Gas law";
    s.modeNames = {"Boyle/Charles", "mass", "air"};
    static std::vector<String> gases;
    if (gases.empty()) {
        for (int i = 0; i < kGasN; i++) gases.push_back(kGases[i].name);
    }
    static const std::vector<String> pMode = {"gauge", "absolute"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        bool imp = kvxConfig.calcUseImperial;
        if (cs.mode == 2) {
            cs.fields.push_back({imp ? "temp F" : "temp C", imp ? "68" : "20", CE_NUM, {}, 10});
            return;
        }
        cs.fields.push_back({"P type", "gauge", CE_CHOICE, pMode, 10});
        cs.fields.push_back({imp ? "P psi" : "P kPa", "", CE_NUM, {}, 12});
        cs.fields.push_back({imp ? "T F" : "T C", "", CE_NUM, {}, 10});
        cs.fields.push_back({"V L", "", CE_NUM, {}, 12});
        if (cs.mode == 1) {
            cs.fields.push_back({"gas", "air", CE_CHOICE, gases, 12});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        bool imp = kvxConfig.calcUseImperial;

        if (cs.mode == 2) {
            double t;
            if (!calcParseDouble(cs.fields[0].value, t) || !isfinite(t)) {
                calcSetError(cs, "need temperature");
                return;
            }
            double c = imp ? (t - 32.0) * 5.0 / 9.0 : t;
            double k = c + 273.15;
            if (!(k > 0)) {
                calcSetError(cs, "temp must be > 0 K");
                return;
            }
            double dens = kAtmKpa * 1000.0 / (kR_air * k); // kg/m3 at 1 atm
            double sos = sqrt(1.4 * kR_air * k);
            cs.results.push_back({"density", calcFmtSig(dens) + " kg/m3", true});
            cs.results.push_back({"speed sound", calcFmtSig(sos) + " m/s", false});
            return;
        }

        bool hasP = false, hasT = false, hasV = false;
        double p_in = 0, t_in = 0, v_l = 0;
        String sp = calcTrim(cs.fields[1].value);
        String st = calcTrim(cs.fields[2].value);
        String sv = calcTrim(cs.fields[3].value);
        auto takeNum = [&](const String &s, double &out, bool &has, const char *name) -> bool {
            if (!s.length()) return true;
            if (!calcParseDouble(s, out) || !isfinite(out)) {
                calcSetError(cs, (String("bad ") + name).c_str());
                return false;
            }
            has = true;
            return true;
        };
        if (!takeNum(sp, p_in, hasP, "P")) return;
        if (!takeNum(st, t_in, hasT, "T")) return;
        if (!takeNum(sv, v_l, hasV, "V")) return;
        int n = (int)hasP + (int)hasT + (int)hasV;
        if (n != 2) {
            calcSetError(cs, "leave exactly one of P/T/V blank");
            return;
        }
        bool gauge = (cs.fields[0].value == "gauge");
        // Convert to absolute kPa, Kelvin, m^3
        auto toAbsKpa = [&](double p) {
            double kpa = imp ? p * 6.894757293168 : p;
            return gauge ? kpa + kAtmKpa : kpa;
        };
        auto fromAbsKpa = [&](double absKpa) {
            double kpa = gauge ? absKpa - kAtmKpa : absKpa;
            return imp ? kpa / 6.894757293168 : kpa;
        };
        auto toK = [&](double t) {
            double c = imp ? (t - 32.0) * 5.0 / 9.0 : t;
            return c + 273.15;
        };
        auto fromK = [&](double k) {
            double c = k - 273.15;
            return imp ? c * 9.0 / 5.0 + 32.0 : c;
        };

        double P = hasP ? toAbsKpa(p_in) : 0;
        double T = hasT ? toK(t_in) : 0;
        double V = hasV ? v_l / 1000.0 : 0; // L -> m3

        // Combined gas: P V / T = const. With two known, solve third.
        // For Boyle/Charles without a reference state, we treat the blank as
        // "solve using atmospheric reference": actually for missing one of three
        // with no fourth constant we need a reference. Spec: "Solve for the
        // missing one with Boyle/Charles" — implies two of three with the
        // relationship holding against a unit amount: use P1V1/T1 form where
        // the known two define the state and we... wait, with only 2 of 3 you
        // cannot solve without a constant.
        //
        // Practical approach used by shop calculators: require an initial state
        // OR treat as ideal gas with n=1 mol when mass mode isn't on.
        // Spec says: "One of P/T/V is blank and is the unknown."
        // That only works with a fixed n. Use n = 1 mol for Boyle/Charles mode.
        double nMol = 1.0;
        if (cs.mode == 1) {
            // mass mode will compute mass from n after; still need n from density list
            // First solve state with n=1 then scale? Better: solve missing, then mass = P V M / (R T)
            nMol = 1.0;
        }

        if (hasP) {
            double absCheck = gauge ? (imp ? p_in * 6.894757293168 : p_in) + kAtmKpa
                                    : (imp ? p_in * 6.894757293168 : p_in);
            if (!(absCheck > 0)) {
                calcSetError(cs, "absolute P must be > 0");
                return;
            }
        }
        if (hasT) {
            double kCheck = toK(t_in);
            if (!(kCheck > 0)) {
                calcSetError(cs, "temp must be > 0 K");
                return;
            }
        }
        if (hasV && !(v_l > 0)) {
            calcSetError(cs, "volume must be > 0");
            return;
        }

        if (!hasP) {
            if (!(T > 0) || !(V > 0)) {
                calcSetError(cs, "need T and V > 0");
                return;
            }
            P = nMol * kR_univ * T / V / 1000.0; // kPa
        } else if (!hasT) {
            if (!(P > 0) || !(V > 0)) {
                calcSetError(cs, "need P and V > 0");
                return;
            }
            T = P * 1000.0 * V / (nMol * kR_univ);
        } else if (!hasV) {
            if (!(P > 0) || !(T > 0)) {
                calcSetError(cs, "need P and T > 0");
                return;
            }
            V = nMol * kR_univ * T / (P * 1000.0);
        }

        double pAbs = P;
        double pGauge = P - kAtmKpa;
        double pDisp = fromAbsKpa(P);
        double tDisp = fromK(T);
        double vDisp = V * 1000.0;

        cs.results.push_back({!hasP ? "P (solved)" : "P", calcFmtSig(pDisp) + (imp ? " psi" : " kPa"), !hasP});
        cs.results.push_back({!hasT ? "T (solved)" : "T", calcFmtSig(tDisp) + (imp ? " F" : " C"), !hasT});
        cs.results.push_back({!hasV ? "V (solved)" : "V", calcFmtSig(vDisp) + " L", !hasV});
        cs.results.push_back({"P abs", calcFmtSig(pAbs) + " kPa", false});
        cs.results.push_back({"P gauge", calcFmtSig(pGauge) + " kPa", false});
        cs.results.push_back({"atm", calcFmtSig(kAtmKpa) + " kPa", false});

        if (cs.mode == 1) {
            String gname = cs.fields[4].value;
            double M = 28.97;
            for (int i = 0; i < kGasN; i++) {
                if (gname == kGases[i].name) {
                    M = kGases[i].molarMass;
                    break;
                }
            }
            // mass from ideal gas: m = P V M / (R T) with P in Pa, M in g/mol -> grams
            double mass_g = (pAbs * 1000.0) * V * M / (kR_univ * T);
            cs.results.push_back({"mass", calcFmtSig(mass_g) + " g", true});
            cs.results.push_back({"mass kg", calcFmtSig(mass_g / 1000.0), false});
        }
    };

    runCalcSession(s);
}

void calcFlow() {
    CalcSession s;
    s.title = "Flow/pipe";
    s.modeNames = {"Q from D,v", "D/v from Q", "pipe volume", "Darcy"};
    static const std::vector<String> rough = {"PVC", "copper", "steel"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        bool imp = kvxConfig.calcUseImperial;
        if (cs.mode == 0) {
            cs.fields.push_back({imp ? "ID in" : "ID mm", imp ? "1" : "25", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "vel ft/s" : "vel m/s", imp ? "5" : "1.5", CE_NUM, {}, 10});
        } else if (cs.mode == 1) {
            cs.fields.push_back({imp ? "Q GPM" : "Q L/min", "10", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "ID in" : "ID mm", "", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "vel ft/s" : "vel m/s", "", CE_NUM, {}, 10});
        } else if (cs.mode == 2) {
            cs.fields.push_back({imp ? "ID in" : "ID mm", imp ? "1" : "25", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "len ft" : "len m", imp ? "100" : "30", CE_NUM, {}, 10});
        } else {
            cs.fields.push_back({imp ? "ID in" : "ID mm", imp ? "1" : "25", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "len ft" : "len m", imp ? "100" : "30", CE_NUM, {}, 10});
            cs.fields.push_back({imp ? "Q GPM" : "Q L/min", "10", CE_NUM, {}, 10});
            cs.fields.push_back({"roughness", "PVC", CE_CHOICE, rough, 10});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        bool imp = kvxConfig.calcUseImperial;

        auto toM = [&](double d) { return imp ? d * 0.0254 : d / 1000.0; }; // ID in or mm -> m
        auto toMs = [&](double v) { return imp ? v * 0.3048 : v; };
        auto toLenM = [&](double L) { return imp ? L * 0.3048 : L; };
        auto toM3s = [&](double q) {
            return imp ? q * 6.30901964e-5 : q / 60000.0; // GPM or L/min
        };

        if (cs.mode == 0) {
            double id, vel;
            if (!calcParsePositive(cs.fields[0].value, id)) {
                calcSetError(cs, "ID must be > 0");
                return;
            }
            if (!calcParsePositive(cs.fields[1].value, vel)) {
                calcSetError(cs, "velocity must be > 0");
                return;
            }
            double D = toM(id);
            double v = toMs(vel);
            double area = M_PI * (D * 0.5) * (D * 0.5);
            double q = area * v; // m3/s
            double lpm = q * 60000.0;
            double gpm = q / 6.30901964e-5;
            cs.results.push_back({"L/min", calcFmtSig(lpm), !imp});
            cs.results.push_back({"gal/min", calcFmtSig(gpm), imp});
            return;
        }
        if (cs.mode == 1) {
            double qv;
            if (!calcParsePositive(cs.fields[0].value, qv)) {
                calcSetError(cs, "Q must be > 0");
                return;
            }
            double q = toM3s(qv);
            String sid = calcTrim(cs.fields[1].value);
            String svel = calcTrim(cs.fields[2].value);
            bool hasD = sid.length() > 0;
            bool hasV = svel.length() > 0;
            if (hasD == hasV) {
                calcSetError(cs, "fill ID or vel (not both)");
                return;
            }
            if (hasD) {
                double id;
                if (!calcParsePositive(sid, id)) {
                    calcSetError(cs, "ID must be > 0");
                    return;
                }
                double D = toM(id);
                double area = M_PI * (D * 0.5) * (D * 0.5);
                if (area <= 0) {
                    calcSetError(cs, "bad ID");
                    return;
                }
                double v = q / area;
                cs.results.push_back({imp ? "vel ft/s" : "vel m/s", calcFmtSig(imp ? v / 0.3048 : v), true});
            } else {
                double vel;
                if (!calcParsePositive(svel, vel)) {
                    calcSetError(cs, "velocity must be > 0");
                    return;
                }
                double v = toMs(vel);
                double area = q / v;
                double D = 2.0 * sqrt(area / M_PI);
                cs.results.push_back({imp ? "ID in" : "ID mm", calcFmtSig(imp ? D / 0.0254 : D * 1000.0), true});
            }
            double lpm = q * 60000.0;
            double gpm = q / 6.30901964e-5;
            cs.results.push_back({"L/min", calcFmtSig(lpm), false});
            cs.results.push_back({"gal/min", calcFmtSig(gpm), false});
            return;
        }
        if (cs.mode == 2) {
            double id, len;
            if (!calcParsePositive(cs.fields[0].value, id)) {
                calcSetError(cs, "ID must be > 0");
                return;
            }
            if (!calcParsePositive(cs.fields[1].value, len)) {
                calcSetError(cs, "length must be > 0");
                return;
            }
            double D = toM(id);
            double L = toLenM(len);
            double vol = M_PI * (D * 0.5) * (D * 0.5) * L; // m3
            cs.results.push_back({"m3", calcFmtSig(vol), true});
            cs.results.push_back({"L", calcFmtSig(vol * 1000.0), false});
            cs.results.push_back({"gal", calcFmtSig(vol / 0.003785411784), false});
            return;
        }
        // Darcy
        double id, len, qv;
        if (!calcParsePositive(cs.fields[0].value, id)) {
            calcSetError(cs, "ID must be > 0");
            return;
        }
        if (!calcParsePositive(cs.fields[1].value, len)) {
            calcSetError(cs, "length must be > 0");
            return;
        }
        if (!calcParsePositive(cs.fields[2].value, qv)) {
            calcSetError(cs, "Q must be > 0");
            return;
        }
        double D = toM(id);
        double L = toLenM(len);
        double Q = toM3s(qv);
        double eps_mm = 0.0015;
        if (cs.fields[3].value == "steel") eps_mm = 0.045;
        double eps = eps_mm / 1000.0;
        double area = M_PI * (D * 0.5) * (D * 0.5);
        if (area <= 0 || D <= 0) {
            calcSetError(cs, "bad ID");
            return;
        }
        double v = Q / area;
        // Water at 20 C
        const double nu = 1.004e-6; // m2/s
        const double g = 9.80665;
        double Re = v * D / nu;
        if (Re < 1) Re = 1;
        // Haaland: 1/sqrt(f) = -1.8 log10( (eps/D/3.7)^1.11 + 6.9/Re )
        double invSqrtF = -1.8 * log10(pow(eps / D / 3.7, 1.11) + 6.9 / Re);
        double f = 1.0 / (invSqrtF * invSqrtF);
        double hf = f * (L / D) * (v * v) / (2.0 * g);
        cs.results.push_back({"head loss", calcFmtSig(hf) + " m", true});
        cs.results.push_back({"Re", calcFmtSig(Re), false});
        cs.results.push_back({"f", calcFmtSig(f), false});
        cs.results.push_back({"vel", calcFmtSig(v) + " m/s", false});
    };

    runCalcSession(s);
}

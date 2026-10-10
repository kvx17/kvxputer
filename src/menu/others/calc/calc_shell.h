#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

enum CalcEditKind : uint8_t { CE_NUM, CE_TEXT, CE_HEX, CE_CHOICE };

struct CalcField {
    String label;
    String value;
    CalcEditKind kind = CE_NUM;
    std::vector<String> choices;
    int maxLen = 32;
};

struct CalcLine {
    String label;
    String value;
    bool hero = false;
};

struct CalcSession {
    String title;
    std::vector<String> modeNames;
    int mode = 0;
    std::vector<CalcField> fields;
    std::vector<CalcLine> results;
    String status;
    int pager = 0;
    int pagerCount = 1;
    // Rebuild fields when mode or SI/IMP changes (optional).
    std::function<void(CalcSession &)> onMode;
    // Fill results from fields/mode.
    std::function<void(CalcSession &)> recompute;
};

void runCalcSession(CalcSession &s);

// ---- Shared helpers --------------------------------------------------------

String calcFmtSig(double v, int sig = 6);
String calcFmtEng(double v); // k/M/m/u prefixes
String calcGroupBin(uint64_t v, int widthBits);
String calcGroupOct(uint64_t v);
String calcGroupHex(uint64_t v, int nibbles = 0);
String calcGroupHexStr(const String &hex);

double calcNearestE(double ohms, const double *series, int n);
double calcNearestE12(double ohms);
double calcNearestE24(double ohms);

double calcAwgDiameterMm(int gauge);
double calcAwgAreaMm2(int gauge);
double calcAwgOhmsPerMeter(int gauge); // copper
int calcNearestAwg(double diameterMm);
double calcResistivity(bool aluminum); // ohm·m

bool calcParseDouble(const String &s, double &out);
bool calcParseU64(const String &s, int base, uint64_t &out);
String calcTrim(const String &s);

// Validation helpers: on failure clear results and set status.
void calcSetError(CalcSession &s, const char *msg);
bool calcParseInt(const String &s, long &out); // whole string, no decimals/suffixes
bool calcParseIntRange(const String &s, long &out, long lo, long hi);
bool calcParseDoubleRange(const String &s, double &out, double lo, double hi);
bool calcParsePositive(const String &s, double &out); // finite and > 0
bool calcParseNonNeg(const String &s, double &out);   // finite and >= 0

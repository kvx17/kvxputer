#include "calc_menu.h"
#include "calc_shell.h"

#include <cstdio>
#include <cstring>
#include <globals.h>

namespace {

static bool parseBaseValue(const String &raw, int defaultBase, uint64_t &out, int &usedBase) {
    String t = calcTrim(raw);
    if (!t.length()) return false;
    usedBase = defaultBase;
    if (t.startsWith("0x") || t.startsWith("0X")) {
        usedBase = 16;
        t = t.substring(2);
    } else if (t.startsWith("0b") || t.startsWith("0B")) {
        usedBase = 2;
        t = t.substring(2);
    } else if (t.startsWith("0o") || t.startsWith("0O")) {
        usedBase = 8;
        t = t.substring(2);
    }
    return calcParseU64(t, usedBase, out);
}

static int64_t twosComplement(uint64_t v, int width) {
    if (width <= 0 || width > 64) return (int64_t)v;
    uint64_t mask = (width == 64) ? ~0ull : ((1ull << width) - 1);
    v &= mask;
    uint64_t sign = 1ull << (width - 1);
    if (v & sign) {
        // negative
        int64_t neg = (int64_t)(v | ~mask);
        return neg;
    }
    return (int64_t)v;
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint16_t crc16CcittFalse(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

static uint16_t crc16Modbus(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

static bool parseHexBytes(const String &s, std::vector<uint8_t> &out) {
    out.clear();
    String t = calcTrim(s);
    // Accept "AA BB" or "AABB" or "0xAA,0xBB"
    String hex;
    for (size_t i = 0; i < t.length(); i++) {
        char c = t[i];
        if (c == ',' || c == ' ' || c == '_' || c == ':') {
            if (hex.length() >= 2) {
                // flush pairs
            }
            continue;
        }
        if ((c == '0') && i + 1 < t.length() && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
            i++;
            continue;
        }
        if (!isxdigit((unsigned char)c)) return false;
        hex += c;
    }
    if (hex.length() % 2) return false;
    for (size_t i = 0; i < hex.length(); i += 2) {
        char buf[3] = {hex[i], hex[i + 1], 0};
        out.push_back((uint8_t)strtoul(buf, nullptr, 16));
    }
    return true;
}

} // namespace

void calcBase() {
    CalcSession s;
    s.title = "Base";
    s.modeNames = {"convert", "2^n"};
    s.mode = 0;
    static const std::vector<String> widths = {"off", "8", "16", "32", "64"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        if (cs.mode == 0) {
            cs.fields.push_back({"value", "255", CE_TEXT, {}, 40});
            cs.fields.push_back({"base", "10", CE_NUM, {}, 3});
            cs.fields.push_back({"width", "off", CE_CHOICE, widths, 4});
        } else {
            cs.fields.push_back({"n", "10", CE_NUM, {}, 3});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        if (cs.mode == 1) {
            long n;
            if (!calcParseIntRange(cs.fields[0].value, n, 0, 63)) {
                calcSetError(cs, "n must be integer 0..63");
                return;
            }
            uint64_t v = 1ull << (int)n;
            cs.results.push_back({"2^n", String((unsigned long long)v), true});
            const char *lab = nullptr;
            if (n == 10) lab = "KiB";
            else if (n == 20) lab = "MiB";
            else if (n == 30) lab = "GiB";
            else if (n == 40) lab = "TiB";
            if (lab) cs.results.push_back({"boundary", lab, false});
            return;
        }

        long baseL;
        if (!calcParseIntRange(cs.fields[1].value, baseL, 2, 36)) {
            calcSetError(cs, "base must be integer 2..36");
            return;
        }
        int base = (int)baseL;
        uint64_t v;
        int used;
        if (!parseBaseValue(cs.fields[0].value, base, v, used)) {
            calcSetError(cs, "bad value for base");
            return;
        }
        int width = 0;
        String w = cs.fields[2].value;
        if (w == "8") width = 8;
        else if (w == "16") width = 16;
        else if (w == "32") width = 32;
        else if (w == "64") width = 64;

        if (width) {
            uint64_t mask = (width == 64) ? ~0ull : ((1ull << width) - 1);
            v &= mask;
        }

        int binW = width ? width : 0;
        cs.results.push_back({"bin", calcGroupBin(v, binW), false});
        cs.results.push_back({"oct", calcGroupOct(v), false});
        cs.results.push_back({"dec", String((unsigned long long)v), true});
        cs.results.push_back({"hex", calcGroupHex(v, width ? width / 4 : 0), false});
        if (width) {
            int64_t tc = twosComplement(v, width);
            cs.results.push_back({"2's comp", String((long long)tc), false});
        }
    };

    runCalcSession(s);
}

void calcBytes() {
    CalcSession s;
    s.title = "Byte units";
    s.modeNames = {"convert"};
    static const std::vector<String> units = {"B", "KB", "MB", "GB", "TB", "KiB", "MiB", "GiB", "TiB"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"count", "1", CE_NUM, {}, 20});
        cs.fields.push_back({"unit", "MiB", CE_CHOICE, units, 4});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        double n;
        if (!calcParseNonNeg(cs.fields[0].value, n)) {
            calcSetError(cs, "count must be >= 0");
            return;
        }
        String u = cs.fields[1].value;
        double bytes = n;
        if (u == "KB") bytes = n * 1000.0;
        else if (u == "MB") bytes = n * 1e6;
        else if (u == "GB") bytes = n * 1e9;
        else if (u == "TB") bytes = n * 1e12;
        else if (u == "KiB") bytes = n * 1024.0;
        else if (u == "MiB") bytes = n * 1048576.0;
        else if (u == "GiB") bytes = n * 1073741824.0;
        else if (u == "TiB") bytes = n * 1099511627776.0;

        cs.results.push_back({"bytes", calcFmtSig(bytes), true});

        auto pushLadder = [&](const char *title, double base, const char *names[]) {
            cs.results.push_back({title, "", false});
            double v = bytes;
            for (int i = 0; i < 5; i++) {
                cs.results.push_back({names[i], calcFmtSig(v), false});
                v /= base;
            }
        };
        const char *decN[] = {"B", "KB", "MB", "GB", "TB"};
        const char *binN[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        pushLadder("decimal", 1000.0, decN);
        pushLadder("binary", 1024.0, binN);

        // Percent gap between same-named prefix at this magnitude
        // Find which pair: pick power based on bytes
        double decUnit = 1000.0, binUnit = 1024.0;
        const char *pair = "KB vs KiB";
        if (bytes >= 1e12) {
            decUnit = 1e12;
            binUnit = 1099511627776.0;
            pair = "TB vs TiB";
        } else if (bytes >= 1e9) {
            decUnit = 1e9;
            binUnit = 1073741824.0;
            pair = "GB vs GiB";
        } else if (bytes >= 1e6) {
            decUnit = 1e6;
            binUnit = 1048576.0;
            pair = "MB vs MiB";
        }
        double gap = (binUnit - decUnit) / decUnit * 100.0;
        cs.results.push_back({pair, calcFmtSig(gap) + "%", false});
    };

    runCalcSession(s);
}

void calcBaud() {
    CalcSession s;
    s.title = "Baud";
    s.modeNames = {"bit-time"};
    static const std::vector<String> rates = {
        "300",     "1200",   "2400",   "4800",   "9600",    "19200",
        "38400",   "57600",  "115200", "230400", "460800",  "921600"
    };

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"baud", "115200", CE_NUM, {}, 10});
        std::vector<String> presets = {"-"};
        presets.insert(presets.end(), rates.begin(), rates.end());
        cs.fields.push_back({"preset", "-", CE_CHOICE, presets, 10});
    };

    s.recompute = [](CalcSession &cs) {
        if (cs.fields.size() >= 2 && cs.fields[1].value != "-") {
            cs.fields[0].value = cs.fields[1].value;
            cs.fields[1].value = "-";
        }
        cs.status = "";
        cs.results.clear();
        long baudL;
        if (!calcParseIntRange(cs.fields[0].value, baudL, 1, 100000000L)) {
            calcSetError(cs, "baud must be integer >= 1");
            return;
        }
        double baud = (double)baudL;
        double bit = 1.0 / baud;
        double frame = bit * 10.0; // 8N1
        double bps = baud / 10.0;
        auto fmtTime = [](double sec) -> String {
            if (sec >= 1.0) return calcFmtSig(sec) + " s";
            if (sec >= 1e-3) return calcFmtSig(sec * 1e3) + " ms";
            return calcFmtSig(sec * 1e6) + " us";
        };
        cs.results.push_back({"bit time", fmtTime(bit), true});
        cs.results.push_back({"8N1 frame", fmtTime(frame), false});
        cs.results.push_back({"bytes/s", calcFmtSig(bps), false});
    };

    runCalcSession(s);
}

void calcCrc() {
    CalcSession s;
    s.title = "CRC";
    s.modeNames = {"checksum"};
    static const std::vector<String> polys = {"CRC-32", "CRC-16-CCITT", "MODBUS"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"hex bytes", "01 02 03", CE_HEX, {}, 120});
        cs.fields.push_back({"poly", "CRC-32", CE_CHOICE, polys, 16});
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        std::vector<uint8_t> bytes;
        if (!parseHexBytes(cs.fields[0].value, bytes)) {
            calcSetError(cs, "bad hex bytes");
            return;
        }
        if (bytes.empty()) {
            calcSetError(cs, "need hex bytes");
            return;
        }
        String poly = cs.fields[1].value;
        if (poly == "CRC-32") {
            uint32_t c = crc32(bytes.data(), bytes.size());
            char buf[16];
            snprintf(buf, sizeof(buf), "%08X", (unsigned)c);
            cs.results.push_back({"CRC-32", String(buf), true});
        } else if (poly == "MODBUS") {
            uint16_t c = crc16Modbus(bytes.data(), bytes.size());
            char buf[12];
            snprintf(buf, sizeof(buf), "%04X", (unsigned)c);
            cs.results.push_back({"MODBUS", String(buf), true});
        } else {
            uint16_t c = crc16CcittFalse(bytes.data(), bytes.size());
            char buf[12];
            snprintf(buf, sizeof(buf), "%04X", (unsigned)c);
            cs.results.push_back({"CCITT", String(buf), true});
        }
    };

    runCalcSession(s);
}

#include "eng_calc_form.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tools::engcalc {
namespace {

// ---- Field tables ----
const FieldDef kIpPfx[] = {{"ip", "IP", AutoFormat::Ipv4}, {"extra", "extra"}};
const FieldDef kMask[] = {{"ip", "IP", AutoFormat::Ipv4}, {"mask", "mask", AutoFormat::Ipv4}};
const FieldDef kSplit[] = {{"net", "net", AutoFormat::Ipv4}, {"pfx", "pfx"}, {"n", "N"}, {"i", "i"}};
const FieldDef kHosts[] = {{"net", "net", AutoFormat::Ipv4}, {"pfx", "pfx"}, {"h", "hosts"}, {"i", "i"}};
const FieldDef kV6[] = {{"addr", "addr"}, {"pfx", "pfx"}};
const ModeDef kSubnetModes[] = {
    {"", "cidr", kIpPfx, 2},
    {"mask", "mask", kMask, 2},
    {"split", "split", kSplit, 4},
    {"hosts", "hosts", kHosts, 4},
    {"v6", "v6", kV6, 2},
};

const FieldDef kBase[] = {{"val", "value"}, {"from", "from"}, {"to", "to"}, {"w", "width"}};
const FieldDef kBytes[] = {{"n", "count"}, {"u", "unit"}};
const FieldDef kResistor[] = {{"c", "colors/rev"}};
const FieldDef kLed[] = {{"vs", "Vs"}, {"vf", "Vf"}, {"ma", "mA"}};
const FieldDef kDiv[] = {{"r1", "R1"}, {"r2", "R2"}, {"vin", "Vin"}};
const FieldDef kDesign[] = {{"vin", "Vin"}, {"vout", "Vout"}, {"r1", "R1"}};
const FieldDef kRatio[] = {{"r", "ratio"}};
const ModeDef kDividerModes[] = {
    {"", "div", kDiv, 3},
    {"design", "design", kDesign, 3},
    {"ratio", "ratio", kRatio, 1},
};

const FieldDef kBaud[] = {{"baud", "baud"}, {"bits", "bits/frm"}};
const FieldDef kDbm[] = {{"v", "value"}, {"u", "unit"}, {"g", "gain"}};
const FieldDef kEpochS[] = {{"s", "seconds"}};
const FieldDef kEpochMs[] = {{"ms", "millis"}};
const FieldDef kEpochDate[] = {{"d", "YYYY-MM-DD", AutoFormat::IsoDate},
                               {"t", "HH:MM:SS", AutoFormat::IsoTime}};
const ModeDef kEpochModes[] = {
    {"", "secs", kEpochS, 1},
    {"ms", "ms", kEpochMs, 1},
    {"date", "date", kEpochDate, 2},
};

const FieldDef kTemp[] = {{"v", "value"}, {"u", "unit"}};
const FieldDef kLen[] = {{"v", "value"}, {"u", "unit"}};
const FieldDef kAwg[] = {{"n", "AWG/dia"}};
const ModeDef kMeasureModes[] = {
    {"temp", "temp", kTemp, 2},
    {"len", "len", kLen, 2},
    {"awg", "awg", kAwg, 1},
};

const FieldDef kBits[] = {{"w", "width"}, {"bits", "bits"}};
const FieldDef kHexMask[] = {{"w", "width"}, {"hex", "hex"}};
const ModeDef kBitmaskModes[] = {
    {"bits", "bits", kBits, 2},
    {"hex", "hex", kHexMask, 2},
};

const FieldDef kCrcPaste[] = {{"data", "data"}};
const ModeDef kCrcModes[] = {
    {"crc32", "crc32", kCrcPaste, 1},
    {"ccitt", "ccitt", kCrcPaste, 1},
    {"modbus", "modbus", kCrcPaste, 1},
    {"all", "all", kCrcPaste, 1},
    {"text", "text", kCrcPaste, 1},
};

const FieldDef kUnits[] = {{"v", "value"}, {"from", "from"}, {"to", "to"}};
const FieldDef kOhm[] = {{"v", "V"}, {"i", "I"}, {"r", "R"}, {"w", "W"}};
const FieldDef kVdrop[] = {{"m", "metal"}, {"len", "length"}, {"a", "amps"}, {"v", "volts"}, {"awg", "AWG"}};
const FieldDef kMotor[] = {{"hp", "hp"}, {"tq", "torque"}, {"rpm", "rpm"}, {"eff", "eff%"}};
const FieldDef kBoyle[] = {{"p1", "P1"}, {"v1", "V1"}, {"p2", "P2"}, {"v2", "V2"}};
const FieldDef kIdeal[] = {{"p", "P"}, {"t", "T"}, {"v", "V"}};
const FieldDef kAir[] = {{"t", "t"}};
const ModeDef kGasModes[] = {
    {"boyle", "boyle", kBoyle, 4},
    {"ideal", "ideal", kIdeal, 3},
    {"air", "air", kAir, 1},
};

const FieldDef kFlowQ[] = {{"id", "ID"}, {"vel", "vel"}};
const FieldDef kFlowDv[] = {{"q", "Q"}, {"id", "ID"}, {"vel", "vel"}};
const FieldDef kFlowVol[] = {{"id", "ID"}, {"len", "len"}};
const FieldDef kFlowDarcy[] = {{"id", "ID"}, {"len", "len"}, {"q", "Q"}, {"r", "rough"}};
const ModeDef kFlowModes[] = {
    {"q", "q", kFlowQ, 2},
    {"dv", "dv", kFlowDv, 3},
    {"vol", "vol", kFlowVol, 2},
    {"darcy", "darcy", kFlowDarcy, 4},
};

const FieldDef kRc[] = {{"r", "R"}, {"c", "C"}, {"pct", "%"}};
const FieldDef kBatt[] = {{"mah", "mAh"}, {"ma", "mA"}};
const FieldDef kFreq[] = {{"v", "Hz/t/wave"}, {"vf", "vf"}};
const FieldDef kPwm[] = {{"f", "freq"}, {"d", "duty/hi"}};
const FieldDef kPctOf[] = {{"p", "%"}, {"y", "of"}};
const FieldDef kPctCh[] = {{"old", "old"}, {"neu", "new"}};
const FieldDef kPctRatio[] = {{"part", "part"}, {"whole", "whole"}};
const ModeDef kPctModes[] = {
    {"", "of", kPctOf, 2},
    {"change", "chg", kPctCh, 2},
    {"ratio", "ratio", kPctRatio, 2},
};

const FieldDef kDateDiff[] = {{"a", "date A", AutoFormat::IsoDate},
                              {"b", "date B", AutoFormat::IsoDate}};
const FieldDef kDateAdd[] = {{"d", "date", AutoFormat::IsoDate}, {"n", "days"}};
const ModeDef kDateModes[] = {
    {"", "diff", kDateDiff, 2},
    {"add", "add", kDateAdd, 2},
};

const FieldDef kTime[] = {{"a", "time A", AutoFormat::IsoTime},
                          {"b", "time B", AutoFormat::IsoTime}};
const FieldDef kTip[] = {{"bill", "bill"}, {"tip", "tip%"}, {"n", "people"}};
const FieldDef kDist[] = {{"lat1", "lat1", AutoFormat::GeoDec},
                          {"lon1", "lon1", AutoFormat::GeoDec},
                          {"lat2", "lat2", AutoFormat::GeoDec},
                          {"lon2", "lon2", AutoFormat::GeoDec}};
const FieldDef kReactC[] = {{"f", "F"}, {"c", "C"}};
const FieldDef kReactL[] = {{"f", "F"}, {"l", "L"}};
const FieldDef kReactRc[] = {{"f", "F"}, {"r", "R"}, {"c", "C"}};
const FieldDef kReactLc[] = {{"l", "L"}, {"c", "C"}};
const ModeDef kReactModes[] = {
    {"c", "c", kReactC, 2},
    {"l", "l", kReactL, 2},
    {"rc", "rc", kReactRc, 3},
    {"lc", "lc", kReactLc, 2},
};

const FieldDef k555a[] = {{"ra", "Ra"}, {"rb", "Rb"}, {"c", "C"}};
const FieldDef k555m[] = {{"r", "R"}, {"c", "C"}};
const ModeDef k555Modes[] = {
    {"", "astable", k555a, 3},
    {"mono", "mono", k555m, 2},
};

const FieldDef kPaste[] = {{"text", "input"}};
const FieldDef kOui[] = {{"mac", "MAC", AutoFormat::Mac}};
const FieldDef kFspl[] = {{"f", "freq"}, {"d", "dist"}};
const FieldDef kLink[] = {{"tx", "tx_dBm"}, {"gtx", "tx_dB"}, {"grx", "rx_dB"}, {"f", "freq"}, {"d", "dist"}};
const ModeDef kFsplModes[] = {
    {"", "loss", kFspl, 2},
    {"link", "link", kLink, 5},
};
const FieldDef kXfer[] = {{"size", "size"}, {"rate", "rate"}};
const FieldDef kKwh[] = {{"w", "W/kWh"}, {"h", "hours"}, {"p", "price"}};
const FieldDef kCoord[] = {{"lat", "lat", AutoFormat::GeoDec}, {"lon", "lon", AutoFormat::GeoDec}};
const FieldDef kListItem[] = {{"v", "value"}};
const FieldDef kCidrItem[] = {{"v", "CIDR", AutoFormat::Ipv4}};

const OpSchema kSchemas[] = {
    /* Subnet */ {FormKind::Labeled, ".:/", kSubnetModes, 5, nullptr, 0, 0, 0},
    /* BaseConvert */ {FormKind::Labeled, "x", nullptr, 0, kBase, 4, 0, 0},
    /* ByteUnits */ {FormKind::Labeled, ".kMGT", nullptr, 0, kBytes, 2, 0, 0},
    /* Resistor */ {FormKind::Labeled, " ", nullptr, 0, kResistor, 1, 0, 0},
    /* LedResistor */ {FormKind::Labeled, ".", nullptr, 0, kLed, 3, 0, 0},
    /* VoltageDivider */ {FormKind::Labeled, ".kMu", kDividerModes, 3, nullptr, 0, 0, 0},
    /* Baud */ {FormKind::Labeled, "", nullptr, 0, kBaud, 2, 0, 0},
    /* Dbm */ {FormKind::Labeled, ".", nullptr, 0, kDbm, 3, 0, 0},
    /* Epoch */ {FormKind::Labeled, "-:", kEpochModes, 3, nullptr, 0, 0, 0},
    /* Measure */ {FormKind::Labeled, ".CFk", kMeasureModes, 3, nullptr, 0, 0, 0},
    /* Bitmask */ {FormKind::Labeled, "x,", kBitmaskModes, 2, nullptr, 0, 0, 0},
    /* Crc */ {FormKind::Paste, " ", kCrcModes, 5, kCrcPaste, 1, 0, 0},
    /* Units */ {FormKind::Labeled, ".", nullptr, 0, kUnits, 3, 0, 0},
    /* OhmsLaw */ {FormKind::Labeled, ".-", nullptr, 0, kOhm, 4, 0, 0},
    /* VoltageDrop */ {FormKind::Labeled, ".", nullptr, 0, kVdrop, 5, 0, 0},
    /* Motor */ {FormKind::Labeled, ".-", nullptr, 0, kMotor, 4, 0, 0},
    /* Gas */ {FormKind::Labeled, ".-", kGasModes, 3, nullptr, 0, 0, 0},
    /* Flow */ {FormKind::Labeled, ".", kFlowModes, 4, nullptr, 0, 0, 0},
    /* Parallel */ {FormKind::List, ".kMu", nullptr, 0, kListItem, 1, 2, 8},
    /* RcTimer */ {FormKind::Labeled, ".kMun", nullptr, 0, kRc, 3, 0, 0},
    /* Battery */ {FormKind::Labeled, "", nullptr, 0, kBatt, 2, 0, 0},
    /* Frequency */ {FormKind::Labeled, ".kMG", nullptr, 0, kFreq, 2, 0, 0},
    /* Pwm */ {FormKind::Labeled, ".ku", nullptr, 0, kPwm, 2, 0, 0},
    /* Percentage */ {FormKind::Labeled, ".%", kPctModes, 3, nullptr, 0, 0, 0},
    /* DateDiff */ {FormKind::Labeled, "-", kDateModes, 2, nullptr, 0, 0, 0},
    /* TimeDiff */ {FormKind::Labeled, "-:", nullptr, 0, kTime, 2, 0, 0},
    /* TipSplit */ {FormKind::Labeled, ".", nullptr, 0, kTip, 3, 0, 0},
    /* Distance */ {FormKind::Labeled, ".-", nullptr, 0, kDist, 4, 0, 0},
    /* Reactance */ {FormKind::Labeled, ".kMun", kReactModes, 4, nullptr, 0, 0, 0},
    /* Timer555 */ {FormKind::Labeled, ".kMun", k555Modes, 2, nullptr, 0, 0, 0},
    /* Entropy */ {FormKind::Paste, " ", nullptr, 0, kPaste, 1, 0, 0},
    /* HashId */ {FormKind::Paste, "$", nullptr, 0, kPaste, 1, 0, 0},
    /* Jwt */ {FormKind::Paste, ".", nullptr, 0, kPaste, 1, 0, 0},
    /* Xor */ {FormKind::Paste, " :", nullptr, 0, kPaste, 1, 0, 0},
    /* CidrAgg */ {FormKind::List, "./", nullptr, 0, kCidrItem, 1, 2, 12},
    /* Oui */ {FormKind::Labeled, "", nullptr, 0, kOui, 1, 0, 0},
    /* Fspl */ {FormKind::Labeled, ".kMG", kFsplModes, 2, nullptr, 0, 0, 0},
    /* Transfer */ {FormKind::Labeled, ".kMG", nullptr, 0, kXfer, 2, 0, 0},
    /* Kwh */ {FormKind::Labeled, ".W", nullptr, 0, kKwh, 3, 0, 0},
    /* Coord */ {FormKind::Labeled, ".-", nullptr, 0, kCoord, 2, 0, 0},
    /* HexView */ {FormKind::Paste, ":", nullptr, 0, kPaste, 1, 0, 0},
};

static_assert(sizeof(kSchemas) / sizeof(kSchemas[0]) == static_cast<size_t>(Op::Count),
              "schema table must match Op::Count");

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

const ModeDef* active_mode(const OpSchema& sch, int mode_index) {
    if (!sch.modes || sch.mode_count <= 0) return nullptr;
    if (mode_index < 0) mode_index = 0;
    if (mode_index >= sch.mode_count) mode_index = sch.mode_count - 1;
    return &sch.modes[mode_index];
}

} // namespace

const OpSchema& schema_for(Op op) {
    const int i = static_cast<int>(op);
    if (i < 0 || i >= static_cast<int>(Op::Count)) return kSchemas[0];
    return kSchemas[i];
}

void parse_line_into(Op op, const std::string& line, int& mode_index, std::vector<std::string>& values) {
    const OpSchema& sch = schema_for(op);
    mode_index = 0;
    values.clear();
    auto toks = split_ws(line);
    if (sch.kind == FormKind::Paste) {
        if (sch.modes && sch.mode_count > 0 && !toks.empty()) {
            for (int i = 0; i < sch.mode_count; ++i) {
                if (sch.modes[i].token[0] && toks[0] == sch.modes[i].token) {
                    mode_index = i;
                    toks.erase(toks.begin());
                    break;
                }
            }
        }
        std::string rest;
        for (size_t i = 0; i < toks.size(); ++i) {
            if (i) rest.push_back(' ');
            rest += toks[i];
        }
        values.push_back(rest);
        return;
    }
    if (sch.kind == FormKind::List) {
        values = std::move(toks);
        if (values.empty()) {
            for (int i = 0; i < sch.list_min; ++i) values.emplace_back();
        }
        return;
    }
    // Labeled
    if (sch.modes && sch.mode_count > 0 && !toks.empty()) {
        bool matched = false;
        for (int i = 0; i < sch.mode_count; ++i) {
            if (sch.modes[i].token[0] && toks[0] == sch.modes[i].token) {
                mode_index = i;
                toks.erase(toks.begin());
                matched = true;
                break;
            }
        }
        if (!matched) {
            // Prefer first mode with empty token.
            for (int i = 0; i < sch.mode_count; ++i) {
                if (!sch.modes[i].token[0]) {
                    mode_index = i;
                    break;
                }
            }
        }
    }
    const ModeDef* mode = active_mode(sch, mode_index);
    const FieldDef* fields = mode ? mode->fields : sch.fields;
    const int n = mode ? mode->field_count : sch.field_count;
    values.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        values[static_cast<size_t>(i)] = (static_cast<size_t>(i) < toks.size()) ? toks[static_cast<size_t>(i)] : "";
    }
}

std::string build_line(Op op, int mode_index, const std::vector<std::string>& values) {
    const OpSchema& sch = schema_for(op);
    std::string out;
    auto append = [&](const std::string& t) {
        if (t.empty()) return;
        if (!out.empty()) out.push_back(' ');
        out += t;
    };

    if (sch.kind == FormKind::Paste) {
        if (const ModeDef* mode = active_mode(sch, mode_index)) {
            if (mode->token[0]) append(mode->token);
        }
        if (!values.empty()) append(values[0]);
        return out;
    }
    if (sch.kind == FormKind::List) {
        for (const auto& v : values) {
            if (!v.empty() && v != "-") append(v);
        }
        return out;
    }
    if (const ModeDef* mode = active_mode(sch, mode_index)) {
        if (mode->token[0]) append(mode->token);
    }
    for (const auto& v : values) {
        if (v.empty()) append("-");
        else append(v);
    }
    // Trim trailing "- " placeholders that were never filled — keep at least
    // one token if the line would otherwise be only mode.
    while (out.size() >= 2 && out.back() == '-' && out[out.size() - 2] == ' ') {
        out.pop_back();
        out.pop_back();
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

const FieldDef* active_fields(Op op, int mode_index, int& count) {
    const OpSchema& sch = schema_for(op);
    if (const ModeDef* mode = active_mode(sch, mode_index)) {
        count = mode->field_count;
        return mode->fields;
    }
    count = sch.field_count;
    return sch.fields;
}

std::string apply_auto_format(AutoFormat fmt, const std::string& raw, int& caret) {
    if (fmt == AutoFormat::None) {
        caret = static_cast<int>(raw.size());
        return raw;
    }

    if (fmt == AutoFormat::Mac) {
        std::string hex;
        for (unsigned char c : raw) {
            if (std::isxdigit(c)) hex.push_back(static_cast<char>(std::toupper(c)));
            if (hex.size() >= 12) break;
        }
        std::string out;
        for (size_t i = 0; i < hex.size(); ++i) {
            if (i && (i % 2) == 0) out.push_back(':');
            out.push_back(hex[i]);
        }
        caret = static_cast<int>(out.size());
        return out;
    }

    if (fmt == AutoFormat::Ipv4) {
        // Keep optional trailing /prefix for CIDR. Auto '.' after 3 digits per
        // octet; an explicit '.' ends a short octet (10.0.0.1).
        std::string host = raw;
        std::string suffix;
        const auto slash = raw.find('/');
        if (slash != std::string::npos) {
            host = raw.substr(0, slash);
            suffix = raw.substr(slash);
        }
        std::string out;
        int octet_len = 0;
        int dots = 0;
        for (unsigned char c : host) {
            if (c == '.') {
                if (octet_len > 0 && dots < 3) {
                    out.push_back('.');
                    ++dots;
                    octet_len = 0;
                }
                continue;
            }
            if (!std::isdigit(c)) continue;
            if (octet_len >= 3 && dots < 3) {
                out.push_back('.');
                ++dots;
                octet_len = 0;
            }
            if (dots >= 3 && octet_len >= 3) continue;
            out.push_back(static_cast<char>(c));
            ++octet_len;
        }
        if (!suffix.empty()) {
            std::string pfx;
            for (size_t j = 1; j < suffix.size(); ++j) {
                if (std::isdigit(static_cast<unsigned char>(suffix[j]))) pfx.push_back(suffix[j]);
                if (pfx.size() >= 2) break;
            }
            out.push_back('/');
            out += pfx;
        }
        caret = static_cast<int>(out.size());
        return out;
    }

    if (fmt == AutoFormat::GeoDec) {
        // Keep leading '-', one '.', and digits. 4th integer digit flips to fractional.
        std::string out;
        bool seen_dot = false;
        int int_digits = 0;
        size_t i = 0;
        if (!raw.empty() && raw[0] == '-') {
            out.push_back('-');
            i = 1;
        }
        for (; i < raw.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(raw[i]);
            if (c == '.' && !seen_dot) {
                out.push_back('.');
                seen_dot = true;
                continue;
            }
            if (!std::isdigit(c)) continue;
            if (!seen_dot) {
                if (int_digits >= 3) {
                    out.push_back('.');
                    seen_dot = true;
                    out.push_back(static_cast<char>(c));
                } else {
                    ++int_digits;
                    out.push_back(static_cast<char>(c));
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        caret = static_cast<int>(out.size());
        return out;
    }

    if (fmt == AutoFormat::IsoDate) {
        std::string digits;
        for (unsigned char c : raw) {
            if (std::isdigit(c)) digits.push_back(static_cast<char>(c));
            if (digits.size() >= 8) break;
        }
        std::string out;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i == 4 || i == 6) out.push_back('-');
            out.push_back(digits[i]);
        }
        caret = static_cast<int>(out.size());
        return out;
    }

    if (fmt == AutoFormat::IsoTime) {
        std::string digits;
        for (unsigned char c : raw) {
            if (std::isdigit(c)) digits.push_back(static_cast<char>(c));
            if (digits.size() >= 6) break;
        }
        std::string out;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i == 2 || i == 4) out.push_back(':');
            out.push_back(digits[i]);
        }
        caret = static_cast<int>(out.size());
        return out;
    }

    caret = static_cast<int>(raw.size());
    return raw;
}

} // namespace tools::engcalc

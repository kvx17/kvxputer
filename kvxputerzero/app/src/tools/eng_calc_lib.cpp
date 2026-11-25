#include "eng_calc_lib.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace tools::engcalc {
namespace {

using Tokens = std::vector<std::string>;

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Generic helpers: strings, parsing, formatting
// ---------------------------------------------------------------------------

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Tokens split_ws(const std::string& s) {
    std::istringstream in(s);
    Tokens out;
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

// Everything after the first whitespace-delimited token (trimmed).
std::string rest_after_first(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return trim(s.substr(i));
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string sig(double v, int digits = 6) {
    if (!std::isfinite(v)) return "nan";
    if (v == 0.0) return "0";
    return fmt("%.*g", digits, v);
}

// 1.5k, 220m, 4.7u ...
std::string eng(double v) {
    if (!std::isfinite(v)) return "nan";
    double a = std::fabs(v);
    double scale = 1.0;
    const char* suf = "";
    if (a >= 1e9) { scale = 1e9; suf = "G"; }
    else if (a >= 1e6) { scale = 1e6; suf = "M"; }
    else if (a >= 1e3) { scale = 1e3; suf = "k"; }
    else if (a >= 1.0 || a == 0.0) { scale = 1.0; }
    else if (a >= 1e-3) { scale = 1e-3; suf = "m"; }
    else if (a >= 1e-6) { scale = 1e-6; suf = "u"; }
    else { scale = 1e-9; suf = "n"; }
    return sig(v / scale) + suf;
}

std::string fmt_ohms(double r) {
    if (r >= 1e6) return sig(r / 1e6) + "M";
    if (r >= 1e3) return sig(r / 1e3) + "k";
    return sig(r);
}

std::string fmt_time(double sec) {
    if (sec >= 1.0) return sig(sec) + " s";
    if (sec >= 1e-3) return sig(sec * 1e3) + " ms";
    return sig(sec * 1e6) + " us";
}

// Multi-line result builder.
class Out {
public:
    void line(const std::string& s) {
        if (!buf_.empty()) buf_ += '\n';
        buf_ += s;
    }
    void add(const std::string& label, const std::string& value) { line(label + ": " + value); }
    const std::string& str() const { return buf_; }

private:
    std::string buf_;
};

std::string fail(bool& ok, const std::string& msg) {
    ok = false;
    return msg;
}

bool is_blank(const std::string& tok) { return tok == "-" || tok == "_" || tok == "?"; }

// Strict number: whole token must parse, finite.
bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// Number with trailing unit text, e.g. "25mm" -> (25, "mm"); suffix lowercased.
bool split_suffix(const std::string& s, double& v, std::string& suffix) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(d)) return false;
    v = d;
    suffix = lower(end);
    return true;
}

bool parse_int_range(const std::string& s, long long& out, long long lo, long long hi) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    if (v < lo || v > hi) return false;
    out = v;
    return true;
}

bool parse_u64(const std::string& s, int base, uint64_t& out) {
    if (s.empty() || !std::isalnum(static_cast<unsigned char>(s[0]))) return false;
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &end, base);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    out = v;
    return true;
}

// Number that may be blank ("-"). has=false when blank.
struct Opt {
    bool has = false;
    double v = 0;
};

bool parse_opt(const std::string& tok, Opt& o) {
    if (is_blank(tok)) {
        o.has = false;
        return true;
    }
    if (!parse_double(tok, o.v)) return false;
    o.has = true;
    return true;
}

// Mode lookup -------------------------------------------------------------

struct ModeDef {
    const char* name;
    int id;
};

// If t[0] is a known mode name, remove it and return its id; else return -1.
int pick_mode(Tokens& t, std::initializer_list<ModeDef> modes) {
    if (t.empty()) return -1;
    const std::string m = lower(t[0]);
    for (const ModeDef& d : modes) {
        if (m == d.name) {
            t.erase(t.begin());
            return d.id;
        }
    }
    return -1;
}

// Optional single-mode token (e.g. "series", "convert").
void skip_mode(Tokens& t, std::initializer_list<const char*> names) {
    if (t.empty()) return;
    const std::string m = lower(t[0]);
    for (const char* n : names) {
        if (m == n) {
            t.erase(t.begin());
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Resistor / E-series helpers
// ---------------------------------------------------------------------------

const double kE12[12] = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
const double kE24[24] = {1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
                         3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};

double nearest_series(double ohms, const double* series, int n) {
    if (!(ohms > 0) || !std::isfinite(ohms)) return ohms;
    double best = ohms, bestD = 1e300;
    const double logT = std::log10(ohms);
    for (int e = -12; e <= 12; ++e) {
        const double decade = std::pow(10.0, e);
        for (int i = 0; i < n; ++i) {
            const double cand = series[i] * decade;
            const double d = std::fabs(std::log10(cand) - logT);
            if (d < bestD) {
                bestD = d;
                best = cand;
            }
        }
    }
    return best;
}

double nearest_e12(double r) { return nearest_series(r, kE12, 12); }
double nearest_e24(double r) { return nearest_series(r, kE24, 24); }

// "4k7", "4.7k", "220R", "1M", "10kohm", "4700" -> ohms.
bool parse_ohms(const std::string& in, double& out) {
    std::string t = trim(in);
    if (t.empty()) return false;
    std::string lt = lower(t);
    if (lt.size() > 3 && lt.compare(lt.size() - 3, 3, "ohm") == 0) {
        t.resize(t.size() - 3);
        lt.resize(lt.size() - 3);
    }
    // Trailing R/r after a digit: plain ohms ("220R").
    if (!t.empty() && (t.back() == 'R' || t.back() == 'r') && t.size() > 1 &&
        std::isdigit(static_cast<unsigned char>(t[t.size() - 2]))) {
        t.pop_back();
    }
    // Multiplier letter in the middle or end: k, K, M, m, R (as decimal point).
    for (size_t i = 1; i < t.size(); ++i) {
        const char c = t[i];
        double mult = 0;
        if (c == 'k' || c == 'K') mult = 1e3;
        else if (c == 'M' || c == 'm') mult = 1e6;
        else if (c == 'R' || c == 'r') mult = 1.0;
        else continue;
        const std::string left = t.substr(0, i);
        const std::string right = t.substr(i + 1);
        double whole = 0;
        if (!parse_double(left, whole)) return false;
        double frac = 0;
        if (!right.empty()) {
            for (char d : right)
                if (!std::isdigit(static_cast<unsigned char>(d))) return false;
            frac = std::strtod(right.c_str(), nullptr) / std::pow(10.0, static_cast<double>(right.size()));
        }
        out = (whole + frac) * mult;
        return true;
    }
    return parse_double(t, out);
}

const char* const kColors[12] = {"black", "brown", "red",  "orange", "yellow", "green",
                                 "blue",  "violet", "grey", "white",  "gold",   "silver"};
const char* const kColorCodes[12] = {"bk", "bn", "rd", "or", "ye", "gn", "bu", "vt", "gy", "wh", "gd", "sv"};

int color_index(const std::string& name) {
    const std::string n = lower(name);
    for (int i = 0; i < 12; ++i)
        if (n == kColors[i] || n == kColorCodes[i]) return i;
    if (n == "gray") return 8;
    if (n == "purple") return 7;
    if (n == "yw") return 4;
    return -1;
}

double band_mult(int idx) {
    if (idx >= 0 && idx <= 9) return std::pow(10.0, idx);
    if (idx == 10) return 0.1;
    if (idx == 11) return 0.01;
    return -1;
}

const char* band_tol(int idx) {
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

const char* band_tempco(int idx) {
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

// Color bands for a value (digits = 2: 4-band 5%, digits = 3: 5-band 1%).
std::string bands_from_ohms(double ohms, int digits, double tolPct) {
    int exp10 = static_cast<int>(std::floor(std::log10(ohms))) - (digits - 1);
    long imant = std::lround(ohms / std::pow(10.0, exp10));
    const long maxMant = static_cast<long>(std::pow(10.0, digits));
    const long minMant = static_cast<long>(std::pow(10.0, digits - 1));
    while (imant >= maxMant) {
        imant /= 10;
        ++exp10;
    }
    while (imant > 0 && imant < minMant) {
        imant *= 10;
        --exp10;
    }
    std::string out;
    for (int d = digits - 1; d >= 0; --d) {
        const int dig = static_cast<int>((imant / static_cast<long>(std::pow(10.0, d))) % 10);
        out += kColors[dig];
        out += ' ';
    }
    if (exp10 >= 0 && exp10 <= 9) out += kColors[exp10];
    else if (exp10 == -1) out += "gold";
    else if (exp10 == -2) out += "silver";
    else out += "?";
    out += ' ';
    if (std::fabs(tolPct - 5.0) < 0.01) out += "gold";
    else if (std::fabs(tolPct - 10.0) < 0.01) out += "silver";
    else if (std::fabs(tolPct - 1.0) < 0.01) out += "brown";
    else if (std::fabs(tolPct - 2.0) < 0.01) out += "red";
    else out += "?";
    return out;
}

// ---------------------------------------------------------------------------
// AWG
// ---------------------------------------------------------------------------

double awg_dia_mm(int g) { return 0.127 * std::pow(92.0, (36.0 - g) / 39.0); }
double awg_area_mm2(int g) {
    const double d = awg_dia_mm(g);
    return kPi * (d * 0.5) * (d * 0.5);
}
double awg_ohm_per_m_cu(int g) { return 1.724e-8 / (awg_area_mm2(g) * 1e-6); }

int nearest_awg(double mm) {
    int best = 0;
    double bestD = 1e300;
    for (int g = 0; g <= 40; ++g) {
        const double d = std::fabs(awg_dia_mm(g) - mm);
        if (d < bestD) {
            bestD = d;
            best = g;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Networking
// ---------------------------------------------------------------------------

bool parse_ipv4(const std::string& s, uint32_t& ip) {
    int parts[4] = {0, 0, 0, 0};
    int part = 0, digits = 0, val = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            if (digits >= 3) return false;
            val = val * 10 + (c - '0');
            ++digits;
            if (val > 255) return false;
        } else if (c == '.') {
            if (digits == 0 || part >= 3) return false;
            parts[part++] = val;
            val = 0;
            digits = 0;
        } else {
            return false;
        }
    }
    if (digits == 0 || part != 3) return false;
    parts[3] = val;
    ip = (static_cast<uint32_t>(parts[0]) << 24) | (static_cast<uint32_t>(parts[1]) << 16) |
         (static_cast<uint32_t>(parts[2]) << 8) | static_cast<uint32_t>(parts[3]);
    return true;
}

std::string fmt_ipv4(uint32_t ip) {
    return fmt("%u.%u.%u.%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
}

uint32_t prefix_to_mask(int p) {
    if (p <= 0) return 0;
    if (p >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - p);
}

// Returns prefix length for a contiguous mask, else -1.
int mask_to_prefix(uint32_t mask) {
    const uint32_t inv = ~mask;
    if (inv & (inv + 1)) return -1;
    int p = 0;
    while (mask & 0x80000000u) {
        ++p;
        mask <<= 1;
    }
    return p;
}

void first_last(uint32_t net, int pfx, uint32_t& first, uint32_t& last) {
    const uint32_t bc = net | ~prefix_to_mask(pfx);
    if (pfx <= 30) {
        first = net + 1;
        last = bc - 1;
    } else {
        first = net;
        last = bc;
    }
}

void v4_details(Out& o, uint32_t ip, int pfx) {
    const uint32_t mask = prefix_to_mask(pfx);
    const uint32_t net = ip & mask;
    const uint32_t bc = net | ~mask;
    const uint64_t total = 1ull << (32 - pfx);
    const uint64_t hosts = pfx <= 30 ? total - 2 : (pfx == 31 ? 2 : 1);
    uint32_t first = 0, last = 0;
    first_last(net, pfx, first, last);

    const char* role = "host";
    if (pfx <= 30 && ip == net) role = "network";
    else if (pfx <= 30 && ip == bc) role = "broadcast";

    o.add("network", fmt_ipv4(net) + "/" + std::to_string(pfx));
    o.add("broadcast", pfx <= 30 ? fmt_ipv4(bc) : std::string("-"));
    o.add("first", fmt_ipv4(first));
    o.add("last", fmt_ipv4(last));
    o.add("hosts", std::to_string(static_cast<unsigned long long>(hosts)));
    o.add("mask", fmt_ipv4(mask) + " /" + std::to_string(pfx));
    o.add("wildcard", fmt_ipv4(~mask));
    o.add("role", role);
}

struct V6 {
    uint16_t w[8] = {};
};

bool parse_hex16(const std::string& s, uint16_t& out) {
    if (s.empty() || s.size() > 4) return false;
    uint64_t v = 0;
    if (!parse_u64(s, 16, v) || v > 0xFFFF) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

std::vector<std::string> split_colon(const std::string& s) {
    std::vector<std::string> parts;
    if (s.empty()) return parts;
    size_t start = 0;
    for (;;) {
        const size_t c = s.find(':', start);
        if (c == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, c - start));
        start = c + 1;
    }
    return parts;
}

bool parse_ipv6(std::string s, V6& out) {
    const size_t pct = s.find('%'); // zone id
    if (pct != std::string::npos) s.resize(pct);
    const size_t dc = s.find("::");
    std::vector<std::string> left, right;
    if (dc == std::string::npos) {
        left = split_colon(s);
        if (left.size() != 8) return false;
        for (int i = 0; i < 8; ++i)
            if (!parse_hex16(left[static_cast<size_t>(i)], out.w[i])) return false;
        return true;
    }
    if (s.find("::", dc + 1) != std::string::npos) return false; // second "::"
    left = split_colon(s.substr(0, dc));
    right = split_colon(s.substr(dc + 2));
    if (left.size() + right.size() > 7) return false;
    size_t idx = 0;
    for (const auto& p : left)
        if (!parse_hex16(p, out.w[idx++])) return false;
    const size_t fill = 8 - left.size() - right.size();
    for (size_t i = 0; i < fill; ++i) out.w[idx++] = 0;
    for (const auto& p : right)
        if (!parse_hex16(p, out.w[idx++])) return false;
    return idx == 8;
}

std::string expand_v6(const V6& a) {
    return fmt("%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", a.w[0], a.w[1], a.w[2], a.w[3], a.w[4], a.w[5],
               a.w[6], a.w[7]);
}

std::string compress_v6(const V6& a) {
    int bestStart = -1, bestLen = 0, run = 0, runStart = 0;
    for (int i = 0; i < 8; ++i) {
        if (a.w[i] == 0) {
            if (run == 0) runStart = i;
            ++run;
            if (run > bestLen) {
                bestLen = run;
                bestStart = runStart;
            }
        } else {
            run = 0;
        }
    }
    if (bestLen < 2) bestStart = -1;
    std::string out;
    for (int i = 0; i < 8;) {
        if (i == bestStart) {
            out += "::";
            i += bestLen;
            continue;
        }
        if (!out.empty() && out.back() != ':') out += ':';
        out += fmt("%x", a.w[i]);
        ++i;
    }
    return out;
}

V6 network_v6(const V6& a, int prefix) {
    V6 n = a;
    for (int i = 0; i < 8; ++i) {
        const int bitStart = i * 16;
        if (prefix <= bitStart) {
            n.w[i] = 0;
        } else if (prefix < bitStart + 16) {
            const int keep = prefix - bitStart;
            n.w[i] = static_cast<uint16_t>(n.w[i] & (0xFFFFu << (16 - keep)));
        }
    }
    return n;
}

// "ip/pfx" in the first token becomes two tokens.
void expand_slash(Tokens& t) {
    if (t.empty()) return;
    const size_t slash = t[0].find('/');
    if (slash == std::string::npos) return;
    const std::string ip = t[0].substr(0, slash);
    const std::string pfx = t[0].substr(slash + 1);
    t[0] = ip;
    t.insert(t.begin() + 1, pfx);
}

std::string subnet_split(const Tokens& t, bool byHosts, bool& ok) {
    if (t.size() < 3) return fail(ok, byHosts ? "need: hosts network prefix hosts_per_net [index]"
                                              : "need: split network prefix N [index]");
    uint32_t netIp = 0;
    if (!parse_ipv4(t[0], netIp)) return fail(ok, "bad IPv4");
    long long basePfx = 0;
    if (!parse_int_range(t[1], basePfx, 0, 32)) return fail(ok, "prefix must be 0..32");
    netIp &= prefix_to_mask(static_cast<int>(basePfx));

    int newPfx = static_cast<int>(basePfx);
    long long requested = 0;
    if (!byHosts) {
        if (!parse_int_range(t[2], requested, 1, 1 << 20)) return fail(ok, "N must be integer >= 1");
        int bits = 0;
        long long need = 1;
        while (need < requested) {
            ++bits;
            need <<= 1;
        }
        if (basePfx + bits > 32) return fail(ok, "too many subnets");
        newPfx = static_cast<int>(basePfx) + bits;
    } else {
        if (!parse_int_range(t[2], requested, 1, 1LL << 30)) return fail(ok, "hosts must be integer >= 1");
        const uint64_t need = static_cast<uint64_t>(requested) + 2;
        int hostBits = 0;
        uint64_t cap = 1;
        while (cap < need) {
            ++hostBits;
            cap <<= 1;
        }
        newPfx = 32 - hostBits;
        if (newPfx < basePfx) return fail(ok, "hosts too large for prefix");
    }
    const int bits = newPfx - static_cast<int>(basePfx);
    const uint64_t count = 1ull << bits;
    const uint64_t step = 1ull << (32 - newPfx);

    Out o;
    o.add("plan", fmt("%llu x /%d (%s) in %s/%d", static_cast<unsigned long long>(count), newPfx,
                      fmt_ipv4(prefix_to_mask(newPfx)).c_str(), fmt_ipv4(netIp).c_str(),
                      static_cast<int>(basePfx)));

    if (t.size() >= 4) {
        long long idx = 0;
        if (!parse_int_range(t[3], idx, 1, static_cast<long long>(count))) return fail(ok, "index out of range");
        const uint32_t sub = netIp + static_cast<uint32_t>(static_cast<uint64_t>(idx - 1) * step);
        o.add("subnet", fmt("%lld of %llu", idx, static_cast<unsigned long long>(count)));
        v4_details(o, sub, newPfx);
        return o.str();
    }
    const uint64_t shown = count > 16 ? 16 : count;
    for (uint64_t i = 0; i < shown; ++i) {
        const uint32_t sub = netIp + static_cast<uint32_t>(i * step);
        uint32_t first = 0, last = 0;
        first_last(sub, newPfx, first, last);
        o.line(fmt("%llu) %s/%d  %s-%s", static_cast<unsigned long long>(i + 1), fmt_ipv4(sub).c_str(),
                   newPfx, fmt_ipv4(first).c_str(), fmt_ipv4(last).c_str()));
    }
    if (count > shown)
        o.line(fmt("... +%llu more (append index to view one)", static_cast<unsigned long long>(count - shown)));
    return o.str();
}

std::string run_subnet(Tokens t, bool& ok) {
    enum { V4, Mask, Split, Hosts, V6Mode };
    int mode = pick_mode(t, {{"v4", V4}, {"4", V4}, {"ipv4", V4}, {"pfx", V4}, {"prefix", V4},
                             {"mask", Mask}, {"v4mask", Mask},
                             {"split", Split}, {"n", Split},
                             {"hosts", Hosts}, {"h", Hosts}, {"vlsm", Hosts},
                             {"v6", V6Mode}, {"6", V6Mode}, {"ipv6", V6Mode}});
    if (mode < 0) { // legacy: "ip/prefix" or "ip prefix"
        if (!t.empty() && t[0].find(':') != std::string::npos) mode = V6Mode;
        else if (!t.empty() && t[0].find('.') != std::string::npos) mode = V4;
        else return fail(ok, "unknown mode; use v4|mask|split|hosts|v6 or ip/prefix");
    }
    expand_slash(t);

    switch (mode) {
    case V4: {
        if (t.size() < 2) return fail(ok, "need: ip prefix  (or ip/prefix)");
        uint32_t ip = 0;
        if (!parse_ipv4(t[0], ip)) return fail(ok, "bad IPv4");
        long long pfx = 0;
        if (!parse_int_range(t[1], pfx, 0, 32)) return fail(ok, "prefix must be 0..32");
        Out o;
        v4_details(o, ip, static_cast<int>(pfx));
        return o.str();
    }
    case Mask: {
        if (t.size() < 2) return fail(ok, "need: mask ip netmask");
        uint32_t ip = 0, mask = 0;
        if (!parse_ipv4(t[0], ip)) return fail(ok, "bad IPv4");
        if (!parse_ipv4(t[1], mask)) return fail(ok, "bad mask");
        const int pfx = mask_to_prefix(mask);
        if (pfx < 0) return fail(ok, "mask not contiguous");
        Out o;
        v4_details(o, ip, pfx);
        return o.str();
    }
    case Split: return subnet_split(t, false, ok);
    case Hosts: return subnet_split(t, true, ok);
    default: break;
    }

    // IPv6
    if (t.empty()) return fail(ok, "need: v6 address [prefix]");
    V6 a;
    if (!parse_ipv6(t[0], a)) return fail(ok, "bad IPv6 address");
    long long pfx = 64;
    if (t.size() >= 2 && !parse_int_range(t[1], pfx, 0, 128)) return fail(ok, "prefix must be 0..128");
    Out o;
    o.add("expanded", expand_v6(a));
    o.add("compressed", compress_v6(a));
    o.add("network", compress_v6(network_v6(a, static_cast<int>(pfx))) + "/" + std::to_string(pfx));
    const int hostBits = 128 - static_cast<int>(pfx);
    o.add("addresses", hostBits == 0 ? std::string("1") : "2^" + std::to_string(hostBits));
    return o.str();
}

// ---------------------------------------------------------------------------
// Number bases, byte units, bitmask, CRC
// ---------------------------------------------------------------------------

std::string to_base(uint64_t v, int base) {
    static const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (v == 0) return "0";
    std::string out;
    while (v) {
        out.insert(out.begin(), digits[v % static_cast<unsigned>(base)]);
        v /= static_cast<unsigned>(base);
    }
    return out;
}

// Group digits from the right (space every `group`).
std::string group_right(const std::string& digits, size_t group) {
    std::string out;
    const size_t n = digits.size();
    for (size_t i = 0; i < n; ++i) {
        if (i && (n - i) % group == 0) out += ' ';
        out += digits[i];
    }
    return out;
}

std::string pad_left(std::string s, size_t width) {
    while (s.size() < width) s.insert(s.begin(), '0');
    return s;
}

std::string bin_str(uint64_t v, int width) {
    return group_right(pad_left(to_base(v, 2), static_cast<size_t>(width)), 4);
}

std::string hex_str(uint64_t v, int width) {
    return group_right(pad_left(to_base(v, 16), static_cast<size_t>(width / 4)), 4);
}

uint64_t width_mask(int width) { return width >= 64 ? ~0ull : ((1ull << width) - 1); }

// Value text with optional 0x/0b/0o prefix; prefix wins over the default base.
bool parse_base_value(std::string t, int base, uint64_t& out) {
    if (t.size() > 2 && t[0] == '0') {
        const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(t[1])));
        if (p == 'x' && base < 34) { base = 16; t = t.substr(2); }
        else if (p == 'b' && base < 12) { base = 2; t = t.substr(2); }
        else if (p == 'o' && base < 25) { base = 8; t = t.substr(2); }
    }
    return parse_u64(t, base, out);
}

void base_views(Out& o, uint64_t v, int width) {
    if (width) v &= width_mask(width);
    o.add("bin", bin_str(v, width));
    o.add("oct", to_base(v, 8));
    o.add("dec", std::to_string(static_cast<unsigned long long>(v)));
    o.add("hex", hex_str(v, width ? width : 0));
    if (width) {
        int64_t s = static_cast<int64_t>(v);
        if (width < 64 && (v & (1ull << (width - 1)))) s = static_cast<int64_t>(v | ~width_mask(width));
        o.add("2's comp", std::to_string(static_cast<long long>(s)));
    }
}

std::string run_base(Tokens t, bool& ok) {
    enum { Conv, All, Pow2 };
    int mode = pick_mode(t, {{"conv", Conv}, {"convert", Conv}, {"all", All}, {"show", All},
                             {"pow", Pow2}, {"2^n", Pow2}, {"p2", Pow2}});
    if (mode < 0) {
        if (t.size() == 2) mode = All;
        else mode = Conv;
    }

    if (mode == Pow2) {
        long long n = 0;
        if (t.empty() || !parse_int_range(t[0], n, 0, 63)) return fail(ok, "n must be integer 0..63");
        const uint64_t v = 1ull << n;
        Out o;
        o.add("2^n", std::to_string(static_cast<unsigned long long>(v)));
        o.add("hex", "0x" + to_base(v, 16));
        static const char* names[] = {"KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
        if (n >= 10 && n % 10 == 0) o.add("boundary", std::string("1 ") + names[n / 10 - 1]);
        return o.str();
    }

    if (mode == All) { // value base [width]
        if (t.size() < 2) return fail(ok, "need: all value base [width]");
        long long base = 0;
        if (!parse_int_range(t[1], base, 2, 36)) return fail(ok, "base must be 2..36");
        uint64_t v = 0;
        if (!parse_base_value(t[0], static_cast<int>(base), v)) return fail(ok, "bad value for base");
        long long width = 0;
        if (t.size() >= 3 && !parse_int_range(t[2], width, 8, 64)) return fail(ok, "width must be 8/16/32/64");
        if (width != 0 && width != 8 && width != 16 && width != 32 && width != 64)
            return fail(ok, "width must be 8/16/32/64");
        Out o;
        base_views(o, v, static_cast<int>(width));
        return o.str();
    }

    // Conv: value from to [width]
    if (t.size() < 3) return fail(ok, "need: value from_base to_base  e.g. FF 16 10");
    long long from = 0, to = 0;
    if (!parse_int_range(t[1], from, 2, 36) || !parse_int_range(t[2], to, 2, 36))
        return fail(ok, "bases must be 2..36");
    uint64_t v = 0;
    if (!parse_base_value(t[0], static_cast<int>(from), v)) return fail(ok, "bad value for base");
    long long width = 0;
    if (t.size() >= 4) {
        if (!parse_int_range(t[3], width, 8, 64) ||
            (width != 8 && width != 16 && width != 32 && width != 64))
            return fail(ok, "width must be 8/16/32/64");
        v &= width_mask(static_cast<int>(width));
    }
    Out o;
    o.add("base " + std::to_string(to), to_base(v, static_cast<int>(to)));
    base_views(o, v, static_cast<int>(width));
    return o.str();
}

std::string run_bytes(Tokens t, bool& ok) {
    skip_mode(t, {"convert", "conv", "bytes"});
    if (t.empty()) return fail(ok, "need: count [B|KB|MB|GB|TB|KiB|MiB|GiB|TiB]");
    double n = 0;
    std::string unit = "b";
    if (!split_suffix(t[0], n, unit) || n < 0) return fail(ok, "count must be a number >= 0");
    if (t.size() >= 2) unit = lower(t[1]);
    if (unit.empty()) unit = "b";

    double mult = 0;
    if (unit == "b") mult = 1.0;
    else if (unit == "kb") mult = 1e3;
    else if (unit == "mb") mult = 1e6;
    else if (unit == "gb") mult = 1e9;
    else if (unit == "tb") mult = 1e12;
    else if (unit == "kib") mult = 1024.0;
    else if (unit == "mib") mult = 1048576.0;
    else if (unit == "gib") mult = 1073741824.0;
    else if (unit == "tib") mult = 1099511627776.0;
    else return fail(ok, "unit: B KB MB GB TB KiB MiB GiB TiB");
    const double bytes = n * mult;

    // Plain (non-exponent) formatting for large byte counts.
    auto plain = [](double v) { return v >= 1e6 && v < 1e15 ? fmt("%.0f", v) : sig(v); };

    Out o;
    o.add("bytes", plain(bytes));
    o.add("KB/MB/GB/TB", plain(bytes / 1e3) + " / " + plain(bytes / 1e6) + " / " + plain(bytes / 1e9) + " / " +
                             plain(bytes / 1e12));
    o.add("KiB/MiB/GiB/TiB", plain(bytes / 1024.0) + " / " + plain(bytes / 1048576.0) + " / " +
                                 plain(bytes / 1073741824.0) + " / " + plain(bytes / 1099511627776.0));
    double dec = 1e3, bin = 1024.0;
    const char* pair = "KB vs KiB";
    if (bytes >= 1e12) { dec = 1e12; bin = 1099511627776.0; pair = "TB vs TiB"; }
    else if (bytes >= 1e9) { dec = 1e9; bin = 1073741824.0; pair = "GB vs GiB"; }
    else if (bytes >= 1e6) { dec = 1e6; bin = 1048576.0; pair = "MB vs MiB"; }
    o.add(pair, sig((bin - dec) / dec * 100.0) + "% gap");
    return o.str();
}

std::string run_bitmask(Tokens t, bool& ok) {
    enum { Bits, Hex };
    int mode = pick_mode(t, {{"bits", Bits}, {"list", Bits}, {"set", Bits},
                             {"hex", Hex}, {"mask", Hex}});
    int width = 32;
    // Optional leading width (only when more fields follow).
    if (t.size() >= 2) {
        long long w = 0;
        if (parse_int_range(t[0], w, 8, 64) && (w == 8 || w == 16 || w == 32 || w == 64)) {
            width = static_cast<int>(w);
            t.erase(t.begin());
        }
    }
    if (t.empty()) return fail(ok, "need: [bits|hex] [width] list-or-hex  e.g. bits 32 0,3,7");
    if (mode < 0) mode = (lower(t[0]).rfind("0x", 0) == 0) ? Hex : Bits;

    const uint64_t wm = width_mask(width);
    uint64_t mask = 0;
    if (mode == Hex) {
        std::string hx = t[0];
        if (hx.size() > 2 && hx[0] == '0' && (hx[1] == 'x' || hx[1] == 'X')) hx = hx.substr(2);
        uint64_t v = 0;
        if (!parse_u64(hx, 16, v)) return fail(ok, "bad hex mask");
        if (width < 64 && v > wm) return fail(ok, "hex wider than width");
        mask = v;
    } else {
        std::string joined;
        for (const auto& s : t) joined += s + ",";
        std::string tok;
        bool any = false;
        for (size_t i = 0; i <= joined.size(); ++i) {
            const char c = i < joined.size() ? joined[i] : ',';
            if (c == ',' || c == ' ') {
                if (!tok.empty()) {
                    long long b = 0;
                    if (!parse_int_range(tok, b, 0, width - 1)) return fail(ok, "bit must be 0.." + std::to_string(width - 1));
                    mask |= 1ull << b;
                    any = true;
                    tok.clear();
                }
            } else {
                tok += c;
            }
        }
        if (!any) return fail(ok, "enter set bits e.g. 0,3,7");
    }
    mask &= wm;

    Out o;
    o.add("mask", "0x" + hex_str(mask, width));
    o.add("binary", bin_str(mask, width));
    o.add("inverted", "0x" + hex_str(~mask & wm, width));
    o.add("dec", std::to_string(static_cast<unsigned long long>(mask)));
    std::string setBits;
    for (int b = 0; b < width; ++b) {
        if (mask & (1ull << b)) {
            if (!setBits.empty()) setBits += ",";
            setBits += std::to_string(b);
        }
    }
    o.add("set bits", setBits.empty() ? std::string("none") : setBits);
    return o.str();
}

uint32_t crc32_calc(const uint8_t* d, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint16_t crc16_ccitt(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(d[i]) << 8));
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint16_t crc16_modbus(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc = static_cast<uint16_t>(crc ^ d[i]);
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
    }
    return crc;
}

// "01 02 03", "0102", "0x01,0x02" -> bytes.
bool parse_hex_bytes(const Tokens& t, std::vector<uint8_t>& out) {
    out.clear();
    std::string hex;
    for (std::string tok : t) {
        for (char& c : tok)
            if (c == ',' || c == ':' || c == '_' || c == ';') c = ' ';
        for (const auto& piece : split_ws(tok)) {
            size_t i = 0;
            if (piece.size() > 2 && piece[0] == '0' && (piece[1] == 'x' || piece[1] == 'X')) i = 2;
            for (; i < piece.size(); ++i) {
                if (!std::isxdigit(static_cast<unsigned char>(piece[i]))) return false;
                hex += piece[i];
            }
        }
    }
    if (hex.empty() || hex.size() % 2) return false;
    for (size_t i = 0; i < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    return true;
}

std::string run_crc(const std::string& raw, Tokens t, bool& ok) {
    enum { C32, Ccitt, Modbus, All, Text };
    const int mode = pick_mode(t, {{"crc32", C32}, {"32", C32}, {"crc32hex", C32},
                                   {"ccitt", Ccitt}, {"crc16", Ccitt}, {"16", Ccitt},
                                   {"modbus", Modbus},
                                   {"all", All}, {"hex", All},
                                   {"text", Text}});
    std::vector<uint8_t> bytes;
    if (mode < 0 || mode == Text) {
        // No mode: CRC-32 of the raw text. "text <str>": all CRCs of the string.
        const std::string text = mode < 0 ? raw : rest_after_first(raw);
        if (text.empty()) return fail(ok, "need text");
        bytes.assign(text.begin(), text.end());
    } else if (!parse_hex_bytes(t, bytes)) {
        return fail(ok, "need hex bytes (even number of digits)");
    }
    const uint8_t* d = bytes.data();
    const size_t n = bytes.size();

    Out o;
    o.add("bytes", std::to_string(n));
    if (mode < 0) {
        o.add("CRC-32", fmt("%08X", crc32_calc(d, n)));
        return o.str();
    }
    if (mode == C32 || mode == All || mode == Text) o.add("CRC-32", fmt("%08X", crc32_calc(d, n)));
    if (mode == Ccitt || mode == All || mode == Text) o.add("CCITT-FALSE", fmt("%04X", crc16_ccitt(d, n)));
    if (mode == Modbus || mode == All || mode == Text) o.add("MODBUS", fmt("%04X", crc16_modbus(d, n)));
    return o.str();
}

// ---------------------------------------------------------------------------
// Electronics
// ---------------------------------------------------------------------------

std::string resistor_from_bands(const std::vector<int>& bands, bool& ok) {
    const size_t n = bands.size();
    if (n < 4 || n > 6) return fail(ok, "need 4..6 color bands");
    const size_t digs = n >= 5 ? 3 : 2;
    double val = 0;
    for (size_t i = 0; i < digs; ++i) {
        if (bands[i] > 9) return fail(ok, "digit band can't be gold/silver");
        val = val * 10.0 + bands[i];
    }
    const double mult = band_mult(bands[digs]);
    if (mult < 0) return fail(ok, "bad multiplier band");
    const double ohms = val * mult;
    Out o;
    o.add("ohms", fmt_ohms(ohms));
    o.add("tolerance", band_tol(bands[digs + 1]));
    if (n == 6) o.add("tempco", band_tempco(bands[5]));
    o.add("E12", fmt_ohms(nearest_e12(ohms)));
    o.add("E24", fmt_ohms(nearest_e24(ohms)));
    return o.str();
}

std::string run_resistor(Tokens t, bool& ok) {
    enum { B4, B5, B6, Colors, Reverse };
    int mode = pick_mode(t, {{"4", B4}, {"4band", B4}, {"5", B5}, {"5band", B5}, {"6", B6}, {"6band", B6},
                             {"colors", Colors}, {"color", Colors}, {"type", Colors}, {"c", Colors},
                             {"rev", Reverse}, {"reverse", Reverse}, {"r", Reverse}});
    if (mode < 0) {
        if (t.empty()) return fail(ok, "need colors or an ohm value");
        mode = color_index(t[0]) >= 0 ? Colors : Reverse;
    }
    if (t.empty()) return fail(ok, "need input");

    if (mode == Reverse) {
        double ohms = 0;
        if (!parse_ohms(t[0], ohms) || !(ohms > 0)) return fail(ok, "bad ohms value (e.g. 4.7k, 4k7, 220R)");
        Out o;
        o.add("ohms", fmt_ohms(ohms));
        o.add("5% 4-band", bands_from_ohms(ohms, 2, 5.0));
        o.add("1% 5-band", bands_from_ohms(ohms, 3, 1.0));
        o.add("E12", fmt_ohms(nearest_e12(ohms)));
        o.add("E24", fmt_ohms(nearest_e24(ohms)));
        return o.str();
    }

    std::vector<int> bands;
    for (const auto& tok : t) {
        const int idx = color_index(tok);
        if (idx < 0) return fail(ok, "unknown color: " + tok);
        bands.push_back(idx);
    }
    const size_t want = mode == B4 ? 4 : mode == B5 ? 5 : mode == B6 ? 6 : 0;
    if (want && bands.size() != want) return fail(ok, "need exactly " + std::to_string(want) + " colors");
    return resistor_from_bands(bands, ok);
}

std::string run_led(Tokens t, bool& ok) {
    skip_mode(t, {"series", "led"});
    if (t.size() < 2) return fail(ok, "need: Vs Vf [mA=20]");
    double vs = 0, vf = 0, ma = 20;
    if (!parse_double(t[0], vs) || !(vs > 0)) return fail(ok, "Vs must be > 0");
    if (!parse_double(t[1], vf) || vf < 0) return fail(ok, "Vf must be >= 0");
    if (t.size() >= 3 && (!parse_double(t[2], ma) || !(ma > 0))) return fail(ok, "mA must be > 0");
    if (vf >= vs) return fail(ok, "Vf must be below supply");
    const double i = ma / 1000.0;
    const double r = (vs - vf) / i;
    const double e24 = nearest_e24(r);
    Out o;
    o.add("R", fmt_ohms(r));
    o.add("E12", fmt_ohms(nearest_e12(r)));
    o.add("E24", fmt_ohms(e24));
    o.add("P (R)", sig((vs - vf) * i) + " W");
    o.add("I @E24", sig((vs - vf) / e24 * 1000.0) + " mA");
    if ((vs - vf) * i > 0.25) o.line("warn: P > 0.25 W");
    return o.str();
}

std::string run_divider(Tokens t, bool& ok) {
    enum { Forward, Design, Ratio };
    int mode = pick_mode(t, {{"div", Forward}, {"forward", Forward}, {"vout", Forward}, {"r1r2vin", Forward},
                             {"design", Design}, {"find", Design}, {"r2", Design}, {"vr1", Design},
                             {"ratio", Ratio}});
    if (mode < 0) mode = t.size() == 1 ? Ratio : Forward;

    if (mode == Forward) { // R1 R2 Vin
        if (t.size() < 3) return fail(ok, "need: R1 R2 Vin");
        double r1 = 0, r2 = 0, vin = 0;
        if (!parse_ohms(t[0], r1) || !(r1 > 0) || !parse_ohms(t[1], r2) || !(r2 > 0))
            return fail(ok, "R1 and R2 must be > 0");
        if (!parse_double(t[2], vin) || !(vin > 0)) return fail(ok, "Vin must be > 0");
        const double i = vin / (r1 + r2);
        Out o;
        o.add("Vout", sig(vin * r2 / (r1 + r2)));
        o.add("I", eng(i) + "A");
        o.add("P R1", sig(i * i * r1) + " W");
        o.add("P R2", sig(i * i * r2) + " W");
        return o.str();
    }
    if (mode == Design) { // Vin Vout R1
        if (t.size() < 3) return fail(ok, "need: Vin Vout R1");
        double vin = 0, vout = 0, r1 = 0;
        if (!parse_double(t[0], vin) || !(vin > 0)) return fail(ok, "Vin must be > 0");
        if (!parse_double(t[1], vout) || !(vout > 0)) return fail(ok, "Vout must be > 0");
        if (!parse_ohms(t[2], r1) || !(r1 > 0)) return fail(ok, "R1 must be > 0");
        if (vout >= vin) return fail(ok, "need 0 < Vout < Vin");
        const double r2 = r1 * vout / (vin - vout);
        const double r2s = nearest_e24(r2);
        const double i = vin / (r1 + r2s);
        Out o;
        o.add("R2", fmt_ohms(r2));
        o.add("R2 E24", fmt_ohms(r2s));
        o.add("Vout @E24", sig(vin * r2s / (r1 + r2s)));
        o.add("I", eng(i) + "A");
        o.add("P R1", sig(i * i * r1) + " W");
        o.add("P R2", sig(i * i * r2s) + " W");
        return o.str();
    }
    // Ratio: suggest standard-value pairs.
    double ratio = 0;
    if (t.empty() || !parse_double(t[0], ratio) || !(ratio > 1e-9 && ratio < 1.0 - 1e-9))
        return fail(ok, "ratio Vout/Vin must be between 0 and 1");
    const double rr = ratio / (1.0 - ratio); // R2/R1
    Out o;
    o.add("target", sig(ratio));
    for (int series = 0; series < 2; ++series) {
        const bool e24 = series == 1;
        for (double scale : {1e3, 4.7e3, 1e4, 4.7e4}) {
            const double r1 = e24 ? nearest_e24(scale) : nearest_e12(scale);
            const double r2 = e24 ? nearest_e24(r1 * rr) : nearest_e12(r1 * rr);
            const double actual = r2 / (r1 + r2);
            o.line(fmt("%s R1=%s R2=%s -> %s (%+.2f%%)", e24 ? "E24" : "E12", fmt_ohms(r1).c_str(),
                       fmt_ohms(r2).c_str(), sig(actual).c_str(), (actual - ratio) / ratio * 100.0));
        }
    }
    return o.str();
}

std::string run_baud(Tokens t, bool& ok) {
    skip_mode(t, {"bit", "baud", "bit-time"});
    if (t.empty()) return fail(ok, "need: baud [bits_per_frame=10]");
    long long baud = 0, frameBits = 10;
    if (!parse_int_range(t[0], baud, 1, 100000000LL)) return fail(ok, "baud must be integer >= 1");
    if (t.size() >= 2 && !parse_int_range(t[1], frameBits, 2, 32)) return fail(ok, "frame bits must be 2..32");
    const double bit = 1.0 / static_cast<double>(baud);
    Out o;
    o.add("bit time", fmt_time(bit));
    o.add(fmt("%lld-bit frame", frameBits), fmt_time(bit * static_cast<double>(frameBits)));
    o.add("bytes/s", sig(static_cast<double>(baud) / static_cast<double>(frameBits)));
    return o.str();
}

std::string run_dbm(Tokens t, bool& ok) {
    skip_mode(t, {"convert", "power"});
    if (t.empty()) return fail(ok, "need: value [dBm|mW|W] [gain_dB]");
    // Accept "dbm 10 [gain]" as well as "10 dbm [gain]".
    const std::string first = lower(t[0]);
    if ((first == "dbm" || first == "mw" || first == "w") && t.size() >= 2) std::swap(t[0], t[1]);
    double v = 0;
    std::string unit = "dbm";
    double u = 0;
    std::string suffix;
    if (!split_suffix(t[0], u, suffix)) return fail(ok, "need a number");
    v = u;
    if (!suffix.empty()) unit = suffix;
    size_t next = 1;
    if (t.size() > next) {
        const std::string cand = lower(t[next]);
        if (cand == "dbm" || cand == "mw" || cand == "w") {
            unit = cand;
            ++next;
        }
    }
    double gain = 0;
    if (t.size() > next && !parse_double(t[next], gain)) return fail(ok, "bad gain dB");
    if (gain < -200 || gain > 200) return fail(ok, "gain out of range");

    double dbm = 0;
    if (unit == "dbm") dbm = v;
    else if (unit == "mw") {
        if (!(v > 0)) return fail(ok, "mW must be > 0");
        dbm = 10.0 * std::log10(v);
    } else if (unit == "w") {
        if (!(v > 0)) return fail(ok, "W must be > 0");
        dbm = 10.0 * std::log10(v * 1000.0);
    } else return fail(ok, "unit must be dBm, mW or W");
    dbm += gain;
    const double mw = std::pow(10.0, dbm / 10.0);
    Out o;
    o.add("dBm", sig(dbm));
    o.add("mW", sig(mw));
    o.add("W", sig(mw / 1000.0));
    if (dbm < -120.0 || dbm > 60.0) o.line("warn: outside bench range");
    return o.str();
}

// Needs "known" count == 2.  Positional V I R W (blank = "-"), "v=12 r=100", or "V R".
std::string run_ohm_dc(const Tokens& t, bool& ok) {
    bool has[4] = {false, false, false, false};
    double val[4] = {0, 0, 0, 0};
    if (t.empty()) return fail(ok, "need two of V I R W (use - for unknown)");

    bool keyed = false;
    for (const auto& s : t)
        if (s.find('=') != std::string::npos) keyed = true;
    bool anyBlank = false;
    for (const auto& s : t)
        if (is_blank(s)) anyBlank = true;

    if (keyed) {
        for (const auto& s : t) {
            const size_t eq = s.find('=');
            if (eq == std::string::npos) return fail(ok, "mix of keyed and positional");
            const std::string key = lower(s.substr(0, eq));
            int idx = key == "v" ? 0 : key == "i" ? 1 : key == "r" ? 2 : (key == "w" || key == "p") ? 3 : -1;
            if (idx < 0) return fail(ok, "key must be v, i, r or w");
            if (has[idx] || !parse_double(s.substr(eq + 1), val[idx])) return fail(ok, "bad value for " + key);
            has[idx] = true;
        }
    } else if (t.size() == 2 && !anyBlank) { // legacy: V R
        if (!parse_double(t[0], val[0]) || !parse_double(t[1], val[2])) return fail(ok, "need numbers: V R");
        has[0] = has[2] = true;
    } else {
        if (t.size() > 4) return fail(ok, "at most 4 fields: V I R W");
        for (size_t i = 0; i < t.size(); ++i) {
            Opt p;
            if (!parse_opt(t[i], p)) return fail(ok, "bad number: " + t[i]);
            has[i] = p.has;
            val[i] = p.v;
        }
    }
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (has[i] && val[i] < 0) return fail(ok, "values must be >= 0");
        n += has[i];
    }
    if (n < 2) return fail(ok, "enter any two of V I R W");
    if (n > 2) return fail(ok, "use only two fields");

    double &V = val[0], &I = val[1], &R = val[2], &W = val[3];
    const char* zero = "zero value: cannot solve";
    if (has[0] && has[1]) {
        if (!(I > 0)) return fail(ok, zero);
        R = V / I;
        W = V * I;
    } else if (has[0] && has[2]) {
        if (!(R > 0)) return fail(ok, zero);
        I = V / R;
        W = V * V / R;
    } else if (has[0] && has[3]) {
        if (!(V > 0) || !(W > 0)) return fail(ok, zero);
        I = W / V;
        R = V * V / W;
    } else if (has[1] && has[2]) {
        V = I * R;
        W = I * I * R;
    } else if (has[1] && has[3]) {
        if (!(I > 0)) return fail(ok, zero);
        V = W / I;
        R = W / (I * I);
    } else {
        if (!(R > 0)) return fail(ok, zero);
        V = std::sqrt(W * R);
        I = std::sqrt(W / R);
    }
    Out o;
    o.add("V", sig(V) + (has[0] ? "" : " *"));
    o.add("I", sig(I) + (has[1] ? "" : " *"));
    o.add("R", sig(R) + (has[2] ? "" : " *"));
    o.add("W", sig(W) + (has[3] ? "" : " *"));
    o.add("hp", sig(W / 745.7));
    return o.str();
}

std::string run_ohm(Tokens t, bool& ok) {
    enum { Dc, Three };
    int mode = pick_mode(t, {{"dc", Dc}, {"3p", Three}, {"3ph", Three}, {"3phase", Three}, {"three", Three}});
    if (mode < 0) mode = Dc;
    if (mode == Dc) return run_ohm_dc(t, ok);

    if (t.size() < 2) return fail(ok, "need: V I [pf=1]");
    double v = 0, i = 0, pf = 1.0;
    if (!parse_double(t[0], v) || !(v > 0)) return fail(ok, "V must be > 0");
    if (!parse_double(t[1], i) || !(i > 0)) return fail(ok, "I must be > 0");
    if (t.size() >= 3 && (!parse_double(t[2], pf) || pf < 0.0 || pf > 1.0)) return fail(ok, "pf must be 0..1");
    const double s = std::sqrt(3.0) * v * i;
    const double w = s * pf;
    Out o;
    o.add("W", sig(w));
    o.add("kW", sig(w / 1000.0));
    o.add("VA", sig(s));
    o.add("hp", sig(w / 745.7));
    return o.str();
}

// Accepts "50", "50m", "50ft". Returns meters.
bool parse_length_m(const std::string& tok, const char* defUnit, double& m) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty()) u = defUnit;
    if (u == "m") m = v;
    else if (u == "mm") m = v / 1000.0;
    else if (u == "cm") m = v / 100.0;
    else if (u == "km") m = v * 1000.0;
    else if (u == "ft") m = v * 0.3048;
    else if (u == "in") m = v * 0.0254;
    else return false;
    return true;
}

std::string run_vdrop(Tokens t, bool& ok) {
    enum { Cu, Al };
    int mat = pick_mode(t, {{"copper", Cu}, {"cu", Cu}, {"aluminum", Al}, {"aluminium", Al}, {"al", Al}});
    if (mat < 0) mat = Cu;
    if (t.size() < 4) return fail(ok, "need: [cu|al] length amps volts AWG");
    double lenM = 0, amps = 0, volts = 0;
    long long gauge = 0;
    if (!parse_length_m(t[0], "m", lenM) || !(lenM > 0)) return fail(ok, "length must be > 0 (e.g. 15 or 50ft)");
    if (!parse_double(t[1], amps) || !(amps > 0)) return fail(ok, "amps must be > 0");
    if (!parse_double(t[2], volts) || !(volts > 0)) return fail(ok, "volts must be > 0");
    if (!parse_int_range(t[3], gauge, 0, 40)) return fail(ok, "AWG must be integer 0..40");

    const double rho = mat == Al ? 2.82e-8 : 1.724e-8;
    auto drop_v = [&](int g) { return amps * rho * lenM / (awg_area_mm2(g) * 1e-6); };
    const double r = rho * lenM / (awg_area_mm2(static_cast<int>(gauge)) * 1e-6);
    const double drop = amps * r;

    auto thinnest_under = [&](double limitPct) {
        int best = -1;
        for (int g = 0; g <= 40; ++g)
            if (drop_v(g) / volts * 100.0 <= limitPct) best = g;
        return best;
    };
    const int g3 = thinnest_under(3.0), g5 = thinnest_under(5.0);

    Out o;
    o.add("drop", sig(drop) + " V (one-way)");
    o.add("drop %", sig(drop / volts * 100.0));
    o.add("round trip", sig(drop * 2.0) + " V");
    o.add("R one-way", sig(r) + " ohm");
    o.add("under 3%", g3 >= 0 ? std::to_string(g3) + " AWG" : std::string("none"));
    o.add("under 5%", g5 >= 0 ? std::to_string(g5) + " AWG" : std::string("none"));
    return o.str();
}

// ---------------------------------------------------------------------------
// Epoch
// ---------------------------------------------------------------------------

bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

int64_t days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yy + (m <= 2));
}

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

bool parse_i64(const std::string& s, int64_t& out) {
    long long v = 0;
    if (!parse_int_range(s, v, INT64_MIN, INT64_MAX)) return false;
    out = v;
    return true;
}

std::string run_epoch(Tokens t, bool& ok) {
    enum { Sec, Ms, Date };
    int mode = pick_mode(t, {{"s", Sec}, {"sec", Sec}, {"unix", Sec}, {"secs", Sec},
                             {"ms", Ms}, {"milli", Ms}, {"millis", Ms},
                             {"date", Date}, {"dt", Date}, {"iso", Date}, {"datetime", Date}});
    if (t.empty()) return fail(ok, "need: [s|ms|date] value");
    if (mode < 0) {
        const std::string& f = t[0];
        mode = (f.find('-', 1) != std::string::npos || f.find(':') != std::string::npos) ? Date : Sec;
    }

    int64_t sec = 0, ms = 0;
    if (mode == Sec) {
        if (!parse_i64(t[0], sec)) return fail(ok, "seconds must be an integer");
        ms = sec * 1000;
    } else if (mode == Ms) {
        if (!parse_i64(t[0], ms)) return fail(ok, "millis must be an integer");
        sec = floor_div(ms, 1000);
    } else {
        std::string s;
        for (const auto& tok : t) s += tok + " ";
        std::string norm;
        for (char c : s) {
            if (c == 'T' || c == 't') norm += ' ';
            else if (c != 'Z' && c != 'z') norm += c;
        }
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        const int n = std::sscanf(norm.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
        if (n != 3 && n != 5 && n != 6) return fail(ok, "bad date: YYYY-MM-DD [HH:MM[:SS]]");
        if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h < 0 || h > 23 || mi < 0 || mi > 59 ||
            se < 0 || se > 60)
            return fail(ok, "date/time out of range");
        sec = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
        ms = sec * 1000;
    }

    const int64_t days = floor_div(sec, 86400);
    const int64_t rem = sec - days * 86400;
    int y = 0, mo = 0, d = 0;
    civil_from_days(days, y, mo, d);
    static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int dow = static_cast<int>(((days % 7) + 11) % 7); // 1970-01-01 = Thu
    Out o;
    o.add("seconds", std::to_string(static_cast<long long>(sec)));
    o.add("millis", std::to_string(static_cast<long long>(ms)));
    o.add("UTC ISO", fmt("%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, static_cast<int>(rem / 3600),
                         static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60)));
    o.add("weekday", wd[dow]);
    return o.str();
}

// ---------------------------------------------------------------------------
// Measure (temp / length / AWG)
// ---------------------------------------------------------------------------

bool temp_to_k(double v, const std::string& u, double& k) {
    if (u == "k") k = v;
    else if (u == "c") k = v + 273.15;
    else if (u == "f") k = (v - 32.0) * 5.0 / 9.0 + 273.15;
    else return false;
    return true;
}

bool is_temp_unit(const std::string& u) { return u == "c" || u == "f" || u == "k"; }

bool len_to_mm(double v, const std::string& u, double& mm) {
    if (u == "mm") mm = v;
    else if (u == "in" || u == "inch") mm = v * 25.4;
    else if (u == "mil") mm = v * 0.0254;
    else return false;
    return true;
}

bool is_len_unit(const std::string& u) { return u == "mm" || u == "in" || u == "inch" || u == "mil"; }

std::string measure_temp(const Tokens& t, bool& ok) {
    if (t.empty()) return fail(ok, "need: temp value [C|F|K]");
    double v = 0;
    std::string u;
    if (!split_suffix(t[0], v, u)) return fail(ok, "need temperature");
    if (t.size() >= 2) u = lower(t[1]);
    if (u.empty()) u = "c";
    double k = 0;
    if (!temp_to_k(v, u, k)) return fail(ok, "unit must be C, F or K");
    if (!(k > 0)) return fail(ok, "temp must be > 0 K");
    Out o;
    o.add("C", sig(k - 273.15));
    o.add("F", sig((k - 273.15) * 9.0 / 5.0 + 32.0));
    o.add("K", sig(k));
    return o.str();
}

std::string measure_len(const Tokens& t, bool& ok) {
    if (t.empty()) return fail(ok, "need: len value [mm|in|mil]");
    double v = 0;
    std::string u;
    if (!split_suffix(t[0], v, u) || !(v > 0)) return fail(ok, "length must be > 0");
    if (t.size() >= 2) u = lower(t[1]);
    if (u.empty()) u = "mm";
    double mm = 0;
    if (!len_to_mm(v, u, mm)) return fail(ok, "unit must be mm, in or mil");
    Out o;
    o.add("mm", sig(mm));
    o.add("inch", sig(mm / 25.4));
    o.add("mil", sig(mm / 0.0254));
    return o.str();
}

std::string measure_awg(const Tokens& t, bool& ok) {
    // awg 24 | awg gauge 24 | awg dia 0.51 [mm|in|mil]
    size_t i = 0;
    bool asDia = false;
    if (!t.empty()) {
        const std::string f = lower(t[0]);
        if (f == "gauge" || f == "g") ++i;
        else if (f == "dia" || f == "d" || f == "diameter") {
            asDia = true;
            ++i;
        }
    }
    if (i >= t.size()) return fail(ok, "need: awg gauge | awg dia value [unit]");
    int g = 0;
    if (!asDia) {
        long long gl = 0;
        if (!parse_int_range(t[i], gl, 0, 40)) return fail(ok, "AWG must be integer 0..40");
        g = static_cast<int>(gl);
    } else {
        double v = 0;
        std::string u;
        if (!split_suffix(t[i], v, u) || !(v > 0)) return fail(ok, "diameter must be > 0");
        if (i + 1 < t.size()) u = lower(t[i + 1]);
        if (u.empty()) u = "mm";
        double mm = 0;
        if (!len_to_mm(v, u, mm)) return fail(ok, "unit must be mm, in or mil");
        g = nearest_awg(mm);
    }
    Out o;
    o.add("AWG", std::to_string(g));
    o.add("neighbors", std::to_string(g > 0 ? g - 1 : 0) + " / " + std::to_string(g < 40 ? g + 1 : 40));
    o.add("dia", sig(awg_dia_mm(g)) + " mm");
    o.add("area", sig(awg_area_mm2(g)) + " mm2");
    o.add("Cu", sig(awg_ohm_per_m_cu(g)) + " ohm/m");
    return o.str();
}

std::string run_measure(Tokens t, bool& ok) {
    enum { Temp, Len, Awg };
    int mode = pick_mode(t, {{"temp", Temp}, {"t", Temp}, {"len", Len}, {"length", Len}, {"l", Len},
                             {"awg", Awg}, {"a", Awg}});
    if (mode < 0) { // infer from unit
        if (t.size() < 2) return fail(ok, "need mode: temp | len | awg");
        const std::string u = lower(t[1]);
        if (is_temp_unit(u)) mode = Temp;
        else if (is_len_unit(u)) mode = Len;
        else return fail(ok, "need mode: temp | len | awg");
    }
    if (mode == Temp) return measure_temp(t, ok);
    if (mode == Len) return measure_len(t, ok);
    return measure_awg(t, ok);
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

struct UnitDef {
    const char* name;
    double toSi;
};

struct DimDef {
    const char* name;
    const UnitDef* units;
    int n;
};

const UnitDef kLen[] = {{"m", 1.0}, {"cm", 0.01}, {"mm", 0.001}, {"km", 1000.0}, {"in", 0.0254},
                        {"ft", 0.3048}, {"yd", 0.9144}, {"mil", 2.54e-5}, {"mi", 1609.344}};
const UnitDef kArea[] = {{"m2", 1.0}, {"cm2", 1e-4}, {"mm2", 1e-6}, {"in2", 0.00064516},
                         {"ft2", 0.09290304}, {"acre", 4046.8564224}, {"circular mil", 5.067075e-10}};
const UnitDef kVol[] = {{"m3", 1.0}, {"L", 0.001}, {"mL", 1e-6}, {"gal", 0.003785411784},
                        {"qt", 0.000946352946}, {"pt", 0.000473176473}, {"fl oz", 2.95735295625e-5},
                        {"in3", 1.6387064e-5}, {"ft3", 0.028316846592}, {"board-foot", 0.002359737216}};
const UnitDef kMass[] = {{"kg", 1.0}, {"g", 0.001}, {"mg", 1e-6}, {"lb", 0.45359237},
                         {"oz", 0.028349523125}, {"ton", 907.18474}};
const UnitDef kForce[] = {{"N", 1.0}, {"kN", 1000.0}, {"lbf", 4.4482216152605}, {"kgf", 9.80665}};
const UnitDef kPress[] = {{"Pa", 1.0}, {"kPa", 1000.0}, {"MPa", 1e6}, {"bar", 1e5}, {"psi", 6894.757293168},
                          {"atm", 101325.0}, {"inH2O", 249.08891}, {"mmHg", 133.322387415}};
const UnitDef kEnergy[] = {{"J", 1.0}, {"kJ", 1000.0}, {"cal", 4.184}, {"kcal", 4184.0},
                           {"Wh", 3600.0}, {"kWh", 3.6e6}, {"BTU", 1055.05585262}};
const UnitDef kPower[] = {{"W", 1.0}, {"kW", 1000.0}, {"hp", 745.7}, {"BTU/h", 0.29307107}};
const UnitDef kTemp[] = {{"C", 0}, {"F", 0}, {"K", 0}}; // special-cased
const UnitDef kFlow[] = {{"m3/s", 1.0}, {"L/min", 1.6666666667e-5}, {"L/s", 0.001},
                         {"gal/min", 6.30901964e-5}, {"ft3/min", 0.00047194745}};
const UnitDef kTorque[] = {{"N.m", 1.0}, {"lb.ft", 1.3558179483314}, {"oz.in", 0.0070615518}};
const UnitDef kSpeed[] = {{"m/s", 1.0}, {"km/h", 1.0 / 3.6}, {"mph", 0.44704}, {"ft/s", 0.3048},
                          {"knot", 0.514444}};

#define DIM(nm, arr) {nm, arr, static_cast<int>(sizeof(arr) / sizeof(arr[0]))}
const DimDef kDims[] = {DIM("length", kLen),   DIM("area", kArea),     DIM("volume", kVol),
                        DIM("mass", kMass),    DIM("force", kForce),   DIM("pressure", kPress),
                        DIM("energy", kEnergy), DIM("power", kPower),  DIM("temperature", kTemp),
                        DIM("flow", kFlow),    DIM("torque", kTorque), DIM("speed", kSpeed)};
#undef DIM
const int kDimN = static_cast<int>(sizeof(kDims) / sizeof(kDims[0]));

// Lowercase, drop spaces/dashes/dots/underscores and non-ASCII (so "N·m" == "nm").
std::string norm_unit(const std::string& s) {
    std::string out;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 128 || c == ' ' || c == '-' || c == '.' || c == '_') continue;
        out += static_cast<char>(std::tolower(u));
    }
    return out;
}

int find_unit(const DimDef& d, const std::string& name, bool exact) {
    const std::string nn = norm_unit(name);
    for (int i = 0; i < d.n; ++i) {
        if (exact ? name == d.units[i].name : nn == norm_unit(d.units[i].name)) return i;
    }
    return -1;
}

int find_dim_token(const std::string& tok) {
    const std::string l = lower(tok);
    if (l.size() < 3) return -1;
    for (int i = 0; i < kDimN; ++i)
        if (std::string(kDims[i].name).rfind(l, 0) == 0) return i;
    if (l == "temp") return 8;
    return -1;
}

double temp_in(double v, const std::string& u) {
    if (u == "K") return v;
    if (u == "C") return v + 273.15;
    return (v - 32.0) * 5.0 / 9.0 + 273.15;
}

double temp_out(double k, const std::string& u) {
    if (u == "K") return k;
    if (u == "C") return k - 273.15;
    return (k - 273.15) * 9.0 / 5.0 + 32.0;
}

std::string run_units(Tokens t, bool& ok) {
    skip_mode(t, {"convert", "conv"});
    if (!t.empty() && (lower(t[0]) == "list" || lower(t[0]) == "dims")) {
        Out o;
        if (t.size() >= 2) {
            const int d = find_dim_token(t[1]);
            if (d < 0) return fail(ok, "unknown dimension");
            std::string names;
            for (int i = 0; i < kDims[d].n; ++i) names += std::string(i ? " " : "") + kDims[d].units[i].name;
            o.add(kDims[d].name, names);
        } else {
            for (int d = 0; d < kDimN; ++d) {
                std::string names;
                for (int i = 0; i < kDims[d].n; ++i) names += std::string(i ? " " : "") + kDims[d].units[i].name;
                o.add(kDims[d].name, names);
            }
        }
        return o.str();
    }

    if (t.size() < 3) return fail(ok, "need: value [dimension] from to  e.g. 10 ft m");
    double v = 0;
    if (!parse_double(t[0], v)) return fail(ok, "need a number");

    int dim = -1, fromIdx = -1, toIdx = -1;
    std::string from, to;
    if (t.size() >= 4) {
        dim = find_dim_token(t[1]);
        if (dim >= 0) {
            from = t[2];
            to = t[3];
        }
    }
    if (dim < 0) {
        from = t[1];
        to = t[2];
        for (int pass = 0; pass < 2 && dim < 0; ++pass) {
            for (int d = 0; d < kDimN; ++d) {
                const int f = find_unit(kDims[d], from, pass == 0);
                const int g = find_unit(kDims[d], to, pass == 0);
                if (f >= 0 && g >= 0) {
                    dim = d;
                    break;
                }
            }
        }
        if (dim < 0) return fail(ok, "unknown or mismatched units (try: units list)");
    }
    fromIdx = find_unit(kDims[dim], from, true);
    if (fromIdx < 0) fromIdx = find_unit(kDims[dim], from, false);
    toIdx = find_unit(kDims[dim], to, true);
    if (toIdx < 0) toIdx = find_unit(kDims[dim], to, false);
    if (fromIdx < 0 || toIdx < 0) return fail(ok, std::string("unknown unit for ") + kDims[dim].name);

    const UnitDef& fu = kDims[dim].units[fromIdx];
    const UnitDef& tu = kDims[dim].units[toIdx];
    double result = 0;
    if (dim == 8) {
        const double k = temp_in(v, fu.name);
        if (!(k > 0)) return fail(ok, "temp must be > 0 K");
        result = temp_out(k, tu.name);
    } else {
        result = v * fu.toSi / tu.toSi;
    }
    Out o;
    o.add(tu.name, sig(result));
    o.add("from", sig(v) + " " + fu.name + " (" + kDims[dim].name + ")");
    return o.str();
}

// ---------------------------------------------------------------------------
// Mechanical / shop: motor, gas, flow
// ---------------------------------------------------------------------------

constexpr double kLbftPerNm = 1.0 / 1.3558179483314;
constexpr double kHpW = 745.7;

std::string run_motor(Tokens t, bool& ok) {
    skip_mode(t, {"hp", "motor"});
    if (t.size() < 3) return fail(ok, "need: hp torque rpm [eff%]  (- unknown; torque 50nm|50lbft)");
    Opt hp, rpm;
    if (!parse_opt(t[0], hp) || !parse_opt(t[2], rpm)) return fail(ok, "bad number");
    Opt tq;
    bool torqueImp = false;
    if (is_blank(t[1])) {
        tq.has = false;
    } else {
        std::string u;
        if (!split_suffix(t[1], tq.v, u)) return fail(ok, "bad torque");
        if (u.empty() || u == "nm" || u == "n.m") torqueImp = false;
        else if (u == "lbft" || u == "lb.ft" || u == "ftlb") torqueImp = true;
        else return fail(ok, "torque unit: nm or lbft");
        tq.has = true;
    }
    if ((hp.has && !(hp.v > 0)) || (tq.has && !(tq.v > 0)) || (rpm.has && !(rpm.v > 0)))
        return fail(ok, "values must be > 0");
    const int n = hp.has + tq.has + rpm.has;
    if (n < 2) return fail(ok, "enter any two of hp, torque, rpm");
    if (n > 2) return fail(ok, "leave one of hp, torque, rpm as -");

    double tLbft = tq.has ? (torqueImp ? tq.v : tq.v * kLbftPerNm) : 0;
    double hpV = hp.v, rpmV = rpm.v;
    if (hp.has && rpm.has) tLbft = hpV * 5252.0 / rpmV;
    else if (hp.has && tq.has) rpmV = hpV * 5252.0 / tLbft;
    else hpV = tLbft * rpmV / 5252.0;

    Out o;
    o.add("hp", sig(hpV) + (hp.has ? "" : " *"));
    o.add("torque", sig(tLbft / kLbftPerNm) + " N.m / " + sig(tLbft) + " lb.ft" + (tq.has ? "" : " *"));
    o.add("rpm", sig(rpmV) + (rpm.has ? "" : " *"));
    if (t.size() >= 4 && !is_blank(t[3])) {
        double eff = 0;
        if (!parse_double(t[3], eff) || !(eff > 0 && eff <= 100.0)) return fail(ok, "eff must be 0..100 %");
        const double shaftW = hpV * kHpW;
        o.add("shaft", sig(shaftW) + " W");
        o.add("electrical", sig(shaftW / (eff / 100.0)) + " W");
    }
    return o.str();
}

// Pressure -> kPa. Default unit kPa.
bool parse_pressure_kpa(const std::string& tok, double& kpa) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty() || u == "kpa") kpa = v;
    else if (u == "pa") kpa = v / 1000.0;
    else if (u == "mpa") kpa = v * 1000.0;
    else if (u == "bar") kpa = v * 100.0;
    else if (u == "psi") kpa = v * 6.894757293168;
    else if (u == "atm") kpa = v * 101.325;
    else return false;
    return true;
}

// Temperature -> K. Default unit C.
bool parse_temp_k(const std::string& tok, double& k) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty()) u = "c";
    return temp_to_k(v, u, k);
}

// Volume -> liters. Default unit L.
bool parse_volume_l(const std::string& tok, double& l) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty() || u == "l") l = v;
    else if (u == "ml") l = v / 1000.0;
    else if (u == "m3") l = v * 1000.0;
    else if (u == "gal") l = v * 3.785411784;
    else if (u == "ft3") l = v * 28.316846592;
    else return false;
    return true;
}

struct GasDef {
    const char* name;
    const char* alias;
    double molarMass; // g/mol
};
const GasDef kGases[] = {{"air", "air", 28.97},      {"nitrogen", "n2", 28.0134}, {"oxygen", "o2", 31.998},
                         {"co2", "co2", 44.01},      {"argon", "ar", 39.948},     {"methane", "ch4", 16.04}};

constexpr double kAtmKpa = 101.325;
constexpr double kRuniv = 8.314462618; // J/(mol K)
constexpr double kRair = 287.05;       // J/(kg K)

std::string gas_air(const Tokens& t, bool& ok) {
    if (t.empty()) return fail(ok, "need: air temp  (e.g. 20 or 68f)");
    double k = 0;
    if (!parse_temp_k(t[0], k)) return fail(ok, "bad temperature");
    if (!(k > 0)) return fail(ok, "temp must be > 0 K");
    Out o;
    o.add("density", sig(kAtmKpa * 1000.0 / (kRair * k)) + " kg/m3 @1 atm");
    o.add("speed of sound", sig(std::sqrt(1.4 * kRair * k)) + " m/s");
    return o.str();
}

// P1 V1 [T1] P2 V2 [T2]; one blank solves. 4 fields = Boyle (isothermal), 6 = combined gas law.
std::string gas_boyle(const Tokens& t, bool& ok) {
    if (t.size() != 4 && t.size() != 6)
        return fail(ok, "need: P1 V1 P2 V2   or   P1 V1 T1 P2 V2 T2  (one '-' unknown, absolute P)");
    const bool combined = t.size() == 6;
    // slots: P1 V1 T1 P2 V2 T2
    const int slotOf4[4] = {0, 1, 3, 4};
    double v[6] = {0, 0, 1, 0, 0, 1};
    int unknown = -1;
    for (size_t i = 0; i < t.size(); ++i) {
        const int s = combined ? static_cast<int>(i) : slotOf4[i];
        if (is_blank(t[i])) {
            if (unknown >= 0) return fail(ok, "only one unknown allowed");
            unknown = s;
            continue;
        }
        bool good = false;
        const int kind = s % 3; // 0 pressure, 1 volume, 2 temperature
        if (kind == 0) good = parse_pressure_kpa(t[i], v[s]);
        else if (kind == 1) good = parse_volume_l(t[i], v[s]);
        else good = parse_temp_k(t[i], v[s]);
        if (!good || !(v[s] > 0)) return fail(ok, "bad or non-positive value: " + t[i]);
    }
    if (unknown < 0) return fail(ok, "leave one field as -");
    const int other = unknown < 3 ? 3 : 0;
    const double K = v[other] * v[other + 1] / v[other + 2]; // PV/T of known state
    const int base = unknown < 3 ? 0 : 3;
    const int kind = unknown % 3;
    if (kind == 0) v[unknown] = K * v[base + 2] / v[base + 1];
    else if (kind == 1) v[unknown] = K * v[base + 2] / v[base];
    else v[unknown] = v[base] * v[base + 1] / K;

    static const char* const names[6] = {"P1", "V1", "T1", "P2", "V2", "T2"};
    Out o;
    for (int s = 0; s < 6; ++s) {
        if (!combined && (s == 2 || s == 5)) continue;
        std::string val;
        if (s % 3 == 0) val = sig(v[s]) + " kPa";
        else if (s % 3 == 1) val = sig(v[s]) + " L";
        else val = sig(v[s] - 273.15) + " C (" + sig(v[s]) + " K)";
        o.add(names[s], val + (s == unknown ? " *" : ""));
    }
    return o.str();
}

// P T V with one blank, n = 1 mol (Cardputer behaviour); optional gas name adds mass.
std::string gas_ideal(Tokens t, bool withMass, bool& ok) {
    bool gauge = true;
    double molar = 28.97;
    std::string gasName = "air";
    for (size_t i = 0; i < t.size();) {
        const std::string l = lower(t[i]);
        bool consumed = true;
        if (l == "gauge") gauge = true;
        else if (l == "abs" || l == "absolute") gauge = false;
        else {
            consumed = false;
            for (const GasDef& g : kGases) {
                if (l == g.name || l == g.alias) {
                    molar = g.molarMass;
                    gasName = g.name;
                    consumed = true;
                }
            }
        }
        if (consumed) t.erase(t.begin() + static_cast<long>(i));
        else ++i;
    }
    if (t.size() != 3) return fail(ok, "need: P T V (one '-' unknown), e.g. ideal gauge 200 20 5");

    double P = 0, T = 0, V = 0; // kPa abs, K, m3
    const bool hasP = !is_blank(t[0]), hasT = !is_blank(t[1]), hasV = !is_blank(t[2]);
    if (hasP + hasT + hasV != 2) return fail(ok, "leave exactly one of P/T/V as -");
    if (hasP) {
        if (!parse_pressure_kpa(t[0], P)) return fail(ok, "bad pressure (kPa Pa MPa bar psi atm)");
        if (gauge) P += kAtmKpa;
        if (!(P > 0)) return fail(ok, "absolute P must be > 0");
    }
    if (hasT) {
        if (!parse_temp_k(t[1], T)) return fail(ok, "bad temperature (C F K)");
        if (!(T > 0)) return fail(ok, "temp must be > 0 K");
    }
    if (hasV) {
        double l = 0;
        if (!parse_volume_l(t[2], l) || !(l > 0)) return fail(ok, "volume must be > 0 (L mL m3 gal ft3)");
        V = l / 1000.0;
    }
    if (!hasP) P = kRuniv * T / V / 1000.0;
    else if (!hasT) T = P * 1000.0 * V / kRuniv;
    else V = kRuniv * T / (P * 1000.0);

    Out o;
    o.add("P abs", sig(P) + " kPa" + (hasP ? "" : " *"));
    o.add("P gauge", sig(P - kAtmKpa) + " kPa (" + sig((P - kAtmKpa) / 6.894757293168) + " psi)");
    o.add("T", sig(T - 273.15) + " C / " + sig(T) + " K" + (hasT ? "" : " *"));
    o.add("V", sig(V * 1000.0) + " L" + (hasV ? "" : " *"));
    if (withMass) {
        const double massG = P * 1000.0 * V * molar / (kRuniv * T);
        o.add("mass (" + gasName + ")", sig(massG) + " g");
    }
    return o.str();
}

std::string run_gas(Tokens t, bool& ok) {
    enum { Boyle, Ideal, Mass, Air };
    int mode = pick_mode(t, {{"boyle", Boyle}, {"charles", Boyle}, {"combined", Boyle},
                             {"ideal", Ideal}, {"pvt", Ideal}, {"mass", Mass}, {"air", Air}});
    if (mode < 0) mode = (t.size() == 4 || t.size() == 6) ? Boyle : Ideal;
    switch (mode) {
    case Boyle: return gas_boyle(t, ok);
    case Ideal: return gas_ideal(t, false, ok);
    case Mass: return gas_ideal(t, true, ok);
    default: return gas_air(t, ok);
    }
}

// Velocity -> m/s. Default m/s.
bool parse_velocity_ms(const std::string& tok, double& ms) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty() || u == "m/s" || u == "ms") ms = v;
    else if (u == "fps" || u == "ft/s") ms = v * 0.3048;
    else if (u == "km/h" || u == "kph") ms = v / 3.6;
    else if (u == "mph") ms = v * 0.44704;
    else return false;
    return true;
}

// Volumetric flow -> m3/s. Default L/min.
bool parse_flow_m3s(const std::string& tok, double& q) {
    double v = 0;
    std::string u;
    if (!split_suffix(tok, v, u)) return false;
    if (u.empty() || u == "lpm" || u == "l/min") q = v / 60000.0;
    else if (u == "gpm" || u == "gal/min") q = v * 6.30901964e-5;
    else if (u == "l/s" || u == "ls") q = v / 1000.0;
    else if (u == "m3/s") q = v;
    else if (u == "m3/h") q = v / 3600.0;
    else return false;
    return true;
}

void flow_lines(Out& o, double q) {
    o.add("L/min", sig(q * 60000.0));
    o.add("gal/min", sig(q / 6.30901964e-5));
}

std::string run_flow(Tokens t, bool& ok) {
    enum { Q, DV, Vol, Darcy };
    int mode = pick_mode(t, {{"q", Q}, {"qdv", Q}, {"dv", DV}, {"fromq", DV}, {"vol", Vol}, {"volume", Vol},
                             {"darcy", Darcy}, {"dp", Darcy}, {"loss", Darcy}});
    if (mode < 0) mode = Q;

    if (mode == Q) { // ID vel
        if (t.size() < 2) return fail(ok, "need: q ID vel  (e.g. 25mm 1.5)");
        double d = 0, v = 0;
        if (!parse_length_m(t[0], "mm", d) || !(d > 0)) return fail(ok, "ID must be > 0 (mm default, or 1in)");
        if (!parse_velocity_ms(t[1], v) || !(v > 0)) return fail(ok, "velocity must be > 0 (m/s, or 5fps)");
        Out o;
        flow_lines(o, kPi * d * d / 4.0 * v);
        return o.str();
    }
    if (mode == DV) { // Q ID vel, one of ID/vel blank
        if (t.size() < 3) return fail(ok, "need: dv Q ID vel  (one of ID/vel as -)");
        double q = 0;
        if (!parse_flow_m3s(t[0], q) || !(q > 0)) return fail(ok, "Q must be > 0 (L/min default, or 10gpm)");
        const bool hasD = !is_blank(t[1]), hasV = !is_blank(t[2]);
        if (hasD == hasV) return fail(ok, "fill ID or vel, not both");
        Out o;
        if (hasD) {
            double d = 0;
            if (!parse_length_m(t[1], "mm", d) || !(d > 0)) return fail(ok, "ID must be > 0");
            o.add("vel", sig(q / (kPi * d * d / 4.0)) + " m/s");
        } else {
            double v = 0;
            if (!parse_velocity_ms(t[2], v) || !(v > 0)) return fail(ok, "velocity must be > 0");
            o.add("ID", sig(2.0 * std::sqrt(q / v / kPi) * 1000.0) + " mm");
        }
        flow_lines(o, q);
        return o.str();
    }
    if (mode == Vol) { // ID len
        if (t.size() < 2) return fail(ok, "need: vol ID length");
        double d = 0, l = 0;
        if (!parse_length_m(t[0], "mm", d) || !(d > 0)) return fail(ok, "ID must be > 0");
        if (!parse_length_m(t[1], "m", l) || !(l > 0)) return fail(ok, "length must be > 0 (m, or 100ft)");
        const double vol = kPi * d * d / 4.0 * l;
        Out o;
        o.add("m3", sig(vol));
        o.add("L", sig(vol * 1000.0));
        o.add("gal", sig(vol / 0.003785411784));
        return o.str();
    }
    // Darcy: ID len Q [PVC|copper|steel]
    if (t.size() < 3) return fail(ok, "need: darcy ID length Q [PVC|copper|steel]");
    double d = 0, l = 0, q = 0;
    if (!parse_length_m(t[0], "mm", d) || !(d > 0)) return fail(ok, "ID must be > 0");
    if (!parse_length_m(t[1], "m", l) || !(l > 0)) return fail(ok, "length must be > 0");
    if (!parse_flow_m3s(t[2], q) || !(q > 0)) return fail(ok, "Q must be > 0");
    double epsMm = 0.0015;
    if (t.size() >= 4) {
        const std::string r = lower(t[3]);
        if (r == "steel") epsMm = 0.045;
        else if (r != "pvc" && r != "copper") return fail(ok, "roughness: PVC, copper or steel");
    }
    const double eps = epsMm / 1000.0;
    const double v = q / (kPi * d * d / 4.0);
    const double nu = 1.004e-6; // water @20 C, m2/s
    double re = v * d / nu;
    if (re < 1) re = 1;
    const double invSqrtF = -1.8 * std::log10(std::pow(eps / d / 3.7, 1.11) + 6.9 / re); // Haaland
    const double f = 1.0 / (invSqrtF * invSqrtF);
    const double hf = f * (l / d) * v * v / (2.0 * 9.80665);
    Out o;
    o.add("head loss", sig(hf) + " m");
    o.add("Re", sig(re));
    o.add("f", sig(f));
    o.add("vel", sig(v) + " m/s");
    return o.str();
}

// ---------------------------------------------------------------------------
// Parallel resistors, RC, battery, frequency, PWM, percent, dates, tip, distance
// ---------------------------------------------------------------------------

std::string fmt_span(double sec) {
    const double a = std::fabs(sec);
    const char* sign = sec < 0 ? "-" : "";
    if (a >= 3600.0) return std::string(sign) + sig(a / 3600.0) + " h";
    if (a >= 60.0) return std::string(sign) + sig(a / 60.0) + " min";
    if (a >= 1.0) return std::string(sign) + sig(a) + " s";
    if (a >= 1e-3) return std::string(sign) + sig(a * 1e3) + " ms";
    if (a >= 1e-6) return std::string(sign) + sig(a * 1e6) + " us";
    if (a >= 1e-9) return std::string(sign) + sig(a * 1e9) + " ns";
    return std::string(sign) + sig(a * 1e12) + " ps";
}

std::string fmt_len(double m) {
    const double a = std::fabs(m);
    if (a >= 1000.0) return sig(m / 1000.0) + " km";
    if (a >= 1.0) return sig(m) + " m";
    if (a >= 0.01) return sig(m * 100.0) + " cm";
    return sig(m * 1000.0) + " mm";
}

// "100n", "4n7", "10uF", "1e-6" -> farads. Bare number is farads.
bool parse_farads(const std::string& in, double& out) {
    std::string t = trim(in);
    if (t.empty()) return false;
    if (t.back() == 'f' || t.back() == 'F') t.pop_back();
    for (size_t i = 1; i < t.size(); ++i) {
        const char c = t[i];
        double mult = 0;
        if (c == 'p' || c == 'P') mult = 1e-12;
        else if (c == 'n' || c == 'N') mult = 1e-9;
        else if (c == 'u' || c == 'U') mult = 1e-6;
        else if (c == 'm') mult = 1e-3;
        else continue;
        double whole = 0;
        if (!parse_double(t.substr(0, i), whole)) continue;
        const std::string right = t.substr(i + 1);
        double frac = 0;
        if (!right.empty()) {
            bool digits = true;
            for (char d : right) {
                if (!std::isdigit(static_cast<unsigned char>(d))) digits = false;
            }
            if (!digits) continue;
            frac = std::strtod(right.c_str(), nullptr) /
                   std::pow(10.0, static_cast<double>(right.size()));
        }
        out = (whole + frac) * mult;
        return std::isfinite(out);
    }
    return parse_double(t, out);
}

// "433M", "2.4GHz", "1000", "1k" -> hertz. A bare trailing m means megahertz.
bool parse_hertz(const std::string& in, double& hz) {
    double v = 0;
    std::string u;
    if (!split_suffix(in, v, u)) return false;
    if (u.size() >= 2 && u.compare(u.size() - 2, 2, "hz") == 0) u.resize(u.size() - 2);
    if (u.empty()) hz = v;
    else if (u == "k") hz = v * 1e3;
    else if (u == "m") hz = v * 1e6;
    else if (u == "g") hz = v * 1e9;
    else return false;
    return std::isfinite(hz);
}

// Time suffixes only. A bare number is not a duration.
bool parse_seconds(const std::string& in, double& sec) {
    double v = 0;
    std::string u;
    if (!split_suffix(in, v, u) || u.empty()) return false;
    if (u == "s" || u == "sec") sec = v;
    else if (u == "ms") sec = v * 1e-3;
    else if (u == "us") sec = v * 1e-6;
    else if (u == "ns") sec = v * 1e-9;
    else return false;
    return std::isfinite(sec);
}

std::string run_parallel(Tokens t, bool& ok) {
    skip_mode(t, {"par", "parallel", "p"});
    if (t.size() < 2 || t.size() > 6) return fail(ok, "need 2 to 6 resistors, e.g. 10k 10k");
    std::vector<double> r;
    r.reserve(t.size());
    double conductance = 0;
    for (const auto& tok : t) {
        double ohms = 0;
        if (!parse_ohms(tok, ohms) || !(ohms > 0)) return fail(ok, "resistor must be > 0 (e.g. 10k, 4k7)");
        r.push_back(ohms);
        conductance += 1.0 / ohms;
    }
    const double req = 1.0 / conductance;
    Out o;
    o.add("Req", fmt_ohms(req) + " ohm");
    for (size_t i = 0; i < r.size(); ++i) {
        const double share = (1.0 / r[i]) / conductance * 100.0;
        o.add("R" + std::to_string(i + 1), fmt_ohms(r[i]) + " ohm  " + sig(share) + "% I");
    }
    o.add("E24", fmt_ohms(nearest_e24(req)) + " ohm");
    return o.str();
}

std::string run_rc(Tokens t, bool& ok) {
    skip_mode(t, {"rc"});
    if (t.size() < 2 || t.size() > 3) return fail(ok, "need: R C [charge%]   e.g. 10k 100n 90");
    double r = 0, c = 0;
    if (!parse_ohms(t[0], r) || !(r > 0)) return fail(ok, "R must be > 0 (e.g. 10k)");
    if (!parse_farads(t[1], c) || !(c > 0)) return fail(ok, "C must be > 0 (e.g. 100n, 10u)");
    const double tau = r * c;
    Out o;
    o.add("tau", fmt_span(tau) + " (63%)");
    o.add("fc", sig(1.0 / (2.0 * kPi * tau)) + " Hz");
    o.add("99%", fmt_span(-tau * std::log(0.01)));
    o.add("5 tau", fmt_span(5.0 * tau));
    if (t.size() == 3) {
        double pct = 0;
        std::string p = t[2];
        if (!p.empty() && p.back() == '%') p.pop_back();
        if (!parse_double(p, pct) || !(pct > 0) || !(pct < 100))
            return fail(ok, "charge % must be between 0 and 100");
        o.add(sig(pct) + "%", fmt_span(-tau * std::log(1.0 - pct / 100.0)));
    }
    return o.str();
}

enum class BatQty { Bare, Mah, Ma, Hours };

bool parse_bat_tok(const std::string& tok, BatQty& kind, double& v) {
    double n = 0;
    std::string u;
    if (!split_suffix(tok, n, u)) return false;
    if (u == "mah") {
        kind = BatQty::Mah;
        v = n;
    } else if (u == "ah") {
        kind = BatQty::Mah;
        v = n * 1000.0;
    } else if (u == "ma") {
        kind = BatQty::Ma;
        v = n;
    } else if (u == "a" || u == "amp" || u == "amps") {
        kind = BatQty::Ma;
        v = n * 1000.0;
    } else if (u == "h" || u == "hr" || u == "hrs") {
        kind = BatQty::Hours;
        v = n;
    } else if (u == "min" || u == "mins") {
        kind = BatQty::Hours;
        v = n / 60.0;
    } else if (u.empty()) {
        kind = BatQty::Bare;
        v = n;
    } else {
        return false;
    }
    return std::isfinite(v);
}

std::string fmt_clock(double hours) {
    long long total_min = std::llround(hours * 60.0);
    if (total_min < 0) total_min = 0;
    return std::to_string(total_min / 60) + "h " + std::to_string(total_min % 60) + "m";
}

std::string run_battery(Tokens t, bool& ok) {
    enum { Runtime, Capacity, Load };
    int mode = pick_mode(t, {{"runtime", Runtime}, {"life", Runtime}, {"time", Runtime},
                             {"mah", Capacity}, {"cap", Capacity}, {"capacity", Capacity},
                             {"ma", Load}, {"load", Load}, {"current", Load}});
    if (mode < 0) mode = Runtime;
    if (t.size() != 2) return fail(ok, "need two of mAh, mA, hours   e.g. 2000 100");

    bool has_mah = false, has_ma = false, has_h = false;
    double mah = 0, ma = 0, hours = 0;
    double bare[2] = {};
    int n_bare = 0;
    for (const auto& tok : t) {
        BatQty kind = BatQty::Bare;
        double v = 0;
        if (!parse_bat_tok(tok, kind, v)) return fail(ok, "bad value (mAh, mA, h, min)");
        if (kind == BatQty::Bare) {
            bare[n_bare++] = v;
            continue;
        }
        if ((kind == BatQty::Mah && has_mah) || (kind == BatQty::Ma && has_ma) ||
            (kind == BatQty::Hours && has_h))
            return fail(ok, "repeated unit");
        if (kind == BatQty::Mah) {
            mah = v;
            has_mah = true;
        } else if (kind == BatQty::Ma) {
            ma = v;
            has_ma = true;
        } else {
            hours = v;
            has_h = true;
        }
    }

    const int known = (has_mah ? 1 : 0) + (has_ma ? 1 : 0) + (has_h ? 1 : 0);
    if (known + n_bare != 2) return fail(ok, "need two of mAh, mA, hours");

    auto assign_bare = [&](int which, double v) {
        if (which == 0) {
            mah = v;
            has_mah = true;
        } else if (which == 1) {
            ma = v;
            has_ma = true;
        } else {
            hours = v;
            has_h = true;
        }
    };

    if (n_bare == 2) {
        if (mode == Capacity) {
            assign_bare(2, bare[0]);
            assign_bare(1, bare[1]);
        } else if (mode == Load) {
            assign_bare(0, bare[0]);
            assign_bare(2, bare[1]);
        } else {
            assign_bare(0, bare[0]);
            assign_bare(1, bare[1]);
        }
    } else if (n_bare == 1) {
        const int answer = mode == Capacity ? 0 : mode == Load ? 1 : 2;
        const bool filled[3] = {has_mah, has_ma, has_h};
        if (filled[answer]) return fail(ok, "add a unit so the bare number is clear");
        int other = -1;
        for (int i = 0; i < 3; ++i) {
            if (i != answer && !filled[i]) other = i;
        }
        assign_bare(other, bare[0]);
    }

    if ((has_mah && !(mah > 0)) || (has_ma && !(ma > 0)) || (has_h && !(hours > 0)))
        return fail(ok, "mAh, mA and hours must be > 0");
    if (!has_mah) mah = ma * hours;
    else if (!has_ma) ma = mah / hours;
    else hours = mah / ma;

    Out o;
    o.add("mAh", sig(mah));
    o.add("mA", sig(ma));
    o.add("hours", sig(hours));
    o.add("clock", fmt_clock(hours));
    if (hours >= 24.0) o.add("days", sig(hours / 24.0));
    return o.str();
}

constexpr double kC = 299792458.0;

std::string emit_wave(double hz, double vf) {
    const double wl = kC / hz;
    Out o;
    o.add("1/4 wave", fmt_len(wl / 4.0 * vf));
    o.add("wavelength", fmt_len(wl));
    o.add("period", fmt_span(1.0 / hz));
    o.add("f", eng(hz) + "Hz");
    o.add("1/2 wave", fmt_len(wl / 2.0 * vf));
    if (vf != 1.0) o.add("vf", sig(vf));
    return o.str();
}

std::string run_frequency(Tokens t, bool& ok) {
    enum { Hz, Period, Wave };
    int mode = pick_mode(t, {{"hz", Hz}, {"f", Hz}, {"freq", Hz},
                             {"t", Period}, {"period", Period}, {"time", Period},
                             {"wave", Wave}, {"wl", Wave}, {"lambda", Wave}, {"len", Wave}});
    if (t.empty() || t.size() > 2) return fail(ok, "need: 433M | t 1us | wave 70cm [vf]");
    double vf = 1.0;
    if (t.size() == 2) {
        if (!parse_double(t[1], vf) || !(vf > 0) || vf > 1.0)
            return fail(ok, "velocity factor must be > 0 and <= 1");
    }
    if (mode < 0) {
        double sec = 0;
        mode = parse_seconds(t[0], sec) ? Period : Hz;
    }
    double hz = 0;
    if (mode == Period) {
        double sec = 0;
        if (!parse_seconds(t[0], sec) || !(sec > 0)) return fail(ok, "period must be > 0 (e.g. 1us)");
        hz = 1.0 / sec;
    } else if (mode == Wave) {
        double m = 0;
        if (!parse_length_m(t[0], "m", m) || !(m > 0)) return fail(ok, "wavelength must be > 0 (e.g. 70cm)");
        hz = kC / m;
    } else {
        if (!parse_hertz(t[0], hz) || !(hz > 0)) return fail(ok, "frequency must be > 0 (e.g. 433M, 2.4G)");
    }
    return emit_wave(hz, vf);
}

std::string run_pwm(Tokens t, bool& ok) {
    skip_mode(t, {"pwm"});
    if (t.size() != 2) return fail(ok, "need: freq duty%   e.g. 1k 25  or  1k 250us");
    double hz = 0;
    if (!parse_hertz(t[0], hz) || !(hz > 0)) return fail(ok, "frequency must be > 0 (e.g. 1k)");
    const double period = 1.0 / hz;
    double high = 0;
    double duty = 0;
    if (parse_seconds(t[1], high)) {
        if (!(high >= 0) || high > period + period * 1e-9) return fail(ok, "high time must fit in one period");
        if (high > period) high = period;
        duty = high / period * 100.0;
    } else {
        std::string d = t[1];
        if (!d.empty() && d.back() == '%') d.pop_back();
        if (!parse_double(d, duty) || duty < 0 || duty > 100) return fail(ok, "duty must be 0..100 %");
        high = period * duty / 100.0;
    }
    Out o;
    o.add("high", fmt_span(high));
    o.add("low", fmt_span(period - high));
    o.add("duty", sig(duty) + "%");
    o.add("period", fmt_span(period));
    o.add("freq", eng(hz) + "Hz");
    return o.str();
}

std::string run_percentage(Tokens t, bool& ok) {
    enum { Of, Change, Ratio };
    int mode = pick_mode(t, {{"of", Of}, {"pct", Of}, {"percent", Of},
                             {"change", Change}, {"delta", Change}, {"chg", Change},
                             {"ratio", Ratio}, {"part", Ratio}, {"what", Ratio}});
    if (!t.empty() && t[0].back() == '%') {
        t[0].pop_back();
        if (mode < 0) mode = Of;
    }
    if (mode < 0 || t.size() != 2) return fail(ok, "need: 15% 200 | change 80 100 | ratio 1 4");
    double a = 0, b = 0;
    if (!parse_double(t[0], a) || !parse_double(t[1], b)) return fail(ok, "need two numbers");
    Out o;
    if (mode == Of) {
        o.add("result", sig(b * a / 100.0));
        o.add("of", sig(a) + "% of " + sig(b));
    } else if (mode == Change) {
        if (a == 0) return fail(ok, "old value must not be 0");
        const double pct = (b - a) / a * 100.0;
        o.add("change", (pct > 0 ? "+" : "") + sig(pct) + "%");
        o.add("delta", sig(b - a));
        o.add("from", sig(a) + " to " + sig(b));
    } else {
        if (b == 0) return fail(ok, "whole must not be 0");
        o.add("percent", sig(a / b * 100.0) + "%");
        o.add("ratio", sig(a) + " / " + sig(b));
    }
    return o.str();
}

bool parse_ymd(const std::string& s, int& y, int& m, int& d) {
    int yy = 0, mm = 0, dd = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d%c", &yy, &mm, &dd, &extra) != 3) return false;
    if (yy < 1 || yy > 9999 || mm < 1 || mm > 12 || dd < 1 || dd > days_in_month(yy, mm)) return false;
    y = yy;
    m = mm;
    d = dd;
    return true;
}

const char* weekday_of(int64_t days) {
    static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int dow = static_cast<int>(((days % 7) + 11) % 7);
    return wd[dow];
}

std::string run_date_diff(Tokens t, bool& ok) {
    enum { Diff, Add };
    int mode = pick_mode(t, {{"diff", Diff}, {"between", Diff}, {"days", Diff},
                             {"add", Add}, {"plus", Add}, {"sub", Add}});
    if (t.size() != 2) return fail(ok, "need: YYYY-MM-DD YYYY-MM-DD | add YYYY-MM-DD days");
    int y1 = 0, m1 = 0, d1 = 0;
    if (!parse_ymd(t[0], y1, m1, d1)) return fail(ok, "bad date (YYYY-MM-DD)");
    const int64_t day1 = days_from_civil(y1, m1, d1);
    int y2 = 0, m2 = 0, d2 = 0;
    const bool second_is_date = parse_ymd(t[1], y2, m2, d2);
    if (mode < 0) mode = second_is_date ? Diff : Add;
    Out o;
    if (mode == Diff) {
        if (!second_is_date) return fail(ok, "need two dates");
        const int64_t day2 = days_from_civil(y2, m2, d2);
        const int64_t delta = day2 - day1;
        const int64_t abs_d = delta < 0 ? -delta : delta;
        o.add("days", std::to_string(static_cast<long long>(delta)));
        o.add("weeks", std::to_string(static_cast<long long>(abs_d / 7)) + "w " +
                           std::to_string(static_cast<long long>(abs_d % 7)) + "d");
        o.add("when", std::string(weekday_of(day1)) + " to " + weekday_of(day2));
        return o.str();
    }
    long long n = 0;
    if (!parse_int_range(t[1], n, -3650000, 3650000)) return fail(ok, "days must be a whole number");
    const int64_t day2 = day1 + n;
    int y = 0, m = 0, d = 0;
    civil_from_days(day2, y, m, d);
    if (y < 1 || y > 9999) return fail(ok, "result is outside year 1..9999");
    o.add("date", fmt("%04d-%02d-%02d", y, m, d));
    o.add("weekday", weekday_of(day2));
    o.add("days", std::to_string(n));
    return o.str();
}

// HH:MM or HH:MM:SS. Seconds are optional. Hours stay inside one day.
bool parse_clock(const std::string& s, int& sec_out, bool& has_seconds) {
    int h = 0, m = 0, sec = 0;
    char extra = 0;
    has_seconds = false;
    const int n3 = std::sscanf(s.c_str(), "%d:%d:%d%c", &h, &m, &sec, &extra);
    if (n3 == 3) {
        has_seconds = true;
    } else {
        const int n2 = std::sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra);
        if (n2 != 2) return false;
        sec = 0;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59) return false;
    sec_out = h * 3600 + m * 60 + sec;
    return true;
}

std::string fmt_clock(int sec, bool with_seconds) {
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    if (with_seconds || s != 0) return fmt("%02d:%02d:%02d", h, m, s);
    return fmt("%02d:%02d", h, m);
}

// 2h30, 90m, 45s, 1:30, -15m. A bare number is minutes. H:MM is a duration, so hours may pass 23.
bool parse_duration_sec(const std::string& in, long long& out) {
    std::string s = lower(trim(in));
    if (s.empty()) return false;
    int sign = 1;
    if (s[0] == '+') s.erase(s.begin());
    else if (s[0] == '-') {
        sign = -1;
        s.erase(s.begin());
    }
    if (s.empty()) return false;

    if (s.find(':') != std::string::npos) {
        int h = 0, m = 0, sec = 0;
        char extra = 0;
        const int n3 = std::sscanf(s.c_str(), "%d:%d:%d%c", &h, &m, &sec, &extra);
        if (n3 != 3) {
            const int n2 = std::sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra);
            if (n2 != 2) return false;
            sec = 0;
        }
        if (h < 0 || m < 0 || m > 59 || sec < 0 || sec > 59) return false;
        out = sign * (static_cast<long long>(h) * 3600 + m * 60 + sec);
        return true;
    }

    long long total = 0;
    bool any = false;
    char prev = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        long long n = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            n = n * 10 + (s[i] - '0');
            ++i;
        }
        char unit = 0;
        if (i < s.size() && (s[i] == 'h' || s[i] == 'm' || s[i] == 's')) {
            unit = s[i++];
        } else if (prev == 'h') {
            unit = 'm';
        } else if (!any && i == s.size()) {
            unit = 'm';
        } else {
            return false;
        }
        if (unit == 'h') total += n * 3600;
        else if (unit == 'm') total += n * 60;
        else total += n;
        prev = unit;
        any = true;
    }
    out = sign * total;
    return any;
}

std::string fmt_hms_span(long long sec) {
    const char* sign = sec < 0 ? "-" : "";
    long long a = sec < 0 ? -sec : sec;
    const long long d = a / 86400;
    a %= 86400;
    const long long h = a / 3600;
    a %= 3600;
    const long long m = a / 60;
    const long long s = a % 60;
    std::string out = sign;
    bool wrote = false;
    if (d != 0) {
        out += std::to_string(d) + "d";
        wrote = true;
    }
    if (h != 0) {
        if (wrote) out += " ";
        out += std::to_string(h) + "h";
        wrote = true;
    }
    if (m != 0) {
        if (wrote) out += " ";
        out += std::to_string(m) + "m";
        wrote = true;
    }
    if (s != 0 || !wrote) {
        if (wrote) out += " ";
        out += std::to_string(s) + "s";
    }
    return out;
}

std::string run_time_diff(Tokens t, bool& ok) {
    enum { Auto, Diff, Add };
    int mode = Auto;
    bool negate = false;
    if (!t.empty()) {
        const std::string word = lower(t[0]);
        if (word == "diff" || word == "between") {
            mode = Diff;
            t.erase(t.begin());
        } else if (word == "add" || word == "plus") {
            mode = Add;
            t.erase(t.begin());
        } else if (word == "sub") {
            mode = Add;
            negate = true;
            t.erase(t.begin());
        }
    }
    if (t.size() >= 3 && (t[1] == "+" || t[1] == "-")) {
        mode = Add;
        if (t[1] == "-") negate = !negate;
        t.erase(t.begin() + 1);
    }

    if (t.size() == 4) {
        if (mode == Add) return fail(ok, "add a duration: add YYYY-MM-DD HH:MM 2h30");
        int s1 = 0, s2 = 0;
        bool sec1 = false, sec2 = false;
        int y1 = 0, mo1 = 0, d1 = 0, y2 = 0, mo2 = 0, d2 = 0;
        if (!parse_ymd(t[0], y1, mo1, d1) || !parse_clock(t[1], s1, sec1) || !parse_ymd(t[2], y2, mo2, d2) ||
            !parse_clock(t[3], s2, sec2))
            return fail(ok, "need: YYYY-MM-DD HH:MM YYYY-MM-DD HH:MM");
        const long long a = days_from_civil(y1, mo1, d1) * 86400LL + s1;
        const long long b = days_from_civil(y2, mo2, d2) * 86400LL + s2;
        const long long delta = b - a;
        Out o;
        o.add("span", delta == 0 ? "0m" : fmt_hms_span(delta));
        o.add("hours", sig(static_cast<double>(delta) / 3600.0));
        o.add("seconds", std::to_string(static_cast<long long>(delta)));
        return o.str();
    }

    if (t.size() == 3) {
        int y = 0, mo = 0, d = 0, clock = 0;
        bool has_sec = false;
        long long dur = 0;
        if (!parse_ymd(t[0], y, mo, d) || !parse_clock(t[1], clock, has_sec) || !parse_duration_sec(t[2], dur))
            return fail(ok, "need: add YYYY-MM-DD HH:MM 2h30");
        if (negate) dur = -dur;
        const long long instant = days_from_civil(y, mo, d) * 86400LL + clock + dur;
        const long long day = floor_div(instant, 86400);
        const int sec = static_cast<int>(instant - day * 86400);
        int yy = 0, mm = 0, dd = 0;
        civil_from_days(day, yy, mm, dd);
        if (yy < 1 || yy > 9999) return fail(ok, "result is outside year 1..9999");
        Out o;
        o.add("date", fmt("%04d-%02d-%02d", yy, mm, dd));
        o.add("time", fmt_clock(sec, has_sec || (dur % 60) != 0));
        o.add("weekday", weekday_of(day));
        return o.str();
    }

    if (t.size() != 2) return fail(ok, "need: HH:MM HH:MM | add HH:MM 2h30");

    int start = 0, end = 0;
    bool start_sec = false, end_sec = false;
    long long dur = 0;
    if (!parse_clock(t[0], start, start_sec)) return fail(ok, "bad time (HH:MM or HH:MM:SS)");
    const bool end_is_clock = parse_clock(t[1], end, end_sec);
    const bool end_is_dur = parse_duration_sec(t[1], dur);
    // A second clock time is a difference. Add only when the user asked, or the
    // second token is a duration such as 2h30 or 90m and not a clock time.
    if (mode == Auto) mode = end_is_clock ? Diff : Add;
    if (mode == Diff && !end_is_clock) return fail(ok, "need two times (HH:MM)");
    if (mode == Add && !end_is_dur) return fail(ok, "need a duration (2h30, 90m, 45s)");

    Out o;
    if (mode == Diff) {
        int delta = end - start;
        const bool next = delta < 0;
        if (next) delta += 86400;
        o.add("span", delta == 0 ? "0m" : fmt_hms_span(delta));
        if (next) o.add("when", "next day");
        if (start_sec || end_sec) o.add("sec", std::to_string(delta));
        else o.add("min", std::to_string(delta / 60));
        return o.str();
    }

    if (negate) dur = -dur;
    const long long total = static_cast<long long>(start) + dur;
    const long long day = floor_div(total, 86400);
    const int sec = static_cast<int>(total - day * 86400);
    o.add("time", fmt_clock(sec, start_sec || (dur % 60) != 0));
    if (day != 0) o.add("day", (day > 0 ? "+" : "") + std::to_string(static_cast<long long>(day)));
    o.add("moved", fmt_hms_span(dur));
    return o.str();
}

std::string run_tip(Tokens t, bool& ok) {
    skip_mode(t, {"tip", "split"});
    if (t.size() < 2 || t.size() > 3) return fail(ok, "need: bill tip% [people]   e.g. 50 15 3");
    double bill = 0, pct = 0, people = 1;
    if (!parse_double(t[0], bill) || bill < 0) return fail(ok, "bill must be >= 0");
    std::string tip = t[1];
    if (!tip.empty() && tip.back() == '%') tip.pop_back();
    if (!parse_double(tip, pct) || pct < 0) return fail(ok, "tip % must be >= 0");
    if (t.size() == 3) {
        if (!parse_double(t[2], people) || people < 1 || people != std::floor(people))
            return fail(ok, "people must be a whole number >= 1");
    }
    const double tip_amt = bill * pct / 100.0;
    const double total = bill + tip_amt;
    Out o;
    o.add("each", fmt("%.2f", total / people));
    o.add("total", fmt("%.2f", total));
    o.add("tip", fmt("%.2f", tip_amt) + " (" + sig(pct) + "%)");
    o.add("tip each", fmt("%.2f", tip_amt / people));
    o.add("people", std::to_string(static_cast<long long>(people)));
    return o.str();
}

Tokens split_commas(const Tokens& in) {
    Tokens out;
    for (const auto& s : in) {
        std::string cur;
        for (char c : s) {
            if (c == ',') {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

std::string run_distance(Tokens in, bool& ok) {
    skip_mode(in, {"dist", "distance", "gps"});
    Tokens t = split_commas(in);
    if (t.size() != 4) return fail(ok, "need: lat1 lon1 lat2 lon2");
    double lat1 = 0, lon1 = 0, lat2 = 0, lon2 = 0;
    if (!parse_double(t[0], lat1) || !parse_double(t[1], lon1) || !parse_double(t[2], lat2) ||
        !parse_double(t[3], lon2))
        return fail(ok, "lat/lon must be numbers");
    if (lat1 < -90 || lat1 > 90 || lat2 < -90 || lat2 > 90) return fail(ok, "latitude must be -90..90");
    if (lon1 < -180 || lon1 > 180 || lon2 < -180 || lon2 > 180) return fail(ok, "longitude must be -180..180");

    constexpr double kEarth = 6371000.0;
    const double p1 = lat1 * kPi / 180.0;
    const double p2 = lat2 * kPi / 180.0;
    const double dp = (lat2 - lat1) * kPi / 180.0;
    const double dl = (lon2 - lon1) * kPi / 180.0;
    const double sdp = std::sin(dp / 2.0);
    const double sdl = std::sin(dl / 2.0);
    const double a = sdp * sdp + std::cos(p1) * std::cos(p2) * sdl * sdl;
    const double under = 1.0 - a;
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(under > 0 ? under : 0));
    const double meters = kEarth * c;

    Out o;
    o.add("km", sig(meters / 1000.0));
    o.add("meters", sig(meters));
    if (meters > 0.5) {
        const double y = std::sin(dl) * std::cos(p2);
        const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
        double deg = std::atan2(y, x) * 180.0 / kPi;
        if (deg < 0) deg += 360.0;
        o.add("bearing", sig(deg) + " deg");
    }
    o.add("miles", sig(meters / 1609.344));
    return o.str();
}

// ---------------------------------------------------------------------------
// Reactance, 555, entropy, hashes, JWT, XOR, CIDR, OUI, radio, transfer, kWh, coords, hex
// ---------------------------------------------------------------------------

bool parse_henries(const std::string& in, double& out) {
    std::string t = trim(in);
    if (t.empty()) return false;
    if (t.back() == 'h' || t.back() == 'H') t.pop_back();
    for (size_t i = 1; i < t.size(); ++i) {
        const char c = t[i];
        double mult = 0;
        if (c == 'u' || c == 'U') mult = 1e-6;
        else if (c == 'n' || c == 'N') mult = 1e-9;
        else if (c == 'm') mult = 1e-3;
        else if (c == 'H' || c == 'h') mult = 1.0;
        else continue;
        double whole = 0;
        if (!parse_double(t.substr(0, i), whole)) continue;
        const std::string right = t.substr(i + 1);
        double frac = 0;
        if (!right.empty()) {
            for (char d : right) {
                if (!std::isdigit(static_cast<unsigned char>(d))) {
                    whole = -1;
                    break;
                }
            }
            if (whole < 0) continue;
            frac = std::strtod(right.c_str(), nullptr) / std::pow(10.0, static_cast<double>(right.size()));
        }
        out = (whole + frac) * mult;
        return std::isfinite(out);
    }
    return parse_double(t, out);
}

bool parse_radio_hz(const std::string& in, double& hz) {
    if (parse_hertz(in, hz) && hz > 0) {
        std::string u;
        double v = 0;
        if (split_suffix(in, v, u) && u.empty() && hz < 1e6) hz *= 1e6;
        return hz > 0;
    }
    return false;
}

std::string run_reactance(Tokens t, bool& ok) {
    enum { Cap, Ind, Rc, Rl, Lc };
    int mode = pick_mode(t, {{"c", Cap}, {"xc", Cap}, {"cap", Cap},
                             {"l", Ind}, {"xl", Ind}, {"ind", Ind},
                             {"rc", Rc}, {"rl", Rl}, {"lc", Lc}, {"res", Lc}});
    if (mode < 0) return fail(ok, "need: c F C | l F L | rc F R C | lc L C");
    if (mode == Lc) {
        if (t.size() != 2) return fail(ok, "need: lc L C   e.g. 10u 100n");
        double l = 0, c = 0;
        if (!parse_henries(t[0], l) || !(l > 0)) return fail(ok, "L must be > 0 (e.g. 10u)");
        if (!parse_farads(t[1], c) || !(c > 0)) return fail(ok, "C must be > 0 (e.g. 100n)");
        const double f = 1.0 / (2.0 * kPi * std::sqrt(l * c));
        Out o;
        o.add("f0", eng(f) + "Hz");
        o.add("period", fmt_span(1.0 / f));
        o.add("Xl=Xc", fmt_ohms(2.0 * kPi * f * l) + " ohm");
        return o.str();
    }
    if (t.size() < 2) return fail(ok, "need frequency and a part value");
    double f = 0;
    if (!parse_hertz(t[0], f) || !(f > 0)) return fail(ok, "bad frequency (e.g. 1k, 10M)");
    Out o;
    o.add("f", eng(f) + "Hz");
    if (mode == Cap) {
        double c = 0;
        if (!parse_farads(t[1], c) || !(c > 0)) return fail(ok, "C must be > 0");
        const double xc = 1.0 / (2.0 * kPi * f * c);
        o.add("Xc", fmt_ohms(xc) + " ohm");
        o.add("phase", "-90 deg");
    } else if (mode == Ind) {
        double l = 0;
        if (!parse_henries(t[1], l) || !(l > 0)) return fail(ok, "L must be > 0");
        const double xl = 2.0 * kPi * f * l;
        o.add("Xl", fmt_ohms(xl) + " ohm");
        o.add("phase", "+90 deg");
    } else if (mode == Rc) {
        if (t.size() < 3) return fail(ok, "need: rc F R C");
        double r = 0, c = 0;
        if (!parse_ohms(t[1], r) || !(r > 0)) return fail(ok, "R must be > 0");
        if (!parse_farads(t[2], c) || !(c > 0)) return fail(ok, "C must be > 0");
        const double xc = 1.0 / (2.0 * kPi * f * c);
        const double z = std::sqrt(r * r + xc * xc);
        o.add("Z", fmt_ohms(z) + " ohm");
        o.add("Xc", fmt_ohms(xc) + " ohm");
        o.add("phase", sig(-std::atan2(xc, r) * 180.0 / kPi) + " deg");
    } else {
        if (t.size() < 3) return fail(ok, "need: rl F R L");
        double r = 0, l = 0;
        if (!parse_ohms(t[1], r) || !(r > 0)) return fail(ok, "R must be > 0");
        if (!parse_henries(t[2], l) || !(l > 0)) return fail(ok, "L must be > 0");
        const double xl = 2.0 * kPi * f * l;
        const double z = std::sqrt(r * r + xl * xl);
        o.add("Z", fmt_ohms(z) + " ohm");
        o.add("Xl", fmt_ohms(xl) + " ohm");
        o.add("phase", sig(std::atan2(xl, r) * 180.0 / kPi) + " deg");
    }
    return o.str();
}

std::string run_555(Tokens t, bool& ok) {
    enum { Astable, Mono };
    int mode = pick_mode(t, {{"astable", Astable}, {"a", Astable}, {"mono", Mono}, {"m", Mono}, {"one", Mono}});
    if (mode < 0) mode = t.size() >= 3 ? Astable : Mono;
    constexpr double kLn2 = 0.693147;
    if (mode == Mono) {
        if (t.size() < 2) return fail(ok, "need: mono R C   e.g. 10k 100n");
        double r = 0, c = 0;
        if (!parse_ohms(t[0], r) || !(r > 0)) return fail(ok, "R must be > 0");
        if (!parse_farads(t[1], c) || !(c > 0)) return fail(ok, "C must be > 0");
        const double pulse = 1.098612 * r * c;
        Out o;
        o.add("pulse", fmt_span(pulse));
        o.add("R", fmt_ohms(r) + " ohm");
        o.add("C", eng(c) + "F");
        return o.str();
    }
    if (t.size() < 3) return fail(ok, "need: astable Ra Rb C   e.g. 10k 10k 100n");
    double ra = 0, rb = 0, c = 0;
    if (!parse_ohms(t[0], ra) || !(ra > 0) || !parse_ohms(t[1], rb) || !(rb > 0))
        return fail(ok, "Ra and Rb must be > 0");
    if (!parse_farads(t[2], c) || !(c > 0)) return fail(ok, "C must be > 0");
    const double high = kLn2 * (ra + rb) * c;
    const double low = kLn2 * rb * c;
    const double freq = 1.0 / (high + low);
    Out o;
    o.add("freq", eng(freq) + "Hz");
    o.add("high", fmt_span(high));
    o.add("low", fmt_span(low));
    o.add("duty", sig(high / (high + low) * 100.0) + "%");
    return o.str();
}

std::string fmt_crack(double seconds) {
    if (!std::isfinite(seconds) || seconds > 1e30) return "impractical";
    if (seconds < 0.001) return "< 1 ms";
    if (seconds < 1) return sig(seconds * 1000.0) + " ms";
    if (seconds < 60) return sig(seconds) + " s";
    if (seconds < 3600) return sig(seconds / 60.0) + " min";
    if (seconds < 86400) return sig(seconds / 3600.0) + " h";
    if (seconds < 86400.0 * 365.25) return sig(seconds / 86400.0) + " d";
    const double years = seconds / (86400.0 * 365.25);
    if (years < 1e6) return sig(years) + " y";
    if (years < 1e9) return sig(years / 1e6) + " My";
    return sig(years / 1e9) + " By";
}

std::string crack_at(double bits, double rate) {
    if (!(rate > 0)) return "bad rate";
    if (bits >= 1020) return "impractical";
    const double seconds = std::pow(2.0, bits - 1.0) / rate;
    return fmt_crack(seconds);
}

std::string run_entropy(const std::string& in, bool& ok) {
    std::string s = trim(in);
    Tokens t = split_ws(s);
    double rate = 1e10;
    if (!t.empty() && lower(t[0]) == "rate" && t.size() >= 3) {
        if (!parse_double(t[1], rate) || !(rate > 0)) return fail(ok, "rate must be guesses/s > 0");
        s = trim(s.substr(s.find(t[1]) + t[1].size()));
        t = split_ws(s);
    }
    if (!t.empty() && (lower(t[0]) == "len" || lower(t[0]) == "length") && t.size() >= 3) {
        double len = 0, pool = 0;
        if (!parse_double(t[1], len) || !(len > 0) || !parse_double(t[2], pool) || !(pool > 1))
            return fail(ok, "need: len length charset   e.g. len 12 95");
        const double bits = len * std::log2(pool);
        Out o;
        o.add("bits", sig(bits));
        o.add("charset", sig(pool));
        o.add("crack", crack_at(bits, rate) + " @ " + eng(rate) + "/s");
        return o.str();
    }
    if (!t.empty() && lower(t[0]) == "words" && t.size() >= 2) {
        double n = 0;
        if (!parse_double(t[1], n) || !(n > 0)) return fail(ok, "need: words N");
        const double bits = n * 12.925;
        Out o;
        o.add("bits", sig(bits));
        o.add("words", sig(n) + " x 12.9");
        o.add("crack", crack_at(bits, rate) + " @ " + eng(rate) + "/s");
        return o.str();
    }
    if (s.empty()) return fail(ok, "type a password, or len 12 95, or words 6");
    bool lo = false, up = false, di = false, sp = false, sy = false;
    for (unsigned char uc : s) {
        if (std::islower(uc)) lo = true;
        else if (std::isupper(uc)) up = true;
        else if (std::isdigit(uc)) di = true;
        else if (uc == ' ') sp = true;
        else sy = true;
    }
    int pool = 0;
    std::string kind;
    auto add = [&](const char* name, int n) {
        pool += n;
        if (!kind.empty()) kind += "+";
        kind += name;
    };
    if (lo) add("lower", 26);
    if (up) add("upper", 26);
    if (di) add("digit", 10);
    if (sy) add("symbol", 33);
    else if (sp) add("space", 1);
    if (pool < 2) return fail(ok, "charset too small");
    const double bits = static_cast<double>(s.size()) * std::log2(static_cast<double>(pool));
    Out o;
    o.add("bits", sig(bits));
    o.add("charset", kind + " " + std::to_string(pool));
    o.add("crack", crack_at(bits, rate) + " @ " + eng(rate) + "/s");
    return o.str();
}

bool is_hex_string(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) return false;
    for (unsigned char c : s) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

std::string run_hash_id(const std::string& in, bool& ok) {
    const std::string s = trim(in);
    if (s.empty()) return fail(ok, "paste a hash");
    const std::string low = lower(s);
    auto named = [&](const char* what, const char* mode) {
        Out o;
        o.add("hash", what);
        o.add("hashcat", mode);
        return o.str();
    };
    if (low.rfind("$2a$", 0) == 0 || low.rfind("$2b$", 0) == 0 || low.rfind("$2y$", 0) == 0 ||
        low.rfind("$2x$", 0) == 0)
        return named("bcrypt", "-m 3200");
    if (low.rfind("$argon2id$", 0) == 0) return named("Argon2id", "-m 34000");
    if (low.rfind("$argon2i$", 0) == 0) return named("Argon2i", "-m 34000");
    if (low.rfind("$argon2d$", 0) == 0) return named("Argon2d", "-m 34000");
    if (low.rfind("$apr1$", 0) == 0) return named("Apache MD5", "-m 1600");
    if (low.rfind("$1$", 0) == 0) return named("md5crypt", "-m 500");
    if (low.rfind("$5$", 0) == 0) return named("sha256crypt", "-m 7400");
    if (low.rfind("$6$", 0) == 0) return named("sha512crypt", "-m 1800");
    if (low.rfind("$p$", 0) == 0 || low.rfind("$h$", 0) == 0) return named("phpass", "-m 400");
    if (low.rfind("$8$", 0) == 0) return named("Cisco type 8", "-m 9200");
    if (low.rfind("$9$", 0) == 0) return named("Cisco type 9", "-m 9300");
    if (low.rfind("$dcc2$", 0) == 0) return named("Domain Cached Credentials 2", "-m 2100");
    if (low.rfind("$krb5tgs$", 0) == 0) return named("Kerberos TGS-REP", "-m 13100");
    if (low.rfind("$krb5asrep$", 0) == 0) return named("Kerberos AS-REP", "-m 18200");
    if (low.rfind("$krb5pa$", 0) == 0) return named("Kerberos AS-REQ", "-m 7500");
    if (low.rfind("wpa*", 0) == 0) return named("WPA-PBKDF2 PMKID/EAPOL", "-m 22000");
    if (low.rfind("{sha}", 0) == 0) return named("LDAP {SHA}", "-m 101");
    if (low.rfind("{ssha}", 0) == 0) return named("LDAP {SSHA}", "-m 111");
    if (!s.empty() && s[0] == '*' && is_hex_string(s.substr(1)) && s.size() == 41)
        return named("MySQL 4/5", "-m 300");
    const int colons = static_cast<int>(std::count(s.begin(), s.end(), ':'));
    if (colons >= 4 && s.find("::") != std::string::npos) return named("NetNTLMv2", "-m 5600");
    std::string hex = low;
    if (hex.rfind("0x", 0) == 0) hex = hex.substr(2);
    if (is_hex_string(hex)) {
        Out o;
        const auto n = hex.size();
        if (n == 16) {
            o.add("hash", "MySQL323");
            o.add("hashcat", "-m 200");
        } else if (n == 32) {
            o.add("hash", "MD5 or NTLM");
            o.add("hashcat", "-m 0  or  -m 1000");
            o.add("note", "32 hex is ambiguous");
        } else if (n == 40) {
            o.add("hash", "SHA1");
            o.add("hashcat", "-m 100");
        } else if (n == 56) {
            o.add("hash", "SHA224");
            o.add("hashcat", "-m 1300");
        } else if (n == 64) {
            o.add("hash", "SHA256");
            o.add("hashcat", "-m 1400");
        } else if (n == 96) {
            o.add("hash", "SHA384");
            o.add("hashcat", "-m 10800");
        } else if (n == 128) {
            o.add("hash", "SHA512");
            o.add("hashcat", "-m 1700");
        } else {
            return fail(ok, "hex length is not a known hash");
        }
        return o.str();
    }
    if (std::count(s.begin(), s.end(), '.') == 2) return fail(ok, "looks like a JWT - use JWT decode");
    return fail(ok, "unrecognized hash");
}

int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool b64_decode(std::string in, std::string& out) {
    std::string s;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        s.push_back(c);
    }
    if (s.empty()) return false;
    while (s.size() % 4) s.push_back('=');
    out.clear();
    for (size_t i = 0; i < s.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            if (s[i + static_cast<size_t>(k)] == '=') {
                v[k] = 0;
                ++pad;
            } else {
                v[k] = b64_val(s[i + static_cast<size_t>(k)]);
                if (v[k] < 0) return false;
            }
        }
        if (pad > 2) return false;
        out.push_back(static_cast<char>((v[0] << 2) | (v[1] >> 4)));
        if (pad < 2) out.push_back(static_cast<char>(((v[1] & 15) << 4) | (v[2] >> 2)));
        if (pad < 1) out.push_back(static_cast<char>(((v[2] & 3) << 6) | v[3]));
    }
    return true;
}

std::string json_field(const std::string& json, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    const auto at = json.find(pat);
    if (at == std::string::npos) return "";
    auto colon = json.find(':', at + pat.size());
    if (colon == std::string::npos) return "";
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

std::string run_jwt(const std::string& in, bool& ok) {
    std::string s = trim(in);
    if (!s.empty() && lower(s).rfind("bearer ", 0) == 0) s = trim(s.substr(7));
    const auto d1 = s.find('.');
    const auto d2 = d1 == std::string::npos ? std::string::npos : s.find('.', d1 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos) return fail(ok, "need header.payload.signature");
    std::string header, payload;
    if (!b64_decode(s.substr(0, d1), header)) return fail(ok, "bad header encoding");
    if (!b64_decode(s.substr(d1 + 1, d2 - d1 - 1), payload)) return fail(ok, "bad payload encoding");
    const std::string sig = s.substr(d2 + 1);
    const std::string alg = json_field(header, "alg");
    Out o;
    o.add("alg", alg.empty() ? "?" : alg);
    if (lower(alg) == "none" || sig.empty()) o.add("warn", "signature not checked");
    else o.add("sig", "present, not verified");
    o.add("header", header);
    o.add("payload", payload);
    return o.str();
}

bool parse_hex_bytes(const std::string& in, std::string& out) {
    std::string hex;
    for (unsigned char c : in) {
        if (std::isxdigit(c)) hex.push_back(static_cast<char>(c));
        else if (c != ' ' && c != ':' && c != '-') return false;
    }
    if (hex.empty() || (hex.size() % 2) != 0) return false;
    out.clear();
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int v = 0;
        if (std::sscanf(hex.c_str() + i, "%2x", &v) != 1) return false;
        out.push_back(static_cast<char>(v));
    }
    return true;
}

std::string hex_preview(const std::string& bytes) {
    std::string hex, ascii;
    const size_t n = bytes.size() > 48 ? 48 : bytes.size();
    for (size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (!hex.empty()) hex += ' ';
        hex += fmt("%02X", c);
        ascii.push_back(c >= 32 && c < 127 ? static_cast<char>(c) : '.');
    }
    if (bytes.size() > n) hex += " ...";
    return hex + "  " + ascii;
}

std::string run_xor(const std::string& in, bool& ok) {
    const std::string s = trim(in);
    const auto sp = s.find(' ');
    if (sp == std::string::npos) return fail(ok, "need: key text   or   hex:0d text");
    std::string key = s.substr(0, sp);
    const std::string msg = s.substr(sp + 1);
    if (msg.empty()) return fail(ok, "need text after the key");
    std::string key_bytes;
    if (lower(key).rfind("hex:", 0) == 0) {
        if (!parse_hex_bytes(key.substr(4), key_bytes) || key_bytes.empty()) return fail(ok, "bad hex key");
    } else {
        key_bytes = key;
    }
    std::string out;
    out.resize(msg.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        out[i] = static_cast<char>(static_cast<unsigned char>(msg[i]) ^
                                   static_cast<unsigned char>(key_bytes[i % key_bytes.size()]));
    }
    Out o;
    o.add("len", std::to_string(out.size()));
    o.add("hex", hex_preview(out));
    return o.str();
}

struct CidrRange {
    uint32_t start;
    uint32_t end;
};

bool parse_cidr_token(const std::string& tok, CidrRange& range) {
    const auto slash = tok.find('/');
    if (slash == std::string::npos) return false;
    uint32_t ip = 0;
    if (!parse_ipv4(tok.substr(0, slash), ip)) return false;
    long long pfx = 0;
    if (!parse_int_range(tok.substr(slash + 1), pfx, 0, 32)) return false;
    const uint32_t mask = prefix_to_mask(static_cast<int>(pfx));
    range.start = ip & mask;
    range.end = range.start | ~mask;
    return true;
}

std::vector<std::string> cover_cidr(uint32_t start, uint32_t end) {
    std::vector<std::string> lines;
    while (true) {
        uint32_t max_host = 0;
        if (start == 0) max_host = 32;
        else {
            uint32_t x = start;
            while ((x & 1u) == 0u && max_host < 32) {
                x >>= 1;
                ++max_host;
            }
        }
        const uint64_t remain = static_cast<uint64_t>(end) - start + 1;
        uint32_t host = 0;
        while (host < max_host && (1ull << (host + 1)) <= remain) ++host;
        lines.push_back(fmt_ipv4(start) + "/" + std::to_string(32 - static_cast<int>(host)));
        const uint64_t next = static_cast<uint64_t>(start) + (1ull << host);
        if (next > static_cast<uint64_t>(end)) break;
        start = static_cast<uint32_t>(next);
    }
    return lines;
}

std::string run_cidr_agg(Tokens t, bool& ok) {
    skip_mode(t, {"agg", "supernet", "sum"});
    if (t.size() < 2 || t.size() > 16) return fail(ok, "need 2 to 16 networks, e.g. 10.0.0.0/24 10.0.1.0/24");
    std::vector<CidrRange> ranges;
    for (const auto& tok : t) {
        CidrRange r;
        if (!parse_cidr_token(tok, r)) return fail(ok, "bad CIDR " + tok);
        ranges.push_back(r);
    }
    for (size_t i = 0; i < ranges.size(); ++i) {
        for (size_t j = i + 1; j < ranges.size(); ++j) {
            if (ranges[j].start < ranges[i].start) std::swap(ranges[i], ranges[j]);
        }
    }
    std::vector<CidrRange> merged;
    for (const auto& r : ranges) {
        if (!merged.empty()) {
            const uint64_t lim = static_cast<uint64_t>(merged.back().end) + 1;
            if (r.start <= lim) {
                if (r.end > merged.back().end) merged.back().end = r.end;
                continue;
            }
        }
        merged.push_back(r);
    }
    Out o;
    std::vector<std::string> lines;
    for (const auto& r : merged) {
        auto part = cover_cidr(r.start, r.end);
        lines.insert(lines.end(), part.begin(), part.end());
    }
    o.add("blocks", std::to_string(lines.size()));
    for (const auto& line : lines) o.line(line);
    return o.str();
}

const char* lookup_oui(uint32_t oui) {
    static const char* table[] = {
        "00000C Cisco",        "000393 Apple",        "000569 VMware",       "00090F Fortinet",
        "000A95 Apple",        "000B86 Aruba",        "000C29 VMware",       "000C42 MikroTik",
        "000D88 D-Link",       "000D93 Apple",        "000E0C Intel",        "000F20 HP",
        "000FB5 Netgear",      "001018 Broadcom",     "001083 HP",           "001143 Dell",
        "001185 HP",           "00124B Texas Instruments", "00037F Atheros", "001451 Apple",
        "00155D Microsoft",    "00156D Ubiquiti",     "00166F Intel",        "001676 Intel",
        "0016CB Apple",        "0016EA Intel",        "0016EB Intel",        "0017A4 HP",
        "0017AB Nintendo",     "0017F2 Apple",        "001882 Huawei",       "00195B D-Link",
        "0019E3 Apple",        "001A11 Google",       "001A1E Aruba",        "001A80 Sony",
        "001A8A Samsung",      "001B11 D-Link",       "001B17 Palo Alto",    "001B21 Intel",
        "001B2F Netgear",      "001B63 Apple",        "001B77 Intel",        "001B78 HP",
        "001BEA Nintendo",     "001C23 Dell",         "001CB3 Apple",        "001CF0 D-Link",
        "001D25 Samsung",      "001D4F Apple",        "001DBA Sony",         "001E0B HP",
        "001E2A Netgear",      "001E35 Nintendo",     "001E52 Apple",        "001E58 D-Link",
        "001E64 Intel",        "001E65 Intel",        "001E67 Intel",        "001EC2 Apple",
        "001EC9 Dell",         "001EE1 Samsung",      "001F32 Nintendo",     "001F33 Netgear",
        "001F3B Intel",        "001F5B Apple",        "001FF3 Apple",        "002119 Samsung",
        "00215A HP",           "002170 Dell",         "002191 D-Link",       "0021E9 Apple",
        "002241 Apple",        "0022AA Nintendo",     "002312 Apple",        "002332 Apple",
        "002339 Samsung",      "0023DF Apple",        "002436 Apple",        "00246C Aruba",
        "0024BE Sony",         "0024F3 Nintendo",     "002500 Apple",        "00254B Apple",
        "00259E Huawei",       "0025B3 HP",           "0025BC Apple",        "002608 Apple",
        "002637 Samsung",      "00264A Apple",        "0026B0 Apple",        "0026BB Apple",
        "0026F2 Netgear",      "005056 VMware",       "00E04C Realtek",      "00E0FC Huawei",
        "0418D6 Ubiquiti",     "045D4B Sony",         "049226 ASUS",         "04D4C4 ASUS",
        "04F938 Huawei",       "080027 VirtualBox",   "0808C2 Samsung",      "08306B Palo Alto",
        "0C1DAF Xiaomi",       "10521C Espressif",    "14CC20 TP-Link",      "18742E Amazon",
        "18E829 Ubiquiti",     "2008ED Huawei",       "204C03 Aruba",        "20E52A Netgear",
        "240AC4 Espressif",    "246F28 Espressif",    "24A43C Ubiquiti",     "24DEC6 Aruba",
        "286C07 Xiaomi",       "28CFE9 Apple",        "2C4D54 ASUS",         "2CC81B MikroTik",
        "2CF432 Espressif",    "30AEA4 Espressif",    "3480B3 Xiaomi",       "3C22FB Apple",
        "3C5AB4 Google",       "3C6105 Espressif",    "3CFDFE Intel",        "40B4CD Amazon",
        "40F520 Espressif",    "44D9E7 Ubiquiti",     "483FDA Espressif",    "488F5A MikroTik",
        "50642B Xiaomi",       "50C7BF TP-Link",      "50DCE7 Amazon",       "525400 QEMU",
        "546C0E Texas Instruments", "54EE75 Lenovo",  "5C0A5B Samsung",      "640980 Xiaomi",
        "64B473 Xiaomi",       "6837E9 Amazon",       "6C3B6B MikroTik",     "6CF37F Aruba",
        "70723C Huawei",       "742344 Xiaomi",       "744D28 MikroTik",     "7483C2 Ubiquiti",
        "74C246 Amazon",       "74D02B ASUS",         "7811DC Xiaomi",       "788A20 Ubiquiti",
        "7CBB8A Nintendo",     "7CDFA1 Espressif",    "802AA8 Ubiquiti",     "84CCA8 Espressif",
        "84D6D0 Amazon",       "84F3EB Espressif",    "8C71F8 Samsung",      "8CAAB5 Espressif",
        "94B40F Aruba",        "94B97E Espressif",    "98B6E9 Nintendo",     "98DAC4 TP-Link",
        "98FA9B Lenovo",       "9C99A0 Xiaomi",       "A0F3C1 TP-Link",      "A4CF12 Espressif",
        "AC67B2 Espressif",    "AC9E17 ASUS",         "ACE215 Huawei",       "B0A737 Roku",
        "B0B448 Texas Instruments", "B4E62D Espressif", "B4FBE4 Ubiquiti",  "B827EB Raspberry Pi",
        "B869F4 MikroTik",     "B8CA3A Dell",         "C006C3 TP-Link",      "C03F0E Netgear",
        "C45BBE Espressif",    "C4AD34 MikroTik",     "C8C9A3 Espressif",    "CC50E3 Espressif",
        "CC6DA0 Roku",         "D03972 Texas Instruments", "D4CA6D MikroTik", "D83134 Roku",
        "D83ADD Raspberry Pi", "D8A01D Espressif",    "D8C7C8 Aruba",        "DCA632 Raspberry Pi",
        "DC9FDB Ubiquiti",     "E00C7F Nintendo",     "E0247F Huawei",               "E063DA Ubiquiti",     "E45F01 Raspberry Pi", "E48D8C MikroTik",     "EC94CB Espressif",
        "F09FC2 Ubiquiti",     "F4CFA2 Espressif",    "F47B5E Samsung",      "F4F5D8 Google",
        "F8BC12 Dell",         "FCECDA Ubiquiti",
    };
    char key[8];
    std::snprintf(key, sizeof(key), "%06X", oui & 0xFFFFFFu);
    for (const char* row : table) {
        if (std::strncmp(row, key, 6) == 0) return row + 7;
    }
    return nullptr;
}

std::string run_oui(const std::string& in, bool& ok) {
    std::string hex;
    for (unsigned char c : in) {
        if (std::isxdigit(c)) hex.push_back(static_cast<char>(std::toupper(c)));
    }
    if (hex.size() != 6 && hex.size() != 12) return fail(ok, "need a MAC or OUI, e.g. 24:0A:C4:11:22:33");
    unsigned int oui = 0;
    if (std::sscanf(hex.c_str(), "%6x", &oui) != 1) return fail(ok, "bad MAC");
    const unsigned first = (oui >> 16) & 0xFF;
    Out o;
    o.add("oui", fmt("%02X:%02X:%02X", (oui >> 16) & 0xFF, (oui >> 8) & 0xFF, oui & 0xFF));
    if (const char* vendor = lookup_oui(oui)) {
        o.add("vendor", vendor);
    } else if (first & 0x02) {
        o.add("vendor", "locally administered");
        o.add("note", "not a factory OUI");
    } else if (first & 0x01) {
        o.add("vendor", "group address");
    } else {
        o.add("vendor", "unknown");
        o.add("note", "not in the built-in list");
    }
    return o.str();
}

double fspl_db(double hz, double meters) {
    return 20.0 * std::log10(meters) + 20.0 * std::log10(hz) - 147.55;
}

std::string run_fspl(Tokens t, bool& ok) {
    enum { Loss, Link };
    int mode = pick_mode(t, {{"fspl", Loss}, {"loss", Loss}, {"link", Link}, {"budget", Link}});
    if (mode == Link) {
        if (t.size() < 5) return fail(ok, "need: link tx_dBm tx_dB rx_dB freq dist");
        double tx = 0, gtx = 0, grx = 0, hz = 0, meters = 0, extra = 0;
        if (!parse_double(t[0], tx) || !parse_double(t[1], gtx) || !parse_double(t[2], grx))
            return fail(ok, "tx and antenna gains must be numbers (dBm, dB)");
        if (!parse_radio_hz(t[3], hz)) return fail(ok, "bad frequency");
        if (!parse_length_m(t[4], "m", meters) || !(meters > 0)) return fail(ok, "distance must be > 0");
        if (t.size() >= 6 && !parse_double(t[5], extra)) return fail(ok, "extra loss must be dB");
        const double loss = fspl_db(hz, meters);
        const double rx = tx + gtx + grx - loss - extra;
        Out o;
        o.add("rx", sig(rx) + " dBm");
        o.add("FSPL", sig(loss) + " dB");
        o.add("tx", sig(tx) + " dBm");
        return o.str();
    }
    if (t.size() < 2) return fail(ok, "need: freq distance   e.g. 2.4G 1km");
    double hz = 0, meters = 0;
    if (!parse_radio_hz(t[0], hz)) return fail(ok, "bad frequency (e.g. 2.4G, 433M)");
    if (!parse_length_m(t[1], "m", meters) || !(meters > 0)) return fail(ok, "distance must be > 0");
    Out o;
    o.add("FSPL", sig(fspl_db(hz, meters)) + " dB");
    o.add("f", eng(hz) + "Hz");
    o.add("distance", fmt_len(meters));
    return o.str();
}

bool split_suffix_raw(const std::string& s, double& v, std::string& suffix) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(d)) return false;
    v = d;
    suffix = end;
    return true;
}

bool parse_size_bytes(const std::string& tok, double& bytes) {
    double n = 0;
    std::string suf;
    if (!split_suffix_raw(tok, n, suf) || n < 0) return false;
    const std::string u = lower(suf);
    if (u.empty() || u == "b") bytes = n;
    else if (u == "kb") bytes = n * 1e3;
    else if (u == "mb") bytes = n * 1e6;
    else if (u == "gb") bytes = n * 1e9;
    else if (u == "tb") bytes = n * 1e12;
    else if (u == "kib") bytes = n * 1024.0;
    else if (u == "mib") bytes = n * 1048576.0;
    else if (u == "gib") bytes = n * 1073741824.0;
    else if (u == "tib") bytes = n * 1099511627776.0;
    else return false;
    return true;
}

bool parse_bit_rate(const std::string& tok, double& bps) {
    double n = 0;
    std::string suf;
    if (!split_suffix_raw(tok, n, suf) || !(n > 0)) return false;
    if (suf.empty()) {
        bps = n * 1e6;
        return true;
    }
    const bool byte_rate = suf.find('B') != std::string::npos && suf.find('b') == std::string::npos;
    const std::string u = lower(suf);
    double scale = 1e6;
    if (u.find('g') != std::string::npos) scale = 1e9;
    else if (u.find('k') != std::string::npos) scale = 1e3;
    else if (u.find('m') != std::string::npos) scale = 1e6;
    if (byte_rate) scale *= 8.0;
    bps = n * scale;
    return true;
}

std::string run_transfer(Tokens t, bool& ok) {
    skip_mode(t, {"xfer", "time", "copy"});
    if (t.size() != 2) return fail(ok, "need: size rate   e.g. 1GiB 100Mbps");
    double bytes = 0, bps = 0;
    if (!parse_size_bytes(t[0], bytes)) return fail(ok, "bad size (B KB MB GB KiB MiB GiB)");
    if (!parse_bit_rate(t[1], bps)) return fail(ok, "bad rate (Mbps, or MBps)");
    const double seconds = bytes * 8.0 / bps;
    Out o;
    o.add("time", fmt_hms_span(static_cast<long long>(std::llround(seconds))));
    o.add("seconds", sig(seconds));
    o.add("rate", sig(bps / 1e6) + " Mbps");
    return o.str();
}

std::string run_kwh(Tokens t, bool& ok) {
    skip_mode(t, {"kwh", "cost", "power"});
    auto watts_of = [](const std::string& tok, double& w) {
        double n = 0;
        std::string suf;
        if (!split_suffix_raw(tok, n, suf)) return false;
        const std::string u = lower(suf);
        if (u.empty() || u == "w") w = n;
        else if (u == "kw") w = n * 1000.0;
        else return false;
        return w >= 0;
    };
    auto hours_of = [](const std::string& tok, double& h) {
        double n = 0;
        std::string suf;
        if (!split_suffix_raw(tok, n, suf) || n < 0) return false;
        const std::string u = lower(suf);
        if (u.empty() || u == "h" || u == "hr") h = n;
        else if (u == "min" || u == "m") h = n / 60.0;
        else return false;
        return true;
    };
    double kwh = 0, price = 0;
    if (t.size() == 2) {
        if (!parse_double(t[0], kwh) || kwh < 0) return fail(ok, "kWh must be >= 0");
        if (!parse_double(t[1], price) || price < 0) return fail(ok, "price must be >= 0");
    } else if (t.size() == 3) {
        double w = 0, h = 0;
        if (!watts_of(t[0], w)) return fail(ok, "power must be W or kW");
        if (!hours_of(t[1], h)) return fail(ok, "time must be hours (or 30min)");
        if (!parse_double(t[2], price) || price < 0) return fail(ok, "price per kWh must be >= 0");
        kwh = w * h / 1000.0;
    } else {
        return fail(ok, "need: kWh price | watts hours price");
    }
    Out o;
    o.add("kWh", sig(kwh));
    o.add("price", sig(price));
    o.add("cost", fmt("%.2f", kwh * price));
    return o.str();
}

std::string fmt_dms(double deg, bool lat) {
    const char hemi = lat ? (deg >= 0 ? 'N' : 'S') : (deg >= 0 ? 'E' : 'W');
    double a = std::fabs(deg);
    int d = static_cast<int>(a);
    double mins = (a - d) * 60.0;
    int m = static_cast<int>(mins);
    double s = (mins - m) * 60.0;
    if (s >= 59.995) {
        s = 0;
        ++m;
    }
    if (m >= 60) {
        m = 0;
        ++d;
    }
    return fmt("%d %02d %05.2f %c", d, m, s, hemi);
}

bool parse_dms_side(const Tokens& t, size_t at, double& deg) {
    if (at + 3 >= t.size()) return false;
    double d = 0, m = 0, s = 0;
    if (!parse_double(t[at], d) || !parse_double(t[at + 1], m) || !parse_double(t[at + 2], s)) return false;
    if (d < 0 || m < 0 || m >= 60 || s < 0 || s >= 60) return false;
    const std::string h = lower(t[at + 3]);
    const double mag = d + m / 60.0 + s / 3600.0;
    if (h == "n" || h == "e") deg = mag;
    else if (h == "s" || h == "w") deg = -mag;
    else return false;
    return true;
}

std::string run_coord(Tokens t, bool& ok) {
    skip_mode(t, {"dd", "dms", "coord"});
    t = split_commas(t);
    if (t.size() == 2) {
        double lat = 0, lon = 0;
        if (!parse_double(t[0], lat) || !parse_double(t[1], lon)) return fail(ok, "need lat lon");
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return fail(ok, "lat -90..90, lon -180..180");
        Out o;
        o.add("lat", fmt_dms(lat, true));
        o.add("lon", fmt_dms(lon, false));
        o.add("dd", sig(lat) + " " + sig(lon));
        return o.str();
    }
    if (t.size() == 8) {
        double lat = 0, lon = 0;
        if (!parse_dms_side(t, 0, lat) || !parse_dms_side(t, 4, lon))
            return fail(ok, "need: d m s N/S d m s E/W");
        if (std::fabs(lat) > 90 || std::fabs(lon) > 180) return fail(ok, "lat/lon out of range");
        Out o;
        o.add("lat", sig(lat));
        o.add("lon", sig(lon));
        o.add("dd", sig(lat) + " " + sig(lon));
        return o.str();
    }
    return fail(ok, "need: lat lon | d m s N d m s W");
}

std::string run_hex_view(const std::string& in, bool& ok) {
    std::string raw = in;
    if (!raw.empty() && raw.back() == '\n') raw.pop_back();
    std::string bytes;
    const std::string low = lower(trim(raw));
    if (low.rfind("hex:", 0) == 0) {
        if (!parse_hex_bytes(trim(raw).substr(4), bytes)) return fail(ok, "bad hex");
    } else {
        bytes = raw;
    }
    if (bytes.empty()) return fail(ok, "type text, or hex:001122");
    const bool cut = bytes.size() > 64;
    if (cut) bytes.resize(64);
    Out o;
    for (size_t i = 0; i < bytes.size(); i += 4) {
        std::string hex, ascii;
        const size_t n = std::min<size_t>(4, bytes.size() - i);
        for (size_t k = 0; k < n; ++k) {
            const auto c = static_cast<unsigned char>(bytes[i + k]);
            if (k) hex += ' ';
            hex += fmt("%02X", c);
            ascii.push_back(c >= 32 && c < 127 ? static_cast<char>(c) : '.');
        }
        o.line(fmt("%04X  %-11s  %s", static_cast<unsigned>(i), hex.c_str(), ascii.c_str()));
    }
    if (cut) o.line("... truncated at 64 bytes");
    return o.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const char* op_label(Op op) {
    switch (op) {
    case Op::Subnet: return "Subnet";
    case Op::BaseConvert: return "Base converter";
    case Op::ByteUnits: return "Byte units";
    case Op::Resistor: return "Resistor color";
    case Op::LedResistor: return "LED resistor";
    case Op::VoltageDivider: return "Voltage divider";
    case Op::Baud: return "Baud bit-time";
    case Op::Dbm: return "dBm and power";
    case Op::Epoch: return "Epoch";
    case Op::Measure: return "Temp/Length/AWG";
    case Op::Bitmask: return "Bitmask";
    case Op::Crc: return "CRC";
    case Op::Units: return "Units";
    case Op::OhmsLaw: return "Ohm's law";
    case Op::VoltageDrop: return "Voltage drop";
    case Op::Motor: return "Motor";
    case Op::Gas: return "Gas law";
    case Op::Flow: return "Flow and pipe";
    case Op::Parallel: return "Parallel resistors";
    case Op::RcTimer: return "RC timer";
    case Op::Battery: return "Battery life";
    case Op::Frequency: return "Frequency";
    case Op::Pwm: return "PWM";
    case Op::Percentage: return "Percentage";
    case Op::DateDiff: return "Date difference";
    case Op::TimeDiff: return "Time difference";
    case Op::TipSplit: return "Tip split";
    case Op::Distance: return "Distance";
    case Op::Reactance: return "RC/LC reactance";
    case Op::Timer555: return "555 timer";
    case Op::Entropy: return "Password entropy";
    case Op::HashId: return "Hash identify";
    case Op::Jwt: return "JWT decode";
    case Op::Xor: return "XOR";
    case Op::CidrAgg: return "CIDR aggregate";
    case Op::Oui: return "MAC vendor";
    case Op::Fspl: return "Link budget";
    case Op::Transfer: return "Transfer time";
    case Op::Kwh: return "kWh cost";
    case Op::Coord: return "Coordinates";
    case Op::HexView: return "Hex viewer";
    case Op::Count: break;
    }
    return "Calc";
}

const char* input_hint(Op op) {
    switch (op) {
    case Op::Subnet:
        return "ip/prefix | v4 ip pfx | mask ip mask | split net pfx N [i] | hosts net pfx H [i] | v6 addr [pfx]";
    case Op::BaseConvert:
        return "value from to [width] | all value base [width] | pow n   e.g. FF 16 10";
    case Op::ByteUnits: return "count [B|KB|MB|GB|TB|KiB|MiB|GiB|TiB]   bare number = bytes";
    case Op::Resistor:
        return "colors c1 c2 c3 c4.. | 4|5|6 colors | rev 4.7k   (or bare colors / ohm value)";
    case Op::LedResistor: return "Vs Vf [mA=20]   e.g. 5 2.0 20";
    case Op::VoltageDivider:
        return "div R1 R2 Vin | design Vin Vout R1 | ratio r   (bare R1 R2 Vin ok)";
    case Op::Baud: return "baud [bits_per_frame=10]   e.g. 115200";
    case Op::Dbm: return "value [dBm|mW|W] [gain_dB]   e.g. 10 dbm 3  or  dbm 0";
    case Op::Epoch: return "s secs | ms millis | date YYYY-MM-DD [HH:MM:SS]   (bare number = secs)";
    case Op::Measure: return "temp v [C|F|K] | len v [mm|in|mil] | awg n | awg dia v [unit]";
    case Op::Bitmask: return "bits [width] 0,3,7 | hex [width] 0x89   width 8/16/32/64 (def 32)";
    case Op::Crc:
        return "crc32|ccitt|modbus|all hex bytes | text str   (bare text = CRC-32)";
    case Op::Units: return "value [dim] from to  e.g. 10 ft m | list [dim]";
    case Op::OhmsLaw: return "dc V I R W (- unknown; or V R | v=12 r=100) | 3p V I [pf]";
    case Op::VoltageDrop: return "[cu|al] length(m|ft) amps volts awg   e.g. cu 15 10 120 12";
    case Op::Motor: return "hp torque rpm [eff%]  (- unknown; torque 50nm|50lbft)";
    case Op::Gas:
        return "boyle P1 V1 P2 V2 | boyle P1 V1 T1 P2 V2 T2 | ideal|mass [gauge|abs] P T V | air t";
    case Op::Flow:
        return "q ID vel | dv Q ID vel | vol ID len | darcy ID len Q [rough]   (mm, m/s, L/min)";
    case Op::Parallel: return "R1 R2 [R3..]   e.g. 10k 10k or 10k 4.7k 1k";
    case Op::RcTimer: return "R C [charge%]   e.g. 10k 100n or 10k 100n 90";
    case Op::Battery: return "mAh mA | mah hours mA | ma mAh hours   e.g. 2000 100 or 2000mah 8h";
    case Op::Frequency: return "Hz | t period | wave length [vf]   e.g. 433M or 2.4G 0.95 or wave 70cm";
    case Op::Pwm: return "freq duty% | freq high-time   e.g. 1k 25 or 1k 250us";
    case Op::Percentage: return "X% of Y | change old new | ratio part whole   e.g. 15% 200";
    case Op::DateDiff: return "YYYY-MM-DD YYYY-MM-DD | add YYYY-MM-DD days";
    case Op::TimeDiff:
        return "HH:MM HH:MM | add HH:MM 2h30 | YYYY-MM-DD HH:MM YYYY-MM-DD HH:MM";
    case Op::TipSplit: return "bill tip% [people=1]   e.g. 50 15 3";
    case Op::Distance: return "lat1 lon1 lat2 lon2   e.g. 40.7 -74 34.05 -118.2";
    case Op::Reactance: return "c F C | l F L | rc F R C | lc L C   e.g. c 1k 100n";
    case Op::Timer555: return "astable Ra Rb C | mono R C   e.g. 10k 10k 100n";
    case Op::Entropy: return "password | len N charset | words N | rate guesses/s password";
    case Op::HashId: return "paste a hash   e.g. $6$... or 64 hex chars";
    case Op::Jwt: return "header.payload.signature";
    case Op::Xor: return "key text | hex:0d text";
    case Op::CidrAgg: return "net/pfx net/pfx ...   e.g. 10.0.0.0/24 10.0.1.0/24";
    case Op::Oui: return "MAC or OUI   e.g. 24:0A:C4:11:22:33";
    case Op::Fspl: return "freq dist | link tx_dBm tx_dB rx_dB freq dist   e.g. 2.4G 1km";
    case Op::Transfer: return "size rate   e.g. 1GiB 100Mbps   (MBps is bytes/s)";
    case Op::Kwh: return "kWh price | watts hours price   e.g. 1500W 4h 0.25";
    case Op::Coord: return "lat lon | d m s N d m s W";
    case Op::HexView: return "text | hex:001122";
    case Op::Count: break;
    }
    return "";
}

const char* input_example(Op op) {
    switch (op) {
    case Op::Subnet: return "e.g.  192.168.1.10/24   or   mask 10.0.0.5 255.255.255.0";
    case Op::BaseConvert: return "e.g.  FF 16 10   or   all 255 10";
    case Op::ByteUnits: return "e.g.  1536  or  1.5 MiB";
    case Op::Resistor: return "e.g.  brown black red gold   or   rev 1k";
    case Op::LedResistor: return "e.g.  5 2.0 20";
    case Op::VoltageDivider: return "e.g.  10k 4.7k 5   or   design 5 3.3 10k";
    case Op::Baud: return "e.g.  115200";
    case Op::Dbm: return "e.g.  10 dbm 3";
    case Op::Epoch: return "e.g.  1700000000   or   date 2024-01-01";
    case Op::Measure: return "e.g.  temp 25 C   or   awg 24";
    case Op::Bitmask: return "e.g.  bits 8 0,3,7   or   hex 0x89";
    case Op::Crc: return "e.g.  crc32 deadbeef   or   text hello";
    case Op::Units: return "e.g.  10 ft m";
    case Op::OhmsLaw: return "e.g.  v=12 r=100   or   dc 12 - 100 -";
    case Op::VoltageDrop: return "e.g.  cu 15 10 120 12";
    case Op::Motor: return "e.g.  1 - 1800 85";
    case Op::Gas: return "e.g.  boyle 1 2 2 -";
    case Op::Flow: return "e.g.  q 20 1.5";
    case Op::Parallel: return "e.g.  10k 10k   or   10k 4.7k 1k";
    case Op::RcTimer: return "e.g.  10k 100n   or   10k 100n 90";
    case Op::Battery: return "e.g.  2000 100   or   mah 8 50";
    case Op::Frequency: return "e.g.  433M   or   2.4G 0.95";
    case Op::Pwm: return "e.g.  1k 25   or   1k 250us";
    case Op::Percentage: return "e.g.  15% 200   or   change 80 100";
    case Op::DateDiff: return "e.g.  2024-01-01 2024-06-01";
    case Op::TimeDiff: return "e.g.  14:30 18:45   or   23:10 01:40";
    case Op::TipSplit: return "e.g.  50 15 3";
    case Op::Distance: return "e.g.  40.7 -74 34.05 -118.2";
    case Op::Reactance: return "e.g.  c 1k 100n   or   lc 10u 100n";
    case Op::Timer555: return "e.g.  10k 10k 100n   or   mono 10k 100n";
    case Op::Entropy: return "e.g.  hunter2   or   len 12 95";
    case Op::HashId: return "e.g.  $2b$...   or a hex digest";
    case Op::Jwt: return "e.g.  header.payload.signature";
    case Op::Xor: return "e.g.  secret hello";
    case Op::CidrAgg: return "e.g.  10.0.0.0/24 10.0.1.0/24";
    case Op::Oui: return "e.g.  24:0A:C4:11:22:33";
    case Op::Fspl: return "e.g.  2.4G 1km   or   link 20 2 2 2.4G 1km";
    case Op::Transfer: return "e.g.  1GiB 100Mbps";
    case Op::Kwh: return "e.g.  1500W 4h 0.25";
    case Op::Coord: return "e.g.  40.7128 -74.006";
    case Op::HexView: return "e.g.  hello   or   hex:68656c6c6f";
    case Op::Count: break;
    }
    return "Enter values, then Enter";
}

std::string run(Op op, const std::string& input, bool& ok) {
    ok = true;
    const std::string in = trim(input);
    if (in.empty()) {
        ok = false;
        return std::string("empty input - ") + input_hint(op);
    }
    Tokens t = split_ws(in);

    switch (op) {
    case Op::Subnet: return run_subnet(t, ok);
    case Op::BaseConvert: return run_base(t, ok);
    case Op::ByteUnits: return run_bytes(t, ok);
    case Op::Resistor: return run_resistor(t, ok);
    case Op::LedResistor: return run_led(t, ok);
    case Op::VoltageDivider: return run_divider(t, ok);
    case Op::Baud: return run_baud(t, ok);
    case Op::Dbm: return run_dbm(t, ok);
    case Op::Epoch: return run_epoch(t, ok);
    case Op::Measure: return run_measure(t, ok);
    case Op::Bitmask: return run_bitmask(t, ok);
    case Op::Crc: return run_crc(in, t, ok);
    case Op::Units: return run_units(t, ok);
    case Op::OhmsLaw: return run_ohm(t, ok);
    case Op::VoltageDrop: return run_vdrop(t, ok);
    case Op::Motor: return run_motor(t, ok);
    case Op::Gas: return run_gas(t, ok);
    case Op::Flow: return run_flow(t, ok);
    case Op::Parallel: return run_parallel(t, ok);
    case Op::RcTimer: return run_rc(t, ok);
    case Op::Battery: return run_battery(t, ok);
    case Op::Frequency: return run_frequency(t, ok);
    case Op::Pwm: return run_pwm(t, ok);
    case Op::Percentage: return run_percentage(t, ok);
    case Op::DateDiff: return run_date_diff(t, ok);
    case Op::TimeDiff: return run_time_diff(t, ok);
    case Op::TipSplit: return run_tip(t, ok);
    case Op::Distance: return run_distance(t, ok);
    case Op::Reactance: return run_reactance(t, ok);
    case Op::Timer555: return run_555(t, ok);
    case Op::Entropy: return run_entropy(in, ok);
    case Op::HashId: return run_hash_id(in, ok);
    case Op::Jwt: return run_jwt(in, ok);
    case Op::Xor: return run_xor(in, ok);
    case Op::CidrAgg: return run_cidr_agg(t, ok);
    case Op::Oui: return run_oui(in, ok);
    case Op::Fspl: return run_fspl(t, ok);
    case Op::Transfer: return run_transfer(t, ok);
    case Op::Kwh: return run_kwh(t, ok);
    case Op::Coord: return run_coord(t, ok);
    case Op::HexView: return run_hex_view(in, ok);
    case Op::Count: break;
    }
    ok = false;
    return "unknown calculator";
}

namespace {

const int* group_items(Group group, int& count) {
    static const int kNetwork[] = {static_cast<int>(Op::Subnet), static_cast<int>(Op::CidrAgg)};
    static const int kElectronics[] = {
        static_cast<int>(Op::OhmsLaw),     static_cast<int>(Op::Resistor),
        static_cast<int>(Op::LedResistor), static_cast<int>(Op::VoltageDivider),
        static_cast<int>(Op::Parallel),    static_cast<int>(Op::VoltageDrop),
        static_cast<int>(Op::RcTimer),     static_cast<int>(Op::Reactance),
        static_cast<int>(Op::Timer555),    static_cast<int>(Op::Battery),
        static_cast<int>(Op::Pwm)};
    static const int kRadio[] = {static_cast<int>(Op::Frequency), static_cast<int>(Op::Dbm),
                                 static_cast<int>(Op::Fspl)};
    static const int kCyber[] = {static_cast<int>(Op::Entropy), static_cast<int>(Op::HashId),
                                 static_cast<int>(Op::Jwt),     static_cast<int>(Op::Xor),
                                 static_cast<int>(Op::Oui)};
    static const int kData[] = {
        static_cast<int>(Op::BaseConvert), static_cast<int>(Op::ByteUnits), static_cast<int>(Op::Transfer),
        static_cast<int>(Op::Bitmask),     static_cast<int>(Op::Crc),       static_cast<int>(Op::HexView),
        static_cast<int>(Op::Epoch),       static_cast<int>(Op::DateDiff),  static_cast<int>(Op::TimeDiff),
        static_cast<int>(Op::Kwh),         static_cast<int>(Op::Baud)};
    static const int kGps[] = {static_cast<int>(Op::Distance), static_cast<int>(Op::Coord)};
    static const int kGeneral[] = {static_cast<int>(Op::Percentage), static_cast<int>(Op::TipSplit),
                                   static_cast<int>(Op::Units),      static_cast<int>(Op::Measure),
                                   static_cast<int>(Op::Motor),      static_cast<int>(Op::Gas),
                                   static_cast<int>(Op::Flow)};
    static const int kOthers[] = {kMenuLifeBase, kMenuLifeBase + 1, kMenuLifeBase + 2};
    switch (group) {
    case Group::Network: count = 2; return kNetwork;
    case Group::Electronics: count = 11; return kElectronics;
    case Group::Radio: count = 3; return kRadio;
    case Group::Cyber: count = 5; return kCyber;
    case Group::Data: count = 11; return kData;
    case Group::Gps: count = 2; return kGps;
    case Group::General: count = 7; return kGeneral;
    case Group::Others: count = 3; return kOthers;
    case Group::Count: break;
    }
    count = 0;
    return nullptr;
}

} // namespace

const char* group_label(Group group) {
    switch (group) {
    case Group::Network: return "Network";
    case Group::Electronics: return "Electronics";
    case Group::Radio: return "Radio";
    case Group::Cyber: return "Cyber";
    case Group::Data: return "Data";
    case Group::Gps: return "GPS";
    case Group::General: return "General use";
    case Group::Others: return "Others";
    case Group::Count: break;
    }
    return "Calcs";
}

int group_size(Group group) {
    int n = 0;
    group_items(group, n);
    return n;
}

int group_item_id(Group group, int index) {
    int n = 0;
    const int* items = group_items(group, n);
    if (!items || index < 0 || index >= n) return 0;
    return items[index];
}

const char* group_item_label(Group group, int index) {
    const int id = group_item_id(group, index);
    if (id == kMenuLifeBase) return "Yu-Gi-Oh LP";
    if (id == kMenuLifeBase + 1) return "Magic Life";
    if (id == kMenuLifeBase + 2) return "Truco";
    return op_label(static_cast<Op>(id));
}

} // namespace tools::engcalc

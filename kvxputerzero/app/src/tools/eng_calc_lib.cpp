#include "eng_calc_lib.h"

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
    case Op::Count: break;
    }
    ok = false;
    return "unknown calculator";
}

} // namespace tools::engcalc

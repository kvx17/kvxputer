#include "calc_menu.h"
#include "calc_shell.h"

#include <cstdio>
#include <cstring>
#include <globals.h>

namespace {

// Strict dotted-quad: exactly four decimal octets 0..255, nothing else.
static bool parseIpv4(const String &s, uint32_t &ip, String *errMsg = nullptr) {
    String t = calcTrim(s);
    if (!t.length()) {
        if (errMsg) *errMsg = "empty IPv4";
        return false;
    }
    int parts[4] = {0, 0, 0, 0};
    int part = 0;
    int digits = 0;
    int val = 0;
    for (size_t i = 0; i < t.length(); i++) {
        char c = t[i];
        if (c >= '0' && c <= '9') {
            if (digits >= 3) {
                if (errMsg) *errMsg = "octet too long";
                return false;
            }
            val = val * 10 + (c - '0');
            digits++;
            if (val > 255) {
                if (errMsg) *errMsg = "octet >255";
                return false;
            }
        } else if (c == '.') {
            if (digits == 0) {
                if (errMsg) *errMsg = "empty octet";
                return false;
            }
            if (part >= 3) {
                if (errMsg) *errMsg = "too many octets";
                return false;
            }
            parts[part++] = val;
            val = 0;
            digits = 0;
        } else {
            if (errMsg) *errMsg = "bad IPv4 chars";
            return false;
        }
    }
    if (digits == 0) {
        if (errMsg) *errMsg = "empty octet";
        return false;
    }
    if (part != 3) {
        if (errMsg) *errMsg = "need 4 octets";
        return false;
    }
    parts[3] = val;
    ip = ((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) | ((uint32_t)parts[2] << 8) |
         (uint32_t)parts[3];
    return true;
}

static String fmtIpv4(uint32_t ip) {
    char buf[20];
    snprintf(
        buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xff), (unsigned)((ip >> 16) & 0xff),
        (unsigned)((ip >> 8) & 0xff), (unsigned)(ip & 0xff)
    );
    return String(buf);
}

static uint32_t prefixToMask(int p) {
    if (p <= 0) return 0;
    if (p >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - p);
}

static int maskToPrefix(uint32_t mask) {
    // Require contiguous mask
    uint32_t inv = ~mask;
    if (inv & (inv + 1)) return -1; // non-contiguous
    int p = 0;
    uint32_t m = mask;
    while (m & 0x80000000u) {
        p++;
        m <<= 1;
    }
    if (m) return -1;
    return p;
}

static void pushIpv4Results(CalcSession &s, uint32_t ip, int prefix) {
    uint32_t mask = prefixToMask(prefix);
    uint32_t net = ip & mask;
    uint32_t bcast = net | ~mask;
    uint32_t wild = ~mask;
    uint64_t hosts = 0;
    if (prefix >= 31) hosts = 0;
    else hosts = ((uint64_t)1 << (32 - prefix)) - 2;

    String role = "host";
    if (ip == net) role = "network";
    else if (ip == bcast) role = "broadcast";

    s.results.clear();
    s.results.push_back({"network", fmtIpv4(net), false});
    s.results.push_back({"broadcast", fmtIpv4(bcast), false});
    if (prefix < 31) {
        s.results.push_back({"first", fmtIpv4(net + 1), false});
        s.results.push_back({"last", fmtIpv4(bcast - 1), false});
    } else {
        s.results.push_back({"first", "-", false});
        s.results.push_back({"last", "-", false});
    }
    s.results.push_back({"hosts", String((unsigned long long)hosts), true});
    s.results.push_back({"wildcard", fmtIpv4(wild), false});
    s.results.push_back({"mask", fmtIpv4(mask) + " /" + String(prefix), false});
    s.results.push_back({"role", role, false});
}

// ---- IPv6 helpers ----------------------------------------------------------

struct V6 {
    uint16_t w[8] = {};
};

static bool parseIpv6(const String &in, V6 &out) {
    String s = calcTrim(in);
    // Strip zone id
    int pct = s.indexOf('%');
    if (pct >= 0) s = s.substring(0, pct);
    // Split prefix if present for parse of address alone
    int slash = s.indexOf('/');
    if (slash >= 0) s = s.substring(0, slash);

    int dc = s.indexOf("::");
    std::vector<String> left, right;
    auto split = [](const String &part, std::vector<String> &outParts) {
        outParts.clear();
        if (!part.length()) return;
        int start = 0;
        for (;;) {
            int c = part.indexOf(':', start);
            if (c < 0) {
                outParts.push_back(part.substring(start));
                break;
            }
            outParts.push_back(part.substring(start, c));
            start = c + 1;
            if (start > (int)part.length()) break;
        }
    };

    if (dc < 0) {
        split(s, left);
        if (left.size() != 8) return false;
        for (int i = 0; i < 8; i++) {
            char *end = nullptr;
            unsigned long v = strtoul(left[i].c_str(), &end, 16);
            if (!left[i].length() || (end && *end) || v > 0xffff) return false;
            out.w[i] = (uint16_t)v;
        }
        return true;
    }

    String L = s.substring(0, dc);
    String R = s.substring(dc + 2);
    split(L, left);
    split(R, right);
    if (left.size() + right.size() > 8) return false;
    int fill = 8 - (int)left.size() - (int)right.size();
    int idx = 0;
    for (auto &p : left) {
        char *end = nullptr;
        unsigned long v = strtoul(p.c_str(), &end, 16);
        if (!p.length() || (end && *end) || v > 0xffff) return false;
        out.w[idx++] = (uint16_t)v;
    }
    for (int i = 0; i < fill; i++) out.w[idx++] = 0;
    for (auto &p : right) {
        char *end = nullptr;
        unsigned long v = strtoul(p.c_str(), &end, 16);
        if (!p.length() || (end && *end) || v > 0xffff) return false;
        out.w[idx++] = (uint16_t)v;
    }
    return idx == 8;
}

static String expandV6(const V6 &a) {
    char buf[48];
    snprintf(
        buf, sizeof(buf), "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", a.w[0], a.w[1], a.w[2], a.w[3], a.w[4],
        a.w[5], a.w[6], a.w[7]
    );
    return String(buf);
}

static String compressV6(const V6 &a) {
    // Find longest run of zeros
    int bestStart = -1, bestLen = 0;
    int run = 0, runStart = 0;
    for (int i = 0; i < 8; i++) {
        if (a.w[i] == 0) {
            if (run == 0) runStart = i;
            run++;
            if (run > bestLen) {
                bestLen = run;
                bestStart = runStart;
            }
        } else {
            run = 0;
        }
    }
    if (bestLen < 2) bestStart = -1;

    String out;
    for (int i = 0; i < 8;) {
        if (i == bestStart) {
            out += (i == 0) ? "::" : ":";
            i += bestLen;
            if (i >= 8) break;
            continue;
        }
        if (out.length() && !out.endsWith(":")) out += ':';
        char buf[8];
        snprintf(buf, sizeof(buf), "%x", a.w[i]);
        out += buf;
        i++;
    }
    if (out.endsWith(":") && !out.endsWith("::") && bestStart + bestLen == 8) out += ':';
    return out;
}

static V6 networkV6(const V6 &a, int prefix) {
    V6 n = a;
    if (prefix < 0) prefix = 0;
    if (prefix > 128) prefix = 128;
    for (int i = 0; i < 8; i++) {
        int bitStart = i * 16;
        if (prefix <= bitStart) {
            n.w[i] = 0;
        } else if (prefix >= bitStart + 16) {
            // keep
        } else {
            int keep = prefix - bitStart;
            uint16_t mask = (uint16_t)(0xFFFFu << (16 - keep));
            n.w[i] &= mask;
        }
    }
    return n;
}

} // namespace

void calcSubnet() {
    CalcSession s;
    s.title = "Subnet";
    s.modeNames = {"IPv4+pfx", "IPv4+mask", "Split N", "Split hosts", "IPv6"};
    s.mode = 0;

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.pager = 0;
        cs.pagerCount = 1;
        switch (cs.mode) {
            case 0:
                cs.fields.push_back({"address", "192.168.1.10", CE_TEXT, {}, 20});
                cs.fields.push_back({"prefix", "24", CE_NUM, {}, 3});
                break;
            case 1:
                cs.fields.push_back({"address", "192.168.1.10", CE_TEXT, {}, 20});
                cs.fields.push_back({"mask", "255.255.255.0", CE_TEXT, {}, 20});
                break;
            case 2:
                cs.fields.push_back({"network", "192.168.0.0", CE_TEXT, {}, 20});
                cs.fields.push_back({"prefix", "24", CE_NUM, {}, 3});
                cs.fields.push_back({"N subnets", "4", CE_NUM, {}, 6});
                break;
            case 3:
                cs.fields.push_back({"network", "192.168.0.0", CE_TEXT, {}, 20});
                cs.fields.push_back({"prefix", "24", CE_NUM, {}, 3});
                cs.fields.push_back({"hosts/net", "50", CE_NUM, {}, 8});
                break;
            case 4:
                cs.fields.push_back({"address", "2001:db8::1", CE_TEXT, {}, 48});
                cs.fields.push_back({"prefix", "64", CE_NUM, {}, 4});
                break;
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        if (cs.mode == 4) {
            V6 a;
            if (!parseIpv6(cs.fields[0].value, a)) {
                calcSetError(cs, "bad IPv6 address");
                return;
            }
            long pfx;
            if (!calcParseIntRange(cs.fields[1].value, pfx, 0, 128)) {
                calcSetError(cs, "prefix must be 0..128");
                return;
            }
            V6 net = networkV6(a, (int)pfx);
            cs.results.push_back({"expanded", expandV6(a), false});
            cs.results.push_back({"compressed", compressV6(a), false});
            cs.results.push_back({"network", compressV6(net) + "/" + String((int)pfx), true});
            return;
        }

        if (cs.mode == 0 || cs.mode == 1) {
            uint32_t ip;
            String ipErr;
            if (!parseIpv4(cs.fields[0].value, ip, &ipErr)) {
                calcSetError(cs, ipErr.c_str());
                return;
            }
            int pfx;
            if (cs.mode == 0) {
                long pd;
                if (!calcParseIntRange(cs.fields[1].value, pd, 0, 32)) {
                    calcSetError(cs, "prefix must be 0..32");
                    return;
                }
                pfx = (int)pd;
            } else {
                uint32_t mask;
                String maskErr;
                if (!parseIpv4(cs.fields[1].value, mask, &maskErr)) {
                    calcSetError(cs, ("mask: " + maskErr).c_str());
                    return;
                }
                pfx = maskToPrefix(mask);
                if (pfx < 0) {
                    calcSetError(cs, "mask not contiguous");
                    return;
                }
            }
            pushIpv4Results(cs, ip, pfx);
            return;
        }

        // Split modes
        uint32_t netIp;
        String netErr;
        if (!parseIpv4(cs.fields[0].value, netIp, &netErr)) {
            calcSetError(cs, netErr.c_str());
            return;
        }
        long basePfxL;
        if (!calcParseIntRange(cs.fields[1].value, basePfxL, 0, 32)) {
            calcSetError(cs, "prefix must be 0..32");
            return;
        }
        int basePfx = (int)basePfxL;
        uint32_t baseMask = prefixToMask(basePfx);
        netIp &= baseMask;

        int newPfx = basePfx;
        int count = 1;
        if (cs.mode == 2) {
            long n;
            if (!calcParseIntRange(cs.fields[2].value, n, 1, 1L << 20)) {
                calcSetError(cs, "N must be integer >= 1");
                return;
            }
            int bits = 0;
            long need = 1;
            while (need < n) {
                bits++;
                need <<= 1;
                if (basePfx + bits > 32) {
                    calcSetError(cs, "too many subnets");
                    return;
                }
            }
            newPfx = basePfx + bits;
            count = 1 << bits;
        } else {
            long hosts;
            if (!calcParseIntRange(cs.fields[2].value, hosts, 1, (1L << 30))) {
                calcSetError(cs, "hosts must be integer >= 1");
                return;
            }
            uint64_t needHosts = (uint64_t)hosts + 2; // network+broadcast
            int hostBits = 0;
            uint64_t cap = 1;
            while (cap < needHosts) {
                hostBits++;
                cap <<= 1;
                if (hostBits > 32) break;
            }
            newPfx = 32 - hostBits;
            if (newPfx < basePfx) {
                calcSetError(cs, "hosts too large for prefix");
                return;
            }
            int bits = newPfx - basePfx;
            count = 1 << bits;
        }

        cs.pagerCount = count;
        if (cs.pager < 0) cs.pager = 0;
        if (cs.pager >= count) cs.pager = count - 1;

        uint32_t step = (newPfx >= 32) ? 1u : (1u << (32 - newPfx));
        uint32_t sub = netIp + (uint32_t)cs.pager * step;
        pushIpv4Results(cs, sub, newPfx);
        cs.results.insert(
            cs.results.begin(),
            {"subnet", String(cs.pager + 1) + " of " + String(count) + " /" + String(newPfx), false}
        );
    };

    runCalcSession(s);
}

void calcBitmask() {
    CalcSession s;
    s.title = "Bitmask";
    s.modeNames = {"bits list", "hex mask"};
    s.mode = 0;

    static const std::vector<String> widths = {"8", "16", "32", "64"};

    s.onMode = [](CalcSession &cs) {
        cs.fields.clear();
        cs.fields.push_back({"width", "32", CE_CHOICE, widths, 4});
        if (cs.mode == 0) {
            cs.fields.push_back({"set bits", "0,3,7", CE_TEXT, {}, 64});
        } else {
            cs.fields.push_back({"hex mask", "0x89", CE_HEX, {}, 20});
        }
    };

    s.recompute = [](CalcSession &cs) {
        cs.status = "";
        cs.results.clear();
        long widthL;
        if (!calcParseInt(cs.fields[0].value, widthL) ||
            (widthL != 8 && widthL != 16 && widthL != 32 && widthL != 64)) {
            calcSetError(cs, "width must be 8/16/32/64");
            return;
        }
        int width = (int)widthL;
        uint64_t mask = 0;
        uint64_t widthMask = (width == 64) ? ~0ull : ((1ull << width) - 1);

        if (cs.mode == 0) {
            String list = calcTrim(cs.fields[1].value);
            if (!list.length()) {
                calcSetError(cs, "enter set bits e.g. 0,3,7");
                return;
            }
            int start = 0;
            bool any = false;
            while (start <= (int)list.length()) {
                int c = list.indexOf(',', start);
                String tok = (c < 0) ? list.substring(start) : list.substring(start, c);
                tok.trim();
                if (tok.length()) {
                    long bd;
                    if (!calcParseIntRange(tok, bd, 0, width - 1)) {
                        calcSetError(cs, "bit must be integer in range");
                        return;
                    }
                    mask |= (1ull << (int)bd);
                    any = true;
                }
                if (c < 0) break;
                start = c + 1;
            }
            if (!any) {
                calcSetError(cs, "enter set bits e.g. 0,3,7");
                return;
            }
        } else {
            String hx = calcTrim(cs.fields[1].value);
            if (hx.startsWith("0x") || hx.startsWith("0X")) hx = hx.substring(2);
            if (!hx.length()) {
                calcSetError(cs, "enter hex mask");
                return;
            }
            uint64_t v;
            if (!calcParseU64(hx, 16, v)) {
                calcSetError(cs, "bad hex mask");
                return;
            }
            if (width < 64 && v > widthMask) {
                calcSetError(cs, "hex wider than width");
                return;
            }
            mask = v & widthMask;
        }
        mask &= widthMask;

        cs.results.push_back({"mask", "0x" + calcGroupHex(mask, width / 4), true});
        cs.results.push_back({"binary", calcGroupBin(mask, width), false});
        cs.results.push_back({"inverted", "0x" + calcGroupHex((~mask) & widthMask, width / 4), false});

        for (int b = 0; b < width; b++) {
            if (mask & (1ull << b)) {
                cs.results.push_back(
                    {"bit " + String(b), "0x" + calcGroupHex(1ull << b, width / 4), false}
                );
            }
        }
    };

    runCalcSession(s);
}

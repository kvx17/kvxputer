#include "rf_decode.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace platform {
namespace {

struct Proto {
    const char* name;
    int te;
    int sync_a, sync_b;
    int zero_a, zero_b;
    int one_a, one_b;
    int bits;
    bool inv;
    bool rolling;
};

// Timings derived from the classic OOK factor model (rc-switch / registry).
constexpr Proto kProtos[] = {
    {"Princeton", 350, 1, 31, 1, 3, 3, 1, 24, false, false},
    {"NICE_FLO", 700, 1, 36, 2, 1, 1, 2, 12, false, false},
    {"CAME", 320, 36, 1, 2, 1, 1, 2, 12, true, false},
    {"Linear", 500, 3, 42, 1, 3, 3, 1, 10, false, false},
    {"KeeLoq", 400, 1, 10, 1, 2, 2, 1, 64, false, true}, // shape hint only
};

bool near(int a, int b, int tol_pct) {
    if (b == 0) return a == 0;
    const int d = std::abs(a - b);
    return d * 100 <= std::abs(b) * tol_pct;
}

bool match_pair(int high, int low, int fa, int fb, int te, int tol) {
    return near(high, fa * te, tol) && near(low, fb * te, tol);
}

RfDecodeResult try_proto(const Proto& p, const std::vector<int>& t) {
    RfDecodeResult r;
    r.protocol = p.name;
    r.te_us = p.te;
    r.bits = p.bits;
    r.rolling = p.rolling;
    if (t.size() < 4) return r;

    const int tol = 40;
    // Find a sync-like gap: long space near sync_b * te.
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i + 1 < t.size(); ++i) {
        const int a = std::abs(t[i]);
        const int b = std::abs(t[i + 1]);
        if (p.inv) {
            if (near(a, p.sync_a * p.te, tol) && near(b, p.sync_b * p.te, tol)) {
                start = i + 2;
                found = true;
                break;
            }
        } else {
            if (near(a, p.sync_a * p.te, tol) && near(b, p.sync_b * p.te, tol)) {
                start = i + 2;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        // Fallback: first long gap.
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] < 0 && std::abs(t[i]) > p.te * 10) {
                start = i + 1;
                found = true;
                break;
            }
        }
    }
    if (!found || start >= t.size()) return r;

    uint64_t key = 0;
    int bits = 0;
    for (size_t i = start; i + 1 < t.size() && bits < p.bits; i += 2) {
        int high = 0, low = 0;
        if (p.inv) {
            // space-coded: low then high
            low = std::abs(t[i]);
            high = std::abs(t[i + 1]);
        } else {
            high = std::abs(t[i]);
            low = std::abs(t[i + 1]);
        }
        // Inter-frame gap ends the word.
        if (low > p.te * 20 || high > p.te * 20) break;
        int bit = -1;
        if (match_pair(high, low, p.zero_a, p.zero_b, p.te, tol)) bit = 0;
        else if (match_pair(high, low, p.one_a, p.one_b, p.te, tol)) bit = 1;
        if (bit < 0) {
            // Allow swapped polarity for tolerance
            if (match_pair(low, high, p.zero_a, p.zero_b, p.te, tol)) bit = 0;
            else if (match_pair(low, high, p.one_a, p.one_b, p.te, tol)) bit = 1;
        }
        if (bit < 0) break;
        key = (key << 1) | static_cast<uint64_t>(bit);
        ++bits;
    }
    if (bits < (p.bits > 0 ? p.bits : 8) && bits < 8) return r;
    if (p.bits > 0 && bits != p.bits && bits < p.bits - 1) return r;
    r.ok = true;
    r.key = key;
    r.bits = bits;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s key=0x%llX bits=%d te=%d%s", p.name,
                  static_cast<unsigned long long>(key), bits, p.te, p.rolling ? " (no replay)" : "");
    r.summary = buf;
    return r;
}

} // namespace

RfDecodeResult decode_ook_timings(const std::vector<int>& timings_us) {
    RfDecodeResult best;
    for (const auto& p : kProtos) {
        auto r = try_proto(p, timings_us);
        if (r.ok && (!best.ok || r.bits > best.bits)) best = r;
    }
    if (!best.ok) {
        best.summary = "no decode";
    }
    return best;
}

bool write_sub_protocol(const std::string& path, uint32_t freq_hz, const RfDecodeResult& d,
                        std::string& error) {
    if (!d.ok) {
        error = "no decode";
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: Flipper SubGhz Key File\n";
    out << "Version: 1\n";
    out << "Frequency: " << freq_hz << "\n";
    out << "Preset: FuriHalSubGhzPresetOok650Async\n";
    out << "Protocol: " << d.protocol << "\n";
    out << "Bit: " << d.bits << "\n";
    char key[32];
    std::snprintf(key, sizeof(key), "%0*llX", (d.bits + 3) / 4, static_cast<unsigned long long>(d.key));
    out << "Key: " << key << "\n";
    out << "TE: " << d.te_us << "\n";
    if (d.rolling) out << "# rolling-code: replay disabled\n";
    return true;
}

} // namespace platform

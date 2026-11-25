#include "rf_decode.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace platform {
namespace {

struct Proto {
    const char* name;
    int te;
    int sync_a, sync_b; // mark, space factors (or space, mark when inv)
    int zero_a, zero_b;
    int one_a, one_b;
    int bits;
    bool inv; // CAME-style: long space then short mark for sync; bit = space then mark
    bool rolling;
};

// Timings: factor model (rc-switch / Flipper SubGhz).
constexpr Proto kProtos[] = {
    {"Princeton", 350, 1, 31, 1, 3, 3, 1, 24, false, false},
    {"EV1527", 350, 1, 31, 1, 3, 3, 1, 24, false, false},
    {"PT2262", 350, 1, 31, 1, 3, 3, 1, 24, false, false},
    {"NICE_FLO", 700, 1, 36, 1, 2, 2, 1, 12, false, false},
    {"CAME", 320, 1, 36, 1, 2, 2, 1, 12, true, false},
    {"Holtek", 200, 1, 36, 1, 2, 2, 1, 12, false, false},
    {"Linear", 500, 3, 42, 1, 3, 3, 1, 10, false, false},
    {"KeeLoq", 400, 1, 10, 1, 2, 2, 1, 66, false, true},
};

bool near(int a, int b, int tol_pct) {
    if (b == 0) return a == 0;
    const int d = std::abs(a - b);
    return d * 100 <= std::abs(b) * tol_pct;
}

bool match_pair(int high, int low, int fa, int fb, int te, int tol) {
    return near(high, fa * te, tol) && near(low, fb * te, tol);
}

struct FrameHit {
    uint64_t key{0};
    int bits{0};
    int te{0};
};

bool try_decode_frame(const Proto& p, const std::vector<int>& t, size_t start, FrameHit& out) {
    uint64_t key = 0;
    int bits = 0;
    const int tol = 35;
    for (size_t i = start; i + 1 < t.size() && bits < p.bits; i += 2) {
        int a = std::abs(t[i]);
        int b = std::abs(t[i + 1]);
        if (a > p.te * 20 || b > p.te * 20) break;
        int high = 0, low = 0;
        if (p.inv) {
            // space then mark
            low = a;
            high = b;
        } else {
            high = a;
            low = b;
        }
        int bit = -1;
        if (match_pair(high, low, p.zero_a, p.zero_b, p.te, tol)) bit = 0;
        else if (match_pair(high, low, p.one_a, p.one_b, p.te, tol)) bit = 1;
        if (bit < 0) return false;
        key = (key << 1) | static_cast<uint64_t>(bit);
        ++bits;
    }
    if (p.bits > 0 && bits < p.bits - 1) return false;
    if (bits < 8) return false;
    out.key = key;
    out.bits = bits;
    out.te = p.te;
    return true;
}

std::vector<FrameHit> find_frames(const Proto& p, const std::vector<int>& t) {
    std::vector<FrameHit> hits;
    const int tol = 40;
    for (size_t i = 0; i + 1 < t.size(); ++i) {
        const int a = std::abs(t[i]);
        const int b = std::abs(t[i + 1]);
        bool sync = false;
        size_t start = 0;
        if (p.inv) {
            // CAME: short mark (optional) then long space — accept long space as sync gap.
            if (t[i] < 0 && near(a, p.sync_b * p.te, tol)) {
                start = i + 1;
                sync = true;
            } else if (near(a, p.sync_a * p.te, tol) && near(b, p.sync_b * p.te, tol)) {
                start = i + 2;
                sync = true;
            }
        } else {
            if (near(a, p.sync_a * p.te, tol) && near(b, p.sync_b * p.te, tol)) {
                start = i + 2;
                sync = true;
            }
        }
        if (!sync) continue;
        FrameHit hit;
        if (try_decode_frame(p, t, start, hit)) hits.push_back(hit);
    }
    return hits;
}

RfDecodeResult try_proto(const Proto& p, const std::vector<int>& t) {
    RfDecodeResult r;
    r.protocol = p.name;
    r.te_us = p.te;
    r.bits = p.bits;
    r.rolling = p.rolling;
    auto hits = find_frames(p, t);
    if (hits.empty()) return r;

    std::map<uint64_t, int> counts;
    FrameHit best = hits.front();
    for (const auto& h : hits) {
        counts[h.key]++;
        if (counts[h.key] > counts[best.key]) best = h;
    }
    const int reps = counts[best.key];
    // Rolling: report detect even on a single frame; fixed codes need consensus.
    if (!p.rolling && reps < 2) return r;
    if (p.rolling && reps < 1) return r;

    r.ok = true;
    r.key = best.key;
    r.bits = best.bits;
    r.te_us = best.te;
    r.repeats = reps;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s key=0x%llX bits=%d te=%d x%d%s", p.name,
                  static_cast<unsigned long long>(best.key), best.bits, best.te, reps,
                  p.rolling ? " (no replay)" : "");
    r.summary = buf;
    return r;
}

const Proto* find_proto(const std::string& name) {
    for (const auto& p : kProtos) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

} // namespace

RfDecodeResult decode_ook_timings(const std::vector<int>& timings_us) {
    RfDecodeResult best;
    for (const auto& p : kProtos) {
        auto r = try_proto(p, timings_us);
        if (!r.ok) continue;
        if (!best.ok || r.repeats > best.repeats || (r.repeats == best.repeats && r.bits > best.bits))
            best = r;
    }
    if (!best.ok) best.summary = "no decode";
    return best;
}

bool encode_ook_protocol(const std::string& protocol, uint64_t key, int bits, std::vector<int>& timings_us,
                         std::string& error) {
    timings_us.clear();
    const Proto* p = find_proto(protocol);
    if (!p || p->rolling) {
        error = "unsupported protocol";
        return false;
    }
    if (bits <= 0) bits = p->bits;
    if (bits > 64) {
        error = "bits too large";
        return false;
    }
    const int te = p->te;
    // Three repeats of: sync + data bits
    for (int rep = 0; rep < 4; ++rep) {
        if (p->inv) {
            timings_us.push_back(p->sync_a * te);
            timings_us.push_back(-(p->sync_b * te));
        } else {
            timings_us.push_back(p->sync_a * te);
            timings_us.push_back(-(p->sync_b * te));
        }
        for (int i = bits - 1; i >= 0; --i) {
            const int bit = static_cast<int>((key >> i) & 1ULL);
            const int fa = bit ? p->one_a : p->zero_a;
            const int fb = bit ? p->one_b : p->zero_b;
            if (p->inv) {
                timings_us.push_back(-(fa * te));
                timings_us.push_back(fb * te);
            } else {
                timings_us.push_back(fa * te);
                timings_us.push_back(-(fb * te));
            }
        }
    }
    return true;
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
    if (d.repeats > 0) out << "# repeats: " << d.repeats << "\n";
    return true;
}

} // namespace platform

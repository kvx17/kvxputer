#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct RfDecodeResult {
    bool ok{false};
    bool rolling{false}; // decode-only; replay disabled
    std::string protocol;
    uint64_t key{0};
    int bits{0};
    int te_us{0};
    int repeats{0};
    std::string summary;
};

// Decode fixed-code OOK protocols (Princeton/EV1527/PT2262, CAME, Nice FLO, Holtek, Linear)
// from Flipper-style signed mark(+)/space(-) timings in microseconds.
// Requires the same key seen ≥2 times (repeat-frame consensus) before ok=true.
RfDecodeResult decode_ook_timings(const std::vector<int>& timings_us);

// Encode a fixed-code key into mark/space timings for TX.
bool encode_ook_protocol(const std::string& protocol, uint64_t key, int bits, std::vector<int>& timings_us,
                         std::string& error);

// Write a Flipper protocol .sub (not RAW) from a successful decode.
bool write_sub_protocol(const std::string& path, uint32_t freq_hz, const RfDecodeResult& d,
                        std::string& error);

} // namespace platform

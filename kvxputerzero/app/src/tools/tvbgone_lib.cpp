#include "tvbgone_lib.h"

#include "tvbgone_world_ir_codes.h"

#include <ctime>
#include <cstring>

namespace tools {
namespace tvbgone {
namespace {

uint8_t read_bits(const IrCode* code, uint8_t& bitsleft, uint8_t& bits, uint8_t& code_ptr, uint8_t count) {
    uint8_t tmp = 0;
    while (count--) {
        if (bitsleft == 0) {
            bits = code->codes[code_ptr++];
            bitsleft = 8;
        }
        tmp = static_cast<uint8_t>((tmp << 1) | ((bits >> --bitsleft) & 1));
    }
    return tmp;
}

} // namespace

bool expand_parsed(const void* ir_code_ptr, PulseCode& out) {
    out = {};
    if (!ir_code_ptr) return false;
    const auto* code = static_cast<const IrCode*>(ir_code_ptr);
    if (!code->times || !code->codes || code->numpairs == 0) return false;
    out.carrier_hz = static_cast<uint32_t>(code->timer_val) * 1000u;
    if (out.carrier_hz < 20000 || out.carrier_hz > 60000) out.carrier_hz = 38000;
    out.pulses_us.reserve(static_cast<size_t>(code->numpairs) * 2u);
    uint8_t bitsleft = 0;
    uint8_t bits = 0;
    uint8_t code_ptr = 0;
    for (uint8_t k = 0; k < code->numpairs; ++k) {
        const uint16_t ti = static_cast<uint16_t>(read_bits(code, bitsleft, bits, code_ptr, code->bitcompression)) * 2u;
        out.pulses_us.push_back(static_cast<uint32_t>(code->times[ti]) * 10u);
        out.pulses_us.push_back(static_cast<uint32_t>(code->times[ti + 1]) * 10u);
    }
    return !out.pulses_us.empty();
}

bool expand_raw(const void* raw_code_ptr, PulseCode& out) {
    out = {};
    if (!raw_code_ptr) return false;
    const auto* code = static_cast<const RawIrCode*>(raw_code_ptr);
    if (!code->times || code->numpairs == 0) return false;
    out.carrier_hz = static_cast<uint32_t>(code->timer_val) * 1000u;
    if (out.carrier_hz < 20000 || out.carrier_hz > 60000) out.carrier_hz = 38000;
    out.pulses_us.reserve(static_cast<size_t>(code->numpairs) * 2u);
    for (uint8_t k = 0; k < code->numpairs; ++k) {
        out.pulses_us.push_back(code->times[k * 2]);
        out.pulses_us.push_back(code->times[k * 2 + 1]);
    }
    return !out.pulses_us.empty();
}

void run_blast(Region region, const CancelFn& cancel, const ProgressFn& progress, const SendFn& send,
               std::string& error) {
    error.clear();
    const IrCode* const* region_codes = region == Region::NA ? NApowerCodes : EUpowerCodes;
    const uint8_t region_count = region == Region::NA ? num_NAcodes : num_EUcodes;

    auto send_parsed_batch = [&](const IrCode* const* codes, uint8_t count, const char* label) -> bool {
        for (uint8_t i = 0; i < count; ++i) {
            if (cancel && cancel()) return false;
            if (progress) progress(i + 1, count, label);
            PulseCode pc;
            if (!expand_parsed(codes[i], pc)) continue;
            std::string err;
            if (!send(pc, err)) {
                error = err.empty() ? "IR send failed" : err;
                return false;
            }
            // Inter-code gap ~205 ms (firmware delay_ten_us(20500)).
            for (int g = 0; g < 41 && !(cancel && cancel()); ++g) {
                // 5 ms steps
                timespec ts{0, 5 * 1000 * 1000};
                nanosleep(&ts, nullptr);
            }
            if (cancel && cancel()) return false;
        }
        return true;
    };

    auto send_raw_batch = [&](const RawIrCode* const* codes, uint8_t count, const char* label) -> bool {
        for (uint8_t i = 0; i < count; ++i) {
            if (cancel && cancel()) return false;
            if (progress) progress(i + 1, count, label);
            PulseCode pc;
            if (!expand_raw(codes[i], pc)) continue;
            std::string err;
            if (!send(pc, err)) {
                error = err.empty() ? "IR send failed" : err;
                return false;
            }
            for (int g = 0; g < 41 && !(cancel && cancel()); ++g) {
                timespec ts{0, 5 * 1000 * 1000};
                nanosleep(&ts, nullptr);
            }
            if (cancel && cancel()) return false;
        }
        return true;
    };

    if (!send_parsed_batch(region_codes, region_count, region == Region::NA ? "NA" : "EU")) return;
    if (cancel && cancel()) return;
    if (!send_parsed_batch(UniversalParsedCodes, num_UniversalParsedCodes, "universal")) return;
    if (cancel && cancel()) return;
    send_raw_batch(UniversalRawCodes, num_UniversalRawCodes, "universal-raw");
}

} // namespace tvbgone
} // namespace tools

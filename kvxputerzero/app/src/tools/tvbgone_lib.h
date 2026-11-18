#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tools {
namespace tvbgone {

enum class Region { NA, EU };

struct PulseCode {
    uint32_t carrier_hz{38000};
    std::vector<uint32_t> pulses_us; // mark/space alternating
};

// Expand one compressed IrCode into mark/space microseconds. Returns false on bad table entry.
bool expand_parsed(const void* ir_code, PulseCode& out);
bool expand_raw(const void* raw_code, PulseCode& out);

using CancelFn = std::function<bool()>;
using ProgressFn = std::function<void(size_t index, size_t total, const std::string& label)>;
using SendFn = std::function<bool(const PulseCode& code, std::string& error)>;

// Walk region codes then universal tables. Stops when cancel returns true or send fails.
void run_blast(Region region, const CancelFn& cancel, const ProgressFn& progress, const SendFn& send,
               std::string& error);

} // namespace tvbgone
} // namespace tools

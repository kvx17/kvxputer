#pragma once

#include <cstdint>
#include <string>

namespace tools::combo {

struct Solution {
    uint8_t first_pin = 0;
    uint8_t second_count = 0;
    uint8_t third_count = 0;
    uint8_t second_pins[10] = {};
    uint8_t third_pins[10] = {};
};

// first_lock / second_lock: 0..39. resistance: 0.0 .. 39.5 in 0.5 steps → index.
void calculate(uint8_t first_lock, uint8_t second_lock, uint8_t resistance_index, Solution& out);
uint8_t resistance_index_from_label(double ohms, bool& ok);
std::string resistance_label(uint8_t index);
// Input: "first second resistance" e.g. "12 34 7.5"
std::string solve_line(const std::string& input, bool& ok);
const char* input_hint();

} // namespace tools::combo

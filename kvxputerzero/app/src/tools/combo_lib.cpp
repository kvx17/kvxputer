#include "combo_lib.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace tools::combo {
namespace {

constexpr uint8_t kLockCount = 40;
constexpr uint8_t kResistanceCount = 80;

} // namespace

void calculate(uint8_t first_lock, uint8_t second_lock, uint8_t resistance_index, Solution& out) {
    std::memset(&out, 0, sizeof(out));

    unsigned int pin0 = resistance_index / 2u;
    if (resistance_index % 2u != 0u) pin0 += 6;
    else pin0 += 5;
    pin0 %= kLockCount;
    out.first_pin = static_cast<uint8_t>(pin0);

    uint8_t remainder = out.first_pin % 4;
    uint8_t a = first_lock;
    uint8_t b = second_lock;
    for (uint8_t i = 0; i < 4u; ++i) {
        if ((a % 4u) == remainder) out.third_pins[out.third_count++] = a;
        if ((b % 4u) == remainder) out.third_pins[out.third_count++] = b;
        a = static_cast<uint8_t>((a + 10u) % 40u);
        b = static_cast<uint8_t>((b + 10u) % 40u);
    }

    uint8_t row_1 = static_cast<uint8_t>((remainder + 2) % kLockCount);
    uint8_t row_2 = static_cast<uint8_t>((row_1 + 4) % kLockCount);
    out.second_pins[out.second_count++] = row_1;
    out.second_pins[out.second_count++] = row_2;
    for (uint8_t i = 0; i < 4u; ++i) {
        row_1 = static_cast<uint8_t>((row_1 + 8u) % kLockCount);
        row_2 = static_cast<uint8_t>((row_2 + 8u) % kLockCount);
        out.second_pins[out.second_count++] = row_1;
        out.second_pins[out.second_count++] = row_2;
    }

    for (uint8_t i = 0; i + 1 < out.second_count; ++i) {
        for (uint8_t j = i + 1; j < out.second_count; ++j) {
            if (out.second_pins[i] > out.second_pins[j]) {
                uint8_t t = out.second_pins[i];
                out.second_pins[i] = out.second_pins[j];
                out.second_pins[j] = t;
            }
        }
    }
}

uint8_t resistance_index_from_label(double ohms, bool& ok) {
    ok = false;
    if (ohms < 0.0 || ohms > 39.5) return 0;
    int idx = static_cast<int>(ohms * 2.0 + 0.25);
    if (idx < 0 || idx >= kResistanceCount) return 0;
    ok = true;
    return static_cast<uint8_t>(idx);
}

std::string resistance_label(uint8_t index) {
    if (index >= kResistanceCount) index = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d.%d", index / 2, (index % 2) ? 5 : 0);
    return buf;
}

const char* input_hint() {
    return "first second resist  e.g. 12 34 7.5";
}

std::string solve_line(const std::string& input, bool& ok) {
    ok = false;
    std::istringstream in(input);
    int first = -1, second = -1;
    double resist = -1.0;
    if (!(in >> first >> second >> resist)) return "need: first second resistance";
    if (first < 0 || first > 39 || second < 0 || second > 39) return "locks must be 0..39";
    bool r_ok = false;
    uint8_t ridx = resistance_index_from_label(resist, r_ok);
    if (!r_ok) return "resistance 0.0..39.5 step 0.5";

    Solution sol;
    calculate(static_cast<uint8_t>(first), static_cast<uint8_t>(second), ridx, sol);

    std::ostringstream out;
    out << "1st pin  " << static_cast<int>(sol.first_pin) << '\n';
    out << "2nd pins ";
    for (uint8_t i = 0; i < sol.second_count; ++i) {
        if (i) out << ',';
        out << static_cast<int>(sol.second_pins[i]);
    }
    out << "\n3rd pins ";
    for (uint8_t i = 0; i < sol.third_count; ++i) {
        if (i) out << ',';
        out << static_cast<int>(sol.third_pins[i]);
    }
    out << "\nR=" << resistance_label(ridx);
    ok = true;
    return out.str();
}

} // namespace tools::combo

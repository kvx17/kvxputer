#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct I2cHit {
    int bus{-1};
    uint8_t address{0};
};

std::vector<I2cHit> scan_all_i2c(std::string& error);

} // namespace platform

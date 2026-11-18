#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct PahubChannelHit {
    uint8_t channel{0};
    std::vector<uint8_t> addresses;
};

// Select TCA9548A channels 0..7 at hub_addr (default 0x70), scan each.
bool pahub_scan(int i2c_bus, uint8_t hub_addr, std::vector<PahubChannelHit>& hits, std::string& error);

} // namespace platform

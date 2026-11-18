#pragma once

#include <cstdint>

namespace platform {

bool read_brightness_percent(int32_t& percent);
bool write_brightness_percent(int32_t percent);

} // namespace platform

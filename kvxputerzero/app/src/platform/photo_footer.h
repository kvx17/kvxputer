#pragma once

#include <cstdint>
#include <vector>

namespace platform {

// Stamp capture details in the bottom-left of a full-resolution RGB888 photo.
void apply_photo_footer(std::vector<uint8_t>& rgb, int width, int height, const char* filter_name);

// Same stamp written into a packed I420 frame so the JPEG can keep the sensor samples.
void apply_photo_footer_i420(std::vector<uint8_t>& i420, int width, int height, int stride,
                             const char* filter_name);

} // namespace platform

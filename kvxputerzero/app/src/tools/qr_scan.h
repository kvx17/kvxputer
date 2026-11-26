#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::qr {

// Decode every QR code found in an 8-bit grayscale image (row-major, width*height
// bytes). Returns the decoded payload strings (empty if none). Backed by quirc.
std::vector<std::string> decode_gray(const uint8_t* gray, int width, int height);

} // namespace tools::qr

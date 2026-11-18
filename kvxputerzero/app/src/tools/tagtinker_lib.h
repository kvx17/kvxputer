#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tools {
namespace tagtinker {

constexpr size_t kMaxFrame = 64;
constexpr int kBarcodeLen = 17;

struct PulseBurst {
    uint32_t carrier_hz{1250000}; // ESL PP4 ~1.25 MHz carrier
    std::vector<uint32_t> pulses_us;
};

// Frame builders (byte payload before PP4 encoding).
size_t make_ping(uint8_t* buf, size_t cap, const uint8_t plid[4]);
size_t make_refresh(uint8_t* buf, size_t cap, const uint8_t plid[4]);
size_t make_led(uint8_t* buf, size_t cap, const uint8_t plid[4], uint8_t mode, uint16_t duration);
size_t make_broadcast_page(uint8_t* buf, size_t cap, uint8_t page, bool forever, uint16_t duration);

bool barcode_to_plid(const char* barcode, uint8_t plid[4]);

// Encode frame bytes as PP4 mark/space envelope for LIRC pulse send.
bool encode_pp4(const uint8_t* data, size_t len, PulseBurst& out, uint16_t repeats = 2);

} // namespace tagtinker
} // namespace tools

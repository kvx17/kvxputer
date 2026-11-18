#include "tagtinker_lib.h"

#include <cstring>

namespace tools {
namespace tagtinker {
namespace {

constexpr uint8_t kProtoDm = 0x85;

// Timings derived from firmware cycle counts at 240 MHz (TT_SCALE from 64 MHz Flipper units).
constexpr uint32_t kMarkUs = 40;
constexpr uint32_t kGapsUs[4] = {60, 242, 121, 181};

uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0x8408;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : static_cast<uint16_t>(crc >> 1);
    }
    return crc;
}

size_t terminate(uint8_t* buf, size_t len, size_t cap) {
    if (len + 2 > cap) return 0;
    const uint16_t crc = crc16(buf, len);
    buf[len] = static_cast<uint8_t>(crc & 0xFF);
    buf[len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return len + 2;
}

size_t raw_frame(uint8_t* buf, size_t cap, uint8_t proto, const uint8_t plid[4], uint8_t cmd) {
    if (cap < 6) return 0;
    buf[0] = proto;
    std::memcpy(&buf[1], plid, 4);
    buf[5] = cmd;
    return 6;
}

} // namespace

size_t make_ping(uint8_t* buf, size_t cap, const uint8_t plid[4]) {
    size_t p = raw_frame(buf, cap, kProtoDm, plid, 0x97);
    if (p == 0 || p + 24 > cap) return 0;
    buf[p++] = 0x01;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    for (int i = 0; i < 20; ++i) buf[p++] = 0x01;
    return terminate(buf, p, cap);
}

size_t make_refresh(uint8_t* buf, size_t cap, const uint8_t plid[4]) {
    size_t p = raw_frame(buf, cap, kProtoDm, plid, 0x34);
    if (p == 0 || p + 22 > cap) return 0;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x01;
    for (int i = 0; i < 18; ++i) buf[p++] = 0x00;
    return terminate(buf, p, cap);
}

size_t make_led(uint8_t* buf, size_t cap, const uint8_t plid[4], uint8_t mode, uint16_t duration) {
    size_t p = raw_frame(buf, cap, kProtoDm, plid, 0x06);
    if (p == 0 || p + 5 > cap) return 0;
    buf[p++] = mode;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = static_cast<uint8_t>((duration >> 8) & 0xFF);
    buf[p++] = static_cast<uint8_t>(duration & 0xFF);
    return terminate(buf, p, cap);
}

size_t make_broadcast_page(uint8_t* buf, size_t cap, uint8_t page, bool forever, uint16_t duration) {
    const uint8_t plid[4] = {0, 0, 0, 0};
    size_t p = raw_frame(buf, cap, kProtoDm, plid, 0x06);
    if (p == 0 || p + 5 > cap) return 0;
    buf[p++] = static_cast<uint8_t>((((page + 1) & 7) << 3) | 0x01 | (forever ? 0x80 : 0x00));
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = static_cast<uint8_t>((duration >> 8) & 0xFF);
    buf[p++] = static_cast<uint8_t>(duration & 0xFF);
    return terminate(buf, p, cap);
}

bool barcode_to_plid(const char* barcode, uint8_t plid[4]) {
    if (!barcode || !plid) return false;
    if (std::strlen(barcode) != static_cast<size_t>(kBarcodeLen)) return false;
    uint64_t a = 0;
    uint64_t b = 0;
    for (int i = 2; i < 7; ++i) {
        if (barcode[i] < '0' || barcode[i] > '9') return false;
        a = a * 10 + static_cast<uint64_t>(barcode[i] - '0');
    }
    for (int i = 7; i < 12; ++i) {
        if (barcode[i] < '0' || barcode[i] > '9') return false;
        b = b * 10 + static_cast<uint64_t>(barcode[i] - '0');
    }
    const uint64_t id = (a << 16) | b;
    plid[0] = static_cast<uint8_t>(id & 0xFF);
    plid[1] = static_cast<uint8_t>((id >> 8) & 0xFF);
    plid[2] = static_cast<uint8_t>((id >> 16) & 0xFF);
    plid[3] = static_cast<uint8_t>((id >> 24) & 0xFF);
    return true;
}

bool encode_pp4(const uint8_t* data, size_t len, PulseBurst& out, uint16_t repeats) {
    out = {};
    out.carrier_hz = 1250000;
    if (!data || len == 0 || len > 255) return false;
    out.pulses_us.reserve((len * 4 + 1) * (static_cast<size_t>(repeats) + 1) * 2);
    for (uint32_t rep = 0; rep <= repeats; ++rep) {
        for (size_t byte_idx = 0; byte_idx < len; ++byte_idx) {
            uint8_t current = data[byte_idx];
            for (int sym = 0; sym < 4; ++sym) {
                const uint8_t symbol = current & 0x03;
                current = static_cast<uint8_t>(current >> 2);
                out.pulses_us.push_back(kMarkUs);
                out.pulses_us.push_back(kGapsUs[symbol]);
            }
        }
        out.pulses_us.push_back(kMarkUs);
        out.pulses_us.push_back(2000); // inter-repeat gap
    }
    return !out.pulses_us.empty();
}

} // namespace tagtinker
} // namespace tools

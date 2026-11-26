#include "qr_scan.h"

extern "C" {
#include "quirc/quirc.h"
}

#include <cstring>

namespace tools::qr {

std::vector<std::string> decode_gray(const uint8_t* gray, int width, int height) {
    std::vector<std::string> out;
    if (!gray || width <= 0 || height <= 0) return out;

    struct quirc* q = quirc_new();
    if (!q) return out;
    if (quirc_resize(q, width, height) < 0) {
        quirc_destroy(q);
        return out;
    }

    int qw = 0, qh = 0;
    uint8_t* buf = quirc_begin(q, &qw, &qh);
    if (buf && qw == width && qh == height) {
        std::memcpy(buf, gray, static_cast<size_t>(width) * static_cast<size_t>(height));
    }
    quirc_end(q);

    const int count = quirc_count(q);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            out.emplace_back(reinterpret_cast<const char*>(data.payload),
                             static_cast<size_t>(data.payload_len));
        }
    }

    quirc_destroy(q);
    return out;
}

} // namespace tools::qr

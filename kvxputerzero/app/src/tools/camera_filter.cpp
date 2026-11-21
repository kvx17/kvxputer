#include "camera_filter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tools::camfilter {
namespace {

uint8_t clamp_u8(int value) {
    return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void unpack_rgb565(uint16_t pixel, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
    g = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
    b = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
}

int luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (77 * r + 150 * g + 29 * b) >> 8;
}

uint8_t quantize_comic_channel(int value) {
    if (value < 48) return 0;
    if (value < 112) return 85;
    if (value < 192) return 170;
    return 255;
}

uint8_t mix_channel(int from, int to, int amount, int range) {
    return clamp_u8(from + (to - from) * amount / range);
}

void thermal_color(int luma, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int segment = std::min(3, luma / 64);
    const int amount = luma - segment * 64;
    constexpr std::array<std::array<uint8_t, 3>, 5> palette{{
        {{4, 0, 18}},
        {{55, 0, 150}},
        {{245, 20, 35}},
        {{255, 220, 0}},
        {{255, 255, 245}},
    }};
    r = mix_channel(palette[segment][0], palette[segment + 1][0], amount, 64);
    g = mix_channel(palette[segment][1], palette[segment + 1][1], amount, 64);
    b = mix_channel(palette[segment][2], palette[segment + 1][2], amount, 64);
}

void apply_vivid(int sr, int sg, int sb, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int gray = luminance(sr, sg, sb);
    r = clamp_u8((gray + (sr - gray) * 135 / 100 - 128) * 118 / 100 + 128);
    g = clamp_u8((gray + (sg - gray) * 135 / 100 - 128) * 118 / 100 + 128);
    b = clamp_u8((gray + (sb - gray) * 135 / 100 - 128) * 118 / 100 + 128);
}

void apply_dramatic(int sr, int sg, int sb, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int gray = luminance(sr, sg, sb);
    const int contrasted = (gray - 128) * 145 / 100 + 128;
    r = clamp_u8(contrasted + (sr - gray) * 88 / 100);
    g = clamp_u8(contrasted + (sg - gray) * 88 / 100);
    b = clamp_u8(contrasted + (sb - gray) * 88 / 100);
}

void nearest_palette(uint8_t& r, uint8_t& g, uint8_t& b, const std::array<std::array<uint8_t, 3>, 4>& pal) {
    const int luma = luminance(r, g, b);
    const int idx = std::min(3, luma * 4 / 256);
    r = pal[idx][0];
    g = pal[idx][1];
    b = pal[idx][2];
}

constexpr std::array<std::array<uint8_t, 3>, 4> kGameBoy{{
    {{15, 56, 15}},
    {{48, 98, 48}},
    {{139, 172, 15}},
    {{155, 188, 15}},
}};
constexpr std::array<std::array<uint8_t, 3>, 4> kRetroGreen{{
    {{8, 24, 8}},
    {{24, 72, 24}},
    {{72, 160, 48}},
    {{180, 232, 120}},
}};
constexpr std::array<std::array<uint8_t, 3>, 4> kRetroAmber{{
    {{24, 8, 0}},
    {{80, 32, 0}},
    {{180, 100, 16}},
    {{255, 200, 80}},
}};
constexpr std::array<std::array<uint8_t, 3>, 4> kRetroIce{{
    {{8, 16, 32}},
    {{24, 48, 88}},
    {{72, 140, 200}},
    {{200, 236, 255}},
}};
constexpr std::array<std::array<uint8_t, 3>, 4> kRetroCandy{{
    {{40, 8, 40}},
    {{160, 40, 120}},
    {{255, 120, 180}},
    {{255, 220, 240}},
}};

struct HalftoneInk {
    uint8_t ir, ig, ib;
    uint8_t pr, pg, pb;
};

const HalftoneInk* halftone_ink(Filter filter) {
    switch (filter) {
    case Filter::Halftone: {
        static constexpr HalftoneInk ink{0, 0, 0, 255, 255, 255};
        return &ink;
    }
    case Filter::HalftoneGreen: {
        static constexpr HalftoneInk ink{0, 0, 0, 32, 220, 64};
        return &ink;
    }
    case Filter::HalftoneRed: {
        static constexpr HalftoneInk ink{0, 0, 0, 230, 32, 36};
        return &ink;
    }
    case Filter::HalftoneAmber: {
        static constexpr HalftoneInk ink{0, 0, 0, 255, 176, 24};
        return &ink;
    }
    case Filter::HalftoneBlue: {
        static constexpr HalftoneInk ink{0, 0, 0, 48, 120, 255};
        return &ink;
    }
    case Filter::HalftonePink: {
        static constexpr HalftoneInk ink{0, 0, 0, 255, 80, 160};
        return &ink;
    }
    case Filter::HalftonePurpleGreen: {
        static constexpr HalftoneInk ink{72, 16, 120, 48, 230, 90};
        return &ink;
    }
    case Filter::HalftoneRedBlue: {
        static constexpr HalftoneInk ink{180, 16, 32, 40, 90, 255};
        return &ink;
    }
    case Filter::HalftoneOrangeTeal: {
        static constexpr HalftoneInk ink{210, 80, 12, 16, 180, 170};
        return &ink;
    }
    default:
        return nullptr;
    }
}

void apply_halftone(int x, int y, int gray, const HalftoneInk& ink, uint8_t& r, uint8_t& g, uint8_t& b) {
    constexpr int bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    const int thresh = bayer[y & 3][x & 3] * 16;
    if (gray > thresh) {
        r = ink.pr;
        g = ink.pg;
        b = ink.pb;
    } else {
        r = ink.ir;
        g = ink.ig;
        b = ink.ib;
    }
}

void transform_pixel(Filter filter, int x, int y, int width, int height, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int sr = r;
    const int sg = g;
    const int sb = b;

    if (const HalftoneInk* ink = halftone_ink(filter)) {
        apply_halftone(x, y, luminance(r, g, b), *ink, r, g, b);
        return;
    }

    switch (filter) {
    case Filter::Polaroid: {
        const int grain = ((x * 17 + y * 31) & 7) - 3;
        r = clamp_u8((sr * 92 + 2550) / 100 + 8 + grain);
        g = clamp_u8((sg * 91 + 2295) / 100 + 4 + grain);
        b = clamp_u8((sb * 84 + 2040) / 100 + grain);
        const int dx = width > 1 ? std::abs(2 * x - width + 1) * 256 / width : 0;
        const int dy = height > 1 ? std::abs(2 * y - height + 1) * 256 / height : 0;
        const int vignette = std::max(205, 256 - (dx * dx + dy * dy) / 900);
        r = static_cast<uint8_t>(r * vignette / 256);
        g = static_cast<uint8_t>(g * vignette / 256);
        b = static_cast<uint8_t>(b * vignette / 256);
        break;
    }
    case Filter::Mono: {
        const uint8_t gray = static_cast<uint8_t>(luminance(r, g, b));
        r = g = b = gray;
        break;
    }
    case Filter::Vintage: {
        r = clamp_u8((393 * sr + 769 * sg + 189 * sb) / 1351 * 82 / 100 + 25);
        g = clamp_u8((349 * sr + 686 * sg + 168 * sb) / 1203 * 78 / 100 + 22);
        b = clamp_u8((272 * sr + 534 * sg + 131 * sb) / 1080 * 70 / 100 + 17);
        break;
    }
    case Filter::Vivid:
        apply_vivid(sr, sg, sb, r, g, b);
        break;
    case Filter::Dramatic:
        apply_dramatic(sr, sg, sb, r, g, b);
        break;
    case Filter::Silvertone: {
        const uint8_t gray = clamp_u8((luminance(r, g, b) - 128) * 125 / 100 + 136);
        r = g = b = gray;
        break;
    }
    case Filter::Comic: {
        const int gray = luminance(r, g, b);
        if (gray < 42) {
            r = 8;
            g = 8;
            b = 12;
            break;
        }
        r = quantize_comic_channel(gray + (sr - gray) * 165 / 100);
        g = quantize_comic_channel(gray + (sg - gray) * 165 / 100);
        b = quantize_comic_channel(gray + (sb - gray) * 165 / 100);
        break;
    }
    case Filter::Neon: {
        r = clamp_u8(((sr * 135 + sb * 55) / 100 - 24 - 128) * 125 / 100 + 128);
        g = clamp_u8(((sg * 120 + sb * 28) / 100 - 22 - 128) * 125 / 100 + 128);
        b = clamp_u8(((sb * 140 + sr * 45) / 100 + 18 - 128) * 125 / 100 + 128);
        break;
    }
    case Filter::Thermal:
        thermal_color(luminance(r, g, b), r, g, b);
        break;
    case Filter::Crt: {
        int crt_r = (sr - 128) * 110 / 100 + 128;
        int crt_g = (sg - 128) * 110 / 100 + 128;
        int crt_b = (sb - 128) * 110 / 100 + 128;
        if (x % 3 == 0) {
            crt_r = crt_r * 112 / 100;
            crt_g = crt_g * 90 / 100;
            crt_b = crt_b * 90 / 100;
        } else if (x % 3 == 1) {
            crt_r = crt_r * 90 / 100;
            crt_g = crt_g * 112 / 100;
            crt_b = crt_b * 90 / 100;
        } else {
            crt_r = crt_r * 90 / 100;
            crt_g = crt_g * 90 / 100;
            crt_b = crt_b * 112 / 100;
        }
        const int scanline = (y & 1) ? 70 : 100;
        r = clamp_u8(crt_r * scanline / 100);
        g = clamp_u8(crt_g * scanline / 100);
        b = clamp_u8(crt_b * scanline / 100);
        break;
    }
    case Filter::Noir: {
        const int grain = ((x * 13 + y * 29) & 15) - 7;
        const uint8_t gray = clamp_u8((luminance(r, g, b) - 128) * 160 / 100 + 128 + grain);
        r = g = b = gray;
        break;
    }
    case Filter::Solarize:
        r = clamp_u8(sr < 128 ? sr * 2 : (255 - sr) * 2);
        g = clamp_u8(sg < 128 ? sg * 2 : (255 - sg) * 2);
        b = clamp_u8(sb < 128 ? sb * 2 : (255 - sb) * 2);
        break;
    case Filter::GameBoy:
        nearest_palette(r, g, b, kGameBoy);
        break;
    case Filter::RetroGreen:
        nearest_palette(r, g, b, kRetroGreen);
        break;
    case Filter::RetroAmber:
        nearest_palette(r, g, b, kRetroAmber);
        break;
    case Filter::RetroIce:
        nearest_palette(r, g, b, kRetroIce);
        break;
    case Filter::RetroCandy:
        nearest_palette(r, g, b, kRetroCandy);
        break;
    case Filter::Blueprint: {
        const int gray = luminance(r, g, b);
        r = clamp_u8(20 + gray / 8);
        g = clamp_u8(40 + gray * 90 / 100);
        b = clamp_u8(80 + gray * 110 / 100);
        break;
    }
    case Filter::Duotone: {
        const int t = luminance(r, g, b);
        // Shadow indigo -> highlight peach
        r = mix_channel(24, 255, t, 255);
        g = mix_channel(16, 180, t, 255);
        b = mix_channel(64, 120, t, 255);
        break;
    }
    case Filter::Bloom: {
        r = clamp_u8(sr + (255 - sr) * 28 / 100);
        g = clamp_u8(sg + (255 - sg) * 28 / 100);
        b = clamp_u8(sb + (255 - sb) * 28 / 100);
        break;
    }
    case Filter::Vignette: {
        const int dx = width > 1 ? std::abs(2 * x - width + 1) * 256 / width : 0;
        const int dy = height > 1 ? std::abs(2 * y - height + 1) * 256 / height : 0;
        const int vignette = std::max(80, 256 - (dx * dx + dy * dy) / 700);
        r = static_cast<uint8_t>(sr * vignette / 256);
        g = static_cast<uint8_t>(sg * vignette / 256);
        b = static_cast<uint8_t>(sb * vignette / 256);
        break;
    }
    case Filter::Glitch: {
        const int shift = ((y / 3) & 1) ? 12 : -8;
        const int nx = std::clamp(x + shift, 0, width - 1);
        // Channel split approximation from position noise
        r = clamp_u8(sr + ((nx * 3) & 31) - 8);
        g = clamp_u8(sg);
        b = clamp_u8(sb + ((y * 5) & 31) - 8);
        if ((y % 17) == 0) {
            r = clamp_u8(r + 40);
            b = clamp_u8(b + 20);
        }
        break;
    }
    case Filter::HighKey: {
        r = clamp_u8(sr * 70 / 100 + 90);
        g = clamp_u8(sg * 70 / 100 + 90);
        b = clamp_u8(sb * 70 / 100 + 90);
        break;
    }
    case Filter::LowKey: {
        r = clamp_u8((sr - 40) * 85 / 100);
        g = clamp_u8((sg - 40) * 85 / 100);
        b = clamp_u8((sb - 40) * 85 / 100);
        break;
    }
    case Filter::Original:
    case Filter::Count:
        break;
    }
}

} // namespace

const char* filter_name(Filter filter) {
    switch (filter) {
    case Filter::Original: return "ORIGINAL";
    case Filter::Polaroid: return "POLAROID";
    case Filter::GameBoy: return "GAME BOY";
    case Filter::Mono: return "MONO";
    case Filter::Vintage: return "VINTAGE";
    case Filter::Vivid: return "VIVID";
    case Filter::Dramatic: return "DRAMATIC";
    case Filter::Silvertone: return "SILVERTONE";
    case Filter::Comic: return "COMIC";
    case Filter::Neon: return "NEON";
    case Filter::Thermal: return "THERMAL";
    case Filter::Crt: return "CRT";
    case Filter::Noir: return "NOIR";
    case Filter::Solarize: return "SOLARIZE";
    case Filter::RetroGreen: return "RETRO GREEN";
    case Filter::RetroAmber: return "RETRO AMBER";
    case Filter::RetroIce: return "RETRO ICE";
    case Filter::RetroCandy: return "RETRO CANDY";
    case Filter::Blueprint: return "BLUEPRINT";
    case Filter::Duotone: return "DUOTONE";
    case Filter::Bloom: return "BLOOM";
    case Filter::Halftone: return "HALFTONE";
    case Filter::HalftoneGreen: return "HT GREEN";
    case Filter::HalftoneRed: return "HT RED";
    case Filter::HalftoneAmber: return "HT AMBER";
    case Filter::HalftoneBlue: return "HT BLUE";
    case Filter::HalftonePink: return "HT PINK";
    case Filter::HalftonePurpleGreen: return "HT PUR/GRN";
    case Filter::HalftoneRedBlue: return "HT RED/BLU";
    case Filter::HalftoneOrangeTeal: return "HT ORG/TEA";
    case Filter::Vignette: return "VIGNETTE";
    case Filter::Glitch: return "GLITCH";
    case Filter::HighKey: return "HIGH KEY";
    case Filter::LowKey: return "LOW KEY";
    case Filter::Count: return "ORIGINAL";
    }
    return "ORIGINAL";
}

Filter step_filter(Filter filter, int direction) {
    const int count = static_cast<int>(Filter::Count);
    int value = static_cast<int>(filter);
    value = (value + (direction < 0 ? -1 : 1) + count) % count;
    return static_cast<Filter>(value);
}

void apply_gain_rgb565(std::vector<uint16_t>& pixels, float gain) {
    if (gain <= 0.01f || std::fabs(gain - 1.0f) < 0.01f) return;
    for (auto& px : pixels) {
        uint8_t r, g, b;
        unpack_rgb565(px, r, g, b);
        r = clamp_u8(static_cast<int>(r * gain + 0.5f));
        g = clamp_u8(static_cast<int>(g * gain + 0.5f));
        b = clamp_u8(static_cast<int>(b * gain + 0.5f));
        px = pack_rgb565(r, g, b);
    }
}

void apply_gain_rgb888(std::vector<uint8_t>& pixels, float gain) {
    if (gain <= 0.01f || std::fabs(gain - 1.0f) < 0.01f) return;
    for (auto& ch : pixels) {
        ch = clamp_u8(static_cast<int>(ch * gain + 0.5f));
    }
}

void apply_rgb565(std::vector<uint16_t>& pixels, int width, int height, Filter filter) {
    if (filter == Filter::Original || filter == Filter::Count || width <= 0 || height <= 0 ||
        pixels.size() < static_cast<size_t>(width * height)) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y * width + x);
            uint8_t r = 0, g = 0, b = 0;
            unpack_rgb565(pixels[index], r, g, b);
            transform_pixel(filter, x, y, width, height, r, g, b);
            pixels[index] = pack_rgb565(r, g, b);
        }
    }
}

void apply_rgb888(std::vector<uint8_t>& pixels, int width, int height, Filter filter) {
    if (filter == Filter::Original || filter == Filter::Count || width <= 0 || height <= 0 ||
        pixels.size() < static_cast<size_t>(width * height * 3)) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y * width + x) * 3;
            transform_pixel(filter, x, y, width, height, pixels[index], pixels[index + 1], pixels[index + 2]);
        }
    }
}

} // namespace tools::camfilter

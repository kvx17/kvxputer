// Creative camera filters for kvxputerzero.
// Filter math adapted from FXCam (imliubo/fxcam-cp0, MIT) with additional filters.

#pragma once

#include <cstdint>
#include <vector>

namespace tools::camfilter {

enum class Filter : uint8_t {
    Original = 0,
    Polaroid,
    GameBoy,
    Mono,
    Vintage,
    Vivid,
    Dramatic,
    Silvertone,
    Comic,
    Neon,
    Thermal,
    Crt,
    Noir,
    Solarize,
    // Small retro-palette sample (not all 88 FXCam variants)
    RetroGreen,
    RetroAmber,
    RetroIce,
    RetroCandy,
    // New vs FXCam named set
    Blueprint,
    Duotone,
    Bloom,
    Halftone,
    HalftoneGreen,
    HalftoneRed,
    HalftoneAmber,
    HalftoneBlue,
    HalftonePink,
    HalftonePurpleGreen,
    HalftoneRedBlue,
    HalftoneOrangeTeal,
    Vignette,
    Glitch,
    HighKey,
    LowKey,
    Count,
};

const char* filter_name(Filter filter);
Filter step_filter(Filter filter, int direction);

void apply_rgb565(std::vector<uint16_t>& pixels, int width, int height, Filter filter);
void apply_rgb888(std::vector<uint8_t>& pixels, int width, int height, Filter filter);

// Software brightness gain (1.0 = unchanged). Applied before filters.
void apply_gain_rgb565(std::vector<uint16_t>& pixels, float gain);
void apply_gain_rgb888(std::vector<uint8_t>& pixels, float gain);

} // namespace tools::camfilter

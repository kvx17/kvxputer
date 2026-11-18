#pragma once

#include "lvgl.h"

#include <cstdint>

namespace view {

// Full-screen green character rain with the KVXPUTER wordmark.
// Runs for kDurationMs. Callers exit early on a key press.
class MatrixRain {
public:
    static constexpr uint32_t kDurationMs = 5000;
    static constexpr uint32_t kFrameMs = 50;

    MatrixRain() = default;
    ~MatrixRain();

    MatrixRain(const MatrixRain&) = delete;
    MatrixRain& operator=(const MatrixRain&) = delete;

    // Parent should already be the active 320x170 screen.
    bool mount(lv_obj_t* parent);
    void step();
    bool finished() const;
    void shutdown();

private:
    struct Column {
        int head = 0;
        int length = 8;
        int interval = 1;
        int tick = 0;
        bool bright = true;
        uint8_t glyph[24]{};
    };

    void init_columns();
    void advance();
    void render();
    uint32_t rnd();
    void put(int x, int y, uint16_t color);

    lv_obj_t* canvas_ = nullptr;
    lv_draw_buf_t* draw_ = nullptr;
    Column columns_[40]{};
    uint32_t rng_ = 0xC0FFEEU;
    uint32_t started_ = 0;
    uint32_t last_step_ = 0;
    uint16_t palette_bright_[8]{};
    uint16_t palette_dim_[8]{};
    uint16_t logo_color_ = 0;
    int stride_ = 0;
};

} // namespace view

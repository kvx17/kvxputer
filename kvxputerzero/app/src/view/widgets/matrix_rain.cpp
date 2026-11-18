#include "matrix_rain.h"

#include "kvx_logo_bitmap.h"
#include "matrix_glyphs.h"
#include "theme.h"

#include "src/misc/cache/instance/lv_image_cache.h"

#include <cstring>

namespace view {
namespace {

constexpr int kCols = 40;
constexpr int kCellW = view::kScreenWidth / kCols;
constexpr int kCellH = 10;
constexpr int kRows = view::kScreenHeight / kCellH;

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool logo_on(int x, int y) {
    if (x < 0 || y < 0 || x >= kKvxLogoW || y >= kKvxLogoH) {
        return false;
    }
    const int bit = y * kKvxLogoW + x;
    return (kKvxLogoBits[bit >> 3] >> (7 - (bit & 7))) & 1;
}

} // namespace

MatrixRain::~MatrixRain() {
    shutdown();
}

bool MatrixRain::mount(lv_obj_t* parent) {
    shutdown();
    if (!parent) {
        return false;
    }

    draw_ = lv_draw_buf_create(view::kScreenWidth, view::kScreenHeight, LV_COLOR_FORMAT_RGB565, 0);
    if (!draw_ || !draw_->data) {
        shutdown();
        return false;
    }
    stride_ = static_cast<int>(draw_->header.stride);

    canvas_ = lv_canvas_create(parent);
    if (!canvas_) {
        shutdown();
        return false;
    }
    lv_canvas_set_draw_buf(canvas_, draw_);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(canvas_);

    const uint8_t bright[][3] = {
        {230, 255, 230},
        {160, 255, 170},
        {70, 255, 80},
        {20, 210, 50},
        {0, 160, 36},
        {0, 110, 24},
        {0, 70, 16},
        {0, 36, 8},
    };
    const uint8_t dim[][3] = {
        {120, 190, 120},
        {50, 150, 60},
        {10, 110, 28},
        {0, 80, 18},
        {0, 55, 12},
        {0, 40, 10},
        {0, 26, 6},
        {0, 16, 4},
    };
    for (int i = 0; i < 8; ++i) {
        palette_bright_[i] = pack565(bright[i][0], bright[i][1], bright[i][2]);
        palette_dim_[i] = pack565(dim[i][0], dim[i][1], dim[i][2]);
    }
    logo_color_ = pack565(90, 255, 48);

    rng_ ^= lv_tick_get() * 1664525u + 1013904223u;
    if (rng_ == 0) {
        rng_ = 0xA5A5F00Du;
    }
    init_columns();
    started_ = lv_tick_get();
    last_step_ = started_;
    render();
    return true;
}

void MatrixRain::shutdown() {
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_delete(canvas_);
    }
    canvas_ = nullptr;
    if (draw_) {
        lv_draw_buf_destroy(draw_);
        draw_ = nullptr;
    }
    stride_ = 0;
}

void MatrixRain::step() {
    if (!draw_) {
        return;
    }
    const uint32_t dt = lv_tick_elaps(last_step_);
    if (dt < kFrameMs) {
        return;
    }
    int frames = static_cast<int>(dt / kFrameMs);
    if (frames > 2) {
        frames = 2;
        last_step_ = lv_tick_get();
    } else {
        last_step_ += static_cast<uint32_t>(frames) * kFrameMs;
    }
    for (int i = 0; i < frames; ++i) {
        advance();
    }
    render();
}

bool MatrixRain::finished() const {
    if (!draw_) {
        return true;
    }
    return lv_tick_elaps(started_) >= kDurationMs;
}

uint32_t MatrixRain::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void MatrixRain::init_columns() {
    for (int i = 0; i < kCols; ++i) {
        Column& col = columns_[i];
        col.head = static_cast<int>(rnd() % static_cast<uint32_t>(kRows + 6)) - 4;
        col.length = 6 + static_cast<int>(rnd() % 11u);
        col.interval = 1 + static_cast<int>(rnd() % 3u);
        col.tick = static_cast<int>(rnd() % static_cast<uint32_t>(col.interval));
        col.bright = (rnd() % 4u) != 0;
        for (int row = 0; row < kRows && row < static_cast<int>(sizeof(col.glyph)); ++row) {
            col.glyph[row] = static_cast<uint8_t>(rnd() % static_cast<uint32_t>(kMatrixGlyphCount));
        }
    }
}

void MatrixRain::advance() {
    for (int i = 0; i < kCols; ++i) {
        Column& col = columns_[i];
        if (++col.tick < col.interval) {
            if ((rnd() % 7u) == 0 && col.length > 0) {
                const int row = col.head - static_cast<int>(rnd() % static_cast<uint32_t>(col.length));
                if (row >= 0 && row < kRows) {
                    col.glyph[row] = static_cast<uint8_t>(rnd() % static_cast<uint32_t>(kMatrixGlyphCount));
                }
            }
            continue;
        }
        col.tick = 0;
        ++col.head;
        if (col.head >= 0 && col.head < kRows) {
            col.glyph[col.head] = static_cast<uint8_t>(rnd() % static_cast<uint32_t>(kMatrixGlyphCount));
        }
        if (col.head - col.length > kRows) {
            col.head = -static_cast<int>(1 + rnd() % 8u);
            col.length = 6 + static_cast<int>(rnd() % 11u);
            col.interval = 1 + static_cast<int>(rnd() % 3u);
            col.bright = (rnd() % 4u) != 0;
        }
    }
}

void MatrixRain::put(int x, int y, uint16_t color) {
    if (!draw_ || x < 0 || y < 0 || x >= view::kScreenWidth || y >= view::kScreenHeight) {
        return;
    }
    auto* pixel = reinterpret_cast<uint16_t*>(draw_->data + y * stride_ + x * 2);
    *pixel = color;
}

void MatrixRain::render() {
    if (!draw_ || !draw_->data) {
        return;
    }
    std::memset(draw_->data, 0, draw_->data_size);

    for (int i = 0; i < kCols; ++i) {
        const Column& col = columns_[i];
        const uint16_t* palette = col.bright ? palette_bright_ : palette_dim_;
        const int x0 = i * kCellW + (kCellW - 5) / 2;
        for (int row = col.head - col.length + 1; row <= col.head; ++row) {
            if (row < 0 || row >= kRows) {
                continue;
            }
            const int age = col.head - row;
            const uint16_t color = palette[age < 7 ? age : 7];
            const uint8_t* glyph = kMatrixGlyphs[col.glyph[row] % kMatrixGlyphCount];
            const int y0 = row * kCellH + 1;
            for (int gx = 0; gx < 5; ++gx) {
                const uint8_t bits = glyph[gx];
                for (int gy = 0; gy < 7; ++gy) {
                    if (bits & (1u << gy)) {
                        put(x0 + gx, y0 + gy, color);
                    }
                }
            }
        }
    }

    constexpr int kDstW = 286;
    constexpr int kDstH = (kKvxLogoH * kDstW + kKvxLogoW / 2) / kKvxLogoW;
    const int origin_x = (view::kScreenWidth - kDstW) / 2;
    const int origin_y = (view::kScreenHeight - kDstH) / 2;

    auto sample = [](int x, int y) {
        if (x < 0 || y < 0 || x >= kDstW || y >= kDstH) {
            return false;
        }
        return logo_on(x * kKvxLogoW / kDstW, y * kKvxLogoH / kDstH);
    };

    for (int y = 0; y < kDstH; ++y) {
        for (int x = 0; x < kDstW; ++x) {
            if (!sample(x, y)) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    put(origin_x + x + dx, origin_y + y + dy, 0);
                }
            }
        }
    }
    for (int y = 0; y < kDstH; ++y) {
        for (int x = 0; x < kDstW; ++x) {
            if (sample(x, y)) {
                put(origin_x + x, origin_y + y, logo_color_);
            }
        }
    }

    lv_image_cache_drop(draw_);
    if (canvas_) {
        lv_obj_invalidate(canvas_);
    }
}

} // namespace view

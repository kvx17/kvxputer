#include "level_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screen {
namespace {

constexpr float kPi = 3.14159265358979f;

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
}
void line(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put_px(data, stride, w, h, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void ring(uint8_t* data, int stride, int w, int h, int cx, int cy, int r, uint16_t c) {
    for (int a = 0; a < 360; a += 2)
        put_px(data, stride, w, h, cx + static_cast<int>(r * std::cos(a * kPi / 180.f)),
               cy + static_cast<int>(r * std::sin(a * kPi / 180.f)), c);
}
void disc(uint8_t* data, int stride, int w, int h, int cx, int cy, int r, uint16_t c) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r) put_px(data, stride, w, h, cx + x, cy + y, c);
}
const char* cardinal(float deg) {
    static const char* k[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return k[static_cast<int>((deg + 22.5f) / 45.f) & 7];
}

} // namespace

LevelScreen::LevelScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    std::string err;
    imu_ok_ = imu_.open(err);
    init();
}

LevelScreen::~LevelScreen() {
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void LevelScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Level");

    if (!imu_ok_) {
        notice_ = lv_label_create(root);
        auto* font = assets().load_standard_font(13);
        lv_obj_set_style_text_font(notice_, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(notice_, tok.ink, 0);
        lv_obj_set_width(notice_, 300);
        lv_label_set_long_mode(notice_, LV_LABEL_LONG_WRAP);
        lv_obj_align(notice_, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(notice_,
                          "Motion sensor not found.\n\nLevel, angle and compass are only available "
                          "on the initial Kickstarter Cardputer (BMI270 / BMM150).");
        return;
    }

    auto* font = assets().load_standard_font(12);
    // Tabs span the full width; the section name is the chrome, so no top-right
    // chip here (it would collide with the "Compass" tab label).
    tabs_.build(root, 20, {"Level", "Angle", "Compass"}, font, tok);

    w_ = 120;
    h_ = view::kContentHeight - 62;
    if (h_ < 60) h_ = 60;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 10, 50);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    auto* big = assets().load_standard_font(34);
    big_ = view::widgets::make_readout(root, big, tok);
    lv_obj_set_width(big_, 160);
    lv_obj_set_pos(big_, 150, 58);

    cap_ = view::widgets::make_caption(root, font, tok);
    lv_obj_set_width(cap_, 160);
    lv_obj_set_pos(cap_, 150, 98);

    hint_ = view::widgets::make_hint_bar(root, "Left/Right section   Enter hold   Tab zero/calib", font, tok);

    bubx_.reset(0.f);
    buby_.reset(0.f);
    horizon_.reset(0.f);
    rose_.reset(0.f);
    last_ms_ = lv_tick_get();
    timer_ = lv_timer_create(timer_cb, 40, this);
    draw();
}

void LevelScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<LevelScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void LevelScreen::on_timer() {
    const uint32_t dt = lv_tick_elaps(last_ms_);
    last_ms_ = lv_tick_get();

    if (!hold_) {
        platform::ImuSample s;
        std::string err;
        if (imu_.read(s, err)) {
            if (s.accel) {
                const float denom = std::sqrt(s.ay * s.ay + s.az * s.az);
                pitch_ = std::atan2(-s.ax, denom < 1e-4f ? 1e-4f : denom) * 180.f / kPi - pitch0_;
                roll_ = std::atan2(s.ay, s.az) * 180.f / kPi - roll0_;
                // Flat on a table (gravity on Z) vs standing upright (gravity on Y).
                flat_ = std::fabs(s.az) >= std::fabs(s.ax) && std::fabs(s.az) >= std::fabs(s.ay);
            }
            if (s.mag) {
                mx_min_ = std::min(mx_min_, s.mx);
                mx_max_ = std::max(mx_max_, s.mx);
                my_min_ = std::min(my_min_, s.my);
                my_max_ = std::max(my_max_, s.my);
                const float cx = (mx_min_ + mx_max_) * 0.5f;
                const float cy = (my_min_ + my_max_) * 0.5f;
                float hd = std::atan2(s.my - cy, s.mx - cx) * 180.f / kPi;
                if (hd < 0) hd += 360.f;
                heading_deg_ = hd;
                mag_cal_ = (mx_max_ - mx_min_) > 5.f && (my_max_ - my_min_) > 5.f;
            }
        }
    }

    // Section stays where the user put it (Left/Right). Only the VIEW inside it
    // changes with orientation (flat on a table vs standing upright).

    // Smooth followers (~120ms, pocket-zero bubble/ruler/compass rule).
    bubx_.set(std::clamp(roll_ / 30.f, -1.f, 1.f));
    buby_.set(std::clamp(-pitch_ / 30.f, -1.f, 1.f));
    horizon_.set(roll_);
    bubx_.tick(static_cast<float>(dt), 120.f);
    buby_.tick(static_cast<float>(dt), 120.f);
    horizon_.tick(static_cast<float>(dt), 120.f);
    // Compass rose rotation smoothed with wrap handling.
    if (heading_deg_ >= 0.f) {
        float d = heading_deg_ - rose_.value;
        while (d > 180.f) d -= 360.f;
        while (d < -180.f) d += 360.f;
        rose_.target = rose_.value + d;
        rose_.tick(static_cast<float>(dt), 120.f);
    }

    const int sec = tabs_.active();
    if (big_ && cap_) {
        if (sec == 0) {  // Level
            const float tilt = std::sqrt(pitch_ * pitch_ + roll_ * roll_);
            lv_label_set_text_fmt(big_, "%.1f\xC2\xB0", tilt);
            lv_label_set_text_fmt(cap_, "pitch %+.1f\nroll  %+.1f", pitch_, roll_);
        } else if (sec == 1) {  // Angle
            lv_label_set_text_fmt(big_, "%+.0f\xC2\xB0", pitch_);
            lv_label_set_text_fmt(cap_, "pitch\nroll %+.1f\xC2\xB0", roll_);
        } else {  // Compass
            if (!imu_.info().has_mag) {
                lv_label_set_text(big_, "--");
                lv_label_set_text(cap_, "no magnetometer");
            } else if (!mag_cal_) {
                lv_label_set_text(big_, "cal");
                lv_label_set_text(cap_, "rotate device,\nthen Tab");
            } else {
                lv_label_set_text_fmt(big_, "%.0f\xC2\xB0", heading_deg_);
                lv_label_set_text_fmt(cap_, "%s", cardinal(heading_deg_));
            }
        }
    }
    draw();
}

void LevelScreen::draw() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(8, 10, 14);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) reinterpret_cast<uint16_t*>(data + y * stride_)[x] = bg;

    const int cx = w_ / 2, cy = h_ / 2;
    const int R = std::min(cx, cy) - 3;
    const uint16_t ringc = pack565(80, 90, 110);
    const uint16_t guide = pack565(40, 46, 58);
    const uint16_t fg = pack565(120, 200, 255);
    const uint16_t ok = pack565(0x39, 0xFF, 0x14);
    const int sec = tabs_.active();

    if (sec == 0) {  // ---- LEVEL ----
        if (flat_) {
            // Bullseye, looking down at the table.
            ring(data, stride_, w_, h_, cx, cy, R, ringc);
            ring(data, stride_, w_, h_, cx, cy, R / 2, pack565(50, 56, 70));
            const int bx = cx + static_cast<int>(bubx_.value * (R - 6));
            const int by = cy + static_cast<int>(buby_.value * (R - 6));
            const bool lvl = std::fabs(pitch_) < 1.5f && std::fabs(roll_) < 1.5f;
            disc(data, stride_, w_, h_, bx, by, 6, lvl ? ok : fg);
        } else {
            // Standing: horizontal spirit tube, bubble moves left/right by roll.
            const int tw = w_ - 16, th = 18;
            const int x0 = cx - tw / 2, y0 = cy - th / 2;
            for (int x = x0; x <= x0 + tw; ++x) {
                put_px(data, stride_, w_, h_, x, y0, ringc);
                put_px(data, stride_, w_, h_, x, y0 + th, ringc);
            }
            for (int y = y0; y <= y0 + th; ++y) {
                put_px(data, stride_, w_, h_, cx - 8, y, guide);
                put_px(data, stride_, w_, h_, cx + 8, y, guide);
            }
            const int bx = cx + static_cast<int>(bubx_.value * (tw / 2 - 8));
            const bool lvl = std::fabs(roll_) < 1.0f;
            disc(data, stride_, w_, h_, bx, cy, 7, lvl ? ok : fg);
        }
    } else if (sec == 1) {  // ---- ANGLE ----
        if (flat_) {
            // Surface horizon tilting by roll.
            const float a = horizon_.value * kPi / 180.f;
            const int dx = static_cast<int>(std::cos(a) * (R - 2));
            const int dy = static_cast<int>(std::sin(a) * (R - 2));
            line(data, stride_, w_, h_, cx - dx, cy + dy, cx + dx, cy - dy, fg);
            for (int x = 0; x < w_; x += 2) put_px(data, stride_, w_, h_, x, cy, guide);
        } else {
            // Standing: inclinometer arm pivoting from the lower-left by pitch.
            const int px = 6, py = h_ - 6;
            for (int x = px; x < w_; x += 2) put_px(data, stride_, w_, h_, x, py, guide);
            for (int y = py; y > py - (h_ - 12); y -= 2) put_px(data, stride_, w_, h_, px, y, guide);
            const float a = (-pitch_) * kPi / 180.f;
            const int len = w_ - 14;
            const int ex = px + static_cast<int>(std::cos(a) * len);
            const int ey = py + static_cast<int>(std::sin(a) * len);
            line(data, stride_, w_, h_, px, py, ex, ey, fg);
        }
    } else {  // ---- COMPASS ----
        const bool ready = imu_.info().has_mag && mag_cal_ && heading_deg_ >= 0.f;
        if (flat_) {
            // Top-down navigator rose: it rotates so the heading is under the marker.
            ring(data, stride_, w_, h_, cx, cy, R, ringc);
            if (ready) {
                struct Card { float bearing; uint16_t col; int inset; };
                const Card cards[4] = {{0.f, pack565(255, 80, 80), 14},
                                       {90.f, pack565(120, 130, 150), 8},
                                       {180.f, pack565(120, 130, 150), 8},
                                       {270.f, pack565(120, 130, 150), 8}};
                for (const auto& c : cards) {
                    const float t = (c.bearing - rose_.value - 90.f) * kPi / 180.f;
                    line(data, stride_, w_, h_, cx + static_cast<int>(std::cos(t) * (R - c.inset)),
                         cy + static_cast<int>(std::sin(t) * (R - c.inset)),
                         cx + static_cast<int>(std::cos(t) * R), cy + static_cast<int>(std::sin(t) * R),
                         c.col);
                }
            }
            line(data, stride_, w_, h_, cx, cy - R - 1, cx, cy - R + 8, ok);
            disc(data, stride_, w_, h_, cx, cy, 2, pack565(200, 210, 230));
        } else {
            // Standing: horizontal heading tape (sighting compass), scrolls past a
            // fixed center marker.
            const int ty = cy;
            for (int x = 0; x < w_; ++x) put_px(data, stride_, w_, h_, x, ty + 9, guide);
            if (ready) {
                const float px_per_deg = static_cast<float>(w_) / 120.f;  // ~120deg visible
                for (int b = 0; b < 360; b += 15) {
                    float rel = static_cast<float>(b) - rose_.value;
                    while (rel > 180.f) rel -= 360.f;
                    while (rel < -180.f) rel += 360.f;
                    const int x = cx + static_cast<int>(rel * px_per_deg);
                    if (x < 2 || x > w_ - 2) continue;
                    const uint16_t col = (b == 0) ? pack565(255, 80, 80) : ringc;
                    const int hh = (b % 90 == 0) ? 8 : 5;
                    for (int y = ty - hh; y <= ty + hh; ++y) put_px(data, stride_, w_, h_, x, y, col);
                }
            }
            line(data, stride_, w_, h_, cx, ty - 12, cx, ty + 12, ok);
        }
    }
    if (canvas_) lv_obj_invalidate(canvas_);
}

void LevelScreen::zero_or_calibrate() {
    if (tabs_.active() == 2) {
        mx_min_ = my_min_ = 1e9f;
        mx_max_ = my_max_ = -1e9f;
        mag_cal_ = false;
    } else {
        pitch0_ += pitch_;
        roll0_ += roll_;
        pitch_ = roll_ = 0.f;
    }
}

void LevelScreen::sync_chrome() {
    if (hint_)
        lv_label_set_text(hint_, hold_ ? "HELD - Enter resume"
                                       : "Left/Right section   Enter hold   Tab zero/calib");
}

bool LevelScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (!imu_ok_) return KvxScreen::handle_key(key);
    if (key == LV_KEY_LEFT) {
        manual_until_ = lv_tick_get() + 6000;
        tabs_.cycle(-1);
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        manual_until_ = lv_tick_get() + 6000;
        tabs_.cycle(1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        hold_ = !hold_;
        sync_chrome();
        return true;
    }
    if (key == platform::kKeyTab) {
        zero_or_calibrate();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> LevelScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() { if (imu_ok_) tabs_.cycle(-1); }},
        {view::ICON_CHECK, [this]() { if (imu_ok_) { hold_ = !hold_; sync_chrome(); } }},
        {view::ICON_ARROW_RIGHT, [this]() { if (imu_ok_) tabs_.cycle(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

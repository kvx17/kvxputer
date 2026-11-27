#include "level_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

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
    load_threshold();
    std::string err;
    imu_ok_ = imu_.open(err);
#if USE_DESKTOP
    imu_ok_ = true;  // synthetic sensors so the instrument layout can be previewed
#endif
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
    h_ = view::kContentHeight - 46;
    if (h_ < 60) h_ = 60;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 8, 40);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    auto* big = assets().load_standard_font(30);
    big_ = view::widgets::make_readout(root, big, tok);
    lv_obj_set_width(big_, 170);
    lv_obj_set_pos(big_, 146, 42);

    cap_ = view::widgets::make_caption(root, font, tok);
    lv_obj_set_width(cap_, 170);
    lv_obj_set_pos(cap_, 146, 74);

    // Controls live in the right column next to the readout, not a bottom bar that
    // would overlap the left instrument canvas.
    hint_ = view::widgets::make_hint_bar(root, "", font, tok);
    lv_obj_set_width(hint_, 170);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint_, LV_ALIGN_TOP_LEFT, 146, 110);
    update_hint();

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
        bool ok = imu_.read(s, err);
#if USE_DESKTOP
        if (!ok) {
            static float tt = 0.f;
            tt += 0.02f;
            s.accel = true;
            s.ax = 0.15f * std::sin(tt);
            s.ay = 0.10f * std::cos(tt * 0.7f);
            s.az = 0.98f;
            s.mag = true;
            s.mx = 25.f * std::cos(tt * 0.4f);
            s.my = 25.f * std::sin(tt * 0.4f);
            s.mz = 10.f;
            ok = true;
        }
#endif
        if (ok) {
            if (s.accel) {
                // Low-pass the accel so a still device reads steady (kills jitter).
                constexpr float A = 0.12f;
                ax_ += A * (s.ax - ax_);
                ay_ += A * (s.ay - ay_);
                az_ += A * (s.az - az_);
                grav_g_ = std::sqrt(ax_ * ax_ + ay_ * ay_ + az_ * az_);
                const float denom = std::sqrt(ay_ * ay_ + az_ * az_);
                pitch_ = std::atan2(-ax_, denom < 1e-4f ? 1e-4f : denom) * 180.f / kPi - pitch0_;
                roll_ = std::atan2(ay_, az_) * 180.f / kPi - roll0_;
                // Tilt of the back from horizontal: 0deg = flat on its back, 90deg =
                // standing on the bottom edge. Switch at the user threshold with a
                // +/-4deg hysteresis band so holding it doesn't flicker.
                const float ratio = grav_g_ > 1e-3f ? std::clamp(std::fabs(az_) / grav_g_, 0.f, 1.f) : 1.f;
                tilt_deg_ = std::acos(ratio) * 180.f / kPi;
                if (!standing_ && tilt_deg_ > static_cast<float>(orient_threshold_) + 4.f) standing_ = true;
                else if (standing_ && tilt_deg_ < static_cast<float>(orient_threshold_) - 4.f) standing_ = false;
                flat_ = !standing_;
            }
            if (s.mag) {
                mx_min_ = std::min(mx_min_, s.mx);
                mx_max_ = std::max(mx_max_, s.mx);
                my_min_ = std::min(my_min_, s.my);
                my_max_ = std::max(my_max_, s.my);
                mz_min_ = std::min(mz_min_, s.mz);
                mz_max_ = std::max(mz_max_, s.mz);
                // Low-pass the mag too (heading is very noise-sensitive).
                constexpr float M = 0.15f;
                mxf_ += M * (s.mx - mxf_);
                myf_ += M * (s.my - myf_);
                mzf_ += M * (s.mz - mzf_);
                const float mx = mxf_ - (mx_min_ + mx_max_) * 0.5f;  // hard-iron (all 3 axes)
                const float my = myf_ - (my_min_ + my_max_) * 0.5f;
                const float mz = mzf_ - (mz_min_ + mz_max_) * 0.5f;
                // Gimbal-lock-free tilt-compensated heading via cross products. The
                // bearing is of the TOP edge (device X) when flat, or the SCREEN
                // normal (device Z) when standing, so pivoting in place (yaw about
                // gravity) changes the heading in either orientation.
                const float an = grav_g_ > 1e-3f ? grav_g_ : 1.f;
                const float ux = ax_ / an, uy = ay_ / an, uz = az_ / an;  // up (accel)
                const float mdu = mx * ux + my * uy + mz * uz;
                const float nx = mx - mdu * ux, ny = my - mdu * uy, nz = mz - mdu * uz;      // north-h
                const float ex = uy * nz - uz * ny, ey = uz * nx - ux * nz, ez = ux * ny - uy * nx;  // east
                const float Px = flat_ ? 1.f : 0.f, Py = 0.f, Pz = flat_ ? 0.f : 1.f;
                const float pdu = Px * ux + Py * uy + Pz * uz;
                const float px = Px - pdu * ux, py = Py - pdu * uy, pz = Pz - pdu * uz;
                float hd = std::atan2(px * ex + py * ey + pz * ez, px * nx + py * ny + pz * nz) * 180.f / kPi;
                if (hd < 0) hd += 360.f;
                heading_deg_ = hd;
                mag_cal_ = (mx_max_ - mx_min_) > 5.f && (my_max_ - my_min_) > 5.f &&
                           (mz_max_ - mz_min_) > 5.f;
                field_ut_ = std::sqrt(mxf_ * mxf_ + myf_ * myf_ + mzf_ * mzf_);
            }
        }
    }

    // Section stays where the user put it (Left/Right). Only the VIEW inside it
    // changes with orientation (flat on a table vs standing upright).

    // Smooth followers (~120ms, pocket-zero bubble/ruler/compass rule).
    bubx_.set(std::clamp(roll_ / 30.f, -1.f, 1.f));
    buby_.set(std::clamp(-pitch_ / 30.f, -1.f, 1.f));
    horizon_.set(roll_);
    bubx_.tick(static_cast<float>(dt), 350.f);  // heavier smoothing: bubble should sit still
    buby_.tick(static_cast<float>(dt), 350.f);
    horizon_.tick(static_cast<float>(dt), 200.f);
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
            lv_label_set_text_fmt(cap_, "R%+.0f P%+.0f  %.2fg\n%s", roll_, pitch_, grav_g_,
                                  tilt < 1.5f ? "LEVEL (<1.5\xC2\xB0)" : "centre the bubble");
        } else if (sec == 1) {  // Angle finder: 0 at the rest position
            const float g = grav_g_ > 1e-3f ? grav_g_ : 1.f;
            // Flat: tilt of the surface from horizontal. Standing: lean of the long
            // axis from vertical (0 when the Zero is upright on its bottom edge).
            const float ang = flat_ ? tilt_deg_
                                    : std::acos(std::clamp(std::fabs(ax_) / g, 0.f, 1.f)) * 180.f / kPi;
            lv_label_set_text_fmt(big_, "%.0f\xC2\xB0", ang);
            lv_label_set_text_fmt(cap_, "%s\ngrav %.2fg  zero:%s", flat_ ? "from horizontal" : "from vertical",
                                  grav_g_, zeroed_ ? "set" : "none");
        } else {  // Compass
            if (!imu_.info().has_mag) {
                lv_label_set_text(big_, "--");
                lv_label_set_text(cap_, "no magnetometer");
            } else if (!mag_cal_) {
                const float range = std::min({mx_max_ - mx_min_, my_max_ - my_min_, mz_max_ - mz_min_});
                const int pct = std::clamp(static_cast<int>(range / 25.f * 100.f), 0, 99);
                lv_label_set_text(big_, "cal");
                lv_label_set_text_fmt(cap_, "rotate + Tab\ncalib %d%%", pct);
            } else {
                lv_label_set_text_fmt(big_, "%.0f\xC2\xB0", heading_deg_);
                lv_label_set_text_fmt(cap_, "%s  %.0f uT\n%s", cardinal(heading_deg_), field_ut_,
                                      imu_.info().mag_name.empty() ? "BMM150" : imu_.info().mag_name.c_str());
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
            // Standing: a centre-pivoted needle, straight up when upright, rotating
            // by the lean-from-vertical. Stays centred (never leaves the frame).
            const int ncx = w_ / 2, ncy = h_ / 2;
            const int nr = std::min(ncx, ncy) - 3;
            for (int y = ncy - nr; y <= ncy + nr; y += 2) put_px(data, stride_, w_, h_, ncx, y, guide);
            const float denom = std::fabs(ax_) < 1e-3f ? 1e-3f : std::fabs(ax_);
            const float lean = std::atan2(ay_, denom);
            const int ex = ncx + static_cast<int>(std::sin(lean) * nr);
            const int ey = ncy - static_cast<int>(std::cos(lean) * nr);
            line(data, stride_, w_, h_, ncx, ncy, ex, ey, fg);
            disc(data, stride_, w_, h_, ncx, ncy, 2, pack565(200, 210, 230));
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
        // Restart compass calibration (hard-iron min/max sweep, all 3 axes).
        mx_min_ = my_min_ = mz_min_ = 1e9f;
        mx_max_ = my_max_ = mz_max_ = -1e9f;
        mag_cal_ = false;
    } else if (zeroed_) {
        // Tab again clears the zero (pocket-zero unzero).
        pitch0_ = 0.f;
        roll0_ = 0.f;
        zeroed_ = false;
    } else {
        // Zero the level here.
        pitch0_ += pitch_;
        roll0_ += roll_;
        zeroed_ = true;
    }
}

void LevelScreen::sync_chrome() {
    update_hint();
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
    if (key == LV_KEY_UP) {
        orient_threshold_ = std::clamp(orient_threshold_ + 5, 20, 80);
        save_threshold();
        update_hint();
        return true;
    }
    if (key == LV_KEY_DOWN) {
        orient_threshold_ = std::clamp(orient_threshold_ - 5, 20, 80);
        save_threshold();
        update_hint();
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

void LevelScreen::update_hint() {
    if (!hint_) return;
    if (hold_)
        lv_label_set_text(hint_, "HELD\nEnter resume");
    else
        lv_label_set_text_fmt(hint_, "L/R mode   U/D %d\xC2\xB0\nEnter hold   Tab zero", orient_threshold_);
}

void LevelScreen::load_threshold() {
    std::ifstream in(tools::config_file("level_orient.txt"));
    int v = 0;
    if (in && (in >> v) && v >= 20 && v <= 80) orient_threshold_ = v;
}

void LevelScreen::save_threshold() {
    std::ofstream out(tools::config_file("level_orient.txt"), std::ios::trunc);
    if (out) out << orient_threshold_ << "\n";
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

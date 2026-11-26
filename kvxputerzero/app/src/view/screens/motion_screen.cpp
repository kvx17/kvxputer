#include "motion_screen.h"

#include "asset_manager.h"
#include "instrument.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screen {
namespace {

constexpr int kBarX = 92;
constexpr int kBarW = 150;

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
}

} // namespace

MotionScreen::MotionScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    std::string err;
    imu_ok_ = imu_.open(err) && imu_.info().has_accel;
    init();
}

MotionScreen::~MotionScreen() {
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void MotionScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Motion");

    auto* font = assets().load_standard_font(12);

    if (!imu_ok_) {
        notice_ = lv_label_create(root);
        auto* nfont = assets().load_standard_font(13);
        lv_obj_set_style_text_font(notice_, nfont ? nfont : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(notice_, tok.ink, 0);
        lv_obj_set_width(notice_, 300);
        lv_label_set_long_mode(notice_, LV_LABEL_LONG_WRAP);
        lv_obj_align(notice_, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(notice_,
                          "Accelerometer not found.\n\nMotion is only available on the initial "
                          "Kickstarter Cardputer (BMI270).");
        return;
    }

    chip_ = view::widgets::make_status_chip(root, "LIVE", view::StatusPillKind::Ok, font, tok);

    // Row band: |a| readout on the left, X/Y/Z bars stacked on the right.
    auto* big = assets().load_standard_font(28);
    mag_lbl_ = view::widgets::make_readout(root, big, tok);
    lv_obj_set_pos(mag_lbl_, 8, 18);

    imp_cap_ = view::widgets::make_caption(root, font, tok);
    lv_obj_set_width(imp_cap_, 118);  // left column only; must not grow into the X/Y/Z bars
    lv_obj_set_pos(imp_cap_, 8, 52);

    const char* names[3] = {"X", "Y", "Z"};
    const int bar_x = 150;
    const int bar_w = 110;
    for (int i = 0; i < 3; ++i) {
        const int y = 24 + i * 15;
        axis_lbl_[i] = lv_label_create(root);
        lv_obj_set_style_text_font(axis_lbl_[i], font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(axis_lbl_[i], tok.ink_dim, 0);
        lv_obj_set_pos(axis_lbl_[i], 130, y - 2);
        lv_label_set_text(axis_lbl_[i], names[i]);

        auto* track = lv_obj_create(root);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, bar_w, 7);
        lv_obj_set_pos(track, bar_x, y);
        lv_obj_set_style_radius(track, 3, 0);
        lv_obj_set_style_bg_color(track, tok.surface, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

        auto* fill = lv_obj_create(track);
        lv_obj_remove_style_all(fill);
        lv_obj_set_size(fill, 0, 7);
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(fill, 3, 0);
        lv_obj_set_style_bg_color(fill, tok.accent, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        axis_bar_[i] = fill;
    }

    // Taller magnitude graph fills the rest of the body.
    w_ = view::kScreenWidth;
    h_ = view::kContentHeight - 74;
    if (h_ < 40) h_ = 40;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 0, 72);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    trace_.assign(static_cast<size_t>(w_), 1.f);
    timer_ = lv_timer_create(timer_cb, 40, this);
    draw();
}

void MotionScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<MotionScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void MotionScreen::on_timer() {
    platform::ImuSample s;
    std::string err;
    if (!imu_.read(s, err) || !s.accel) return;

    const float mag = std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    trace_.push_back(mag);
    while (trace_.size() > static_cast<size_t>(w_)) trace_.pop_front();

    const bool shake = mag >= threshold_;
    if (shake) {
        above_ = true;
        peak_cur_ = std::max(peak_cur_, mag);
    } else if (above_) {
        above_ = false;
        ++impact_n_;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%d %.1fg  ", impact_n_, peak_cur_);
        impacts_ = std::string(buf) + impacts_;
        if (impacts_.size() > 40) impacts_.resize(40);
        peak_cur_ = 0.f;
    }

    if (mag_lbl_) lv_label_set_text_fmt(mag_lbl_, "%.2f g", mag);
    const float av[3] = {s.ax, s.ay, s.az};
    for (int i = 0; i < 3; ++i) {
        if (axis_bar_[i]) lv_obj_set_width(axis_bar_[i], std::clamp(static_cast<int>(std::fabs(av[i]) / 2.f * kBarW), 0, kBarW));
    }
    if (imp_cap_) lv_label_set_text(imp_cap_, impacts_.empty() ? "no impacts" : impacts_.c_str());
    const auto tok = tokens();
    if (chip_)
        view::set_status_pill(chip_, shake ? "SHAKE" : "LIVE",
                              shake ? view::StatusPillKind::Busy : view::StatusPillKind::Ok, tok);
    draw();
}

void MotionScreen::draw() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(8, 10, 14);
    for (int yy = 0; yy < h_; ++yy)
        for (int xx = 0; xx < w_; ++xx) reinterpret_cast<uint16_t*>(data + yy * stride_)[xx] = bg;

    auto y_of = [&](float g) { return h_ - 1 - static_cast<int>(std::clamp(g / 3.f, 0.f, 1.f) * (h_ - 2)); };
    const uint16_t basec = pack565(40, 46, 58);
    for (int x = 0; x < w_; x += 2) put_px(data, stride_, w_, h_, x, y_of(1.f), basec);
    const uint16_t thr = pack565(120, 60, 60);
    for (int x = 0; x < w_; x += 2) put_px(data, stride_, w_, h_, x, y_of(threshold_), thr);

    const uint16_t line = pack565(80, 220, 255);
    int prev = y_of(trace_.empty() ? 1.f : trace_.front());
    for (size_t x = 0; x < trace_.size() && static_cast<int>(x) < w_; ++x) {
        const int y = y_of(trace_[x]);
        int y0 = std::min(prev, y), y1 = std::max(prev, y);
        for (int yy = y0; yy <= y1; ++yy) put_px(data, stride_, w_, h_, static_cast<int>(x), yy, line);
        prev = y;
    }
    if (canvas_) lv_obj_invalidate(canvas_);
}

bool MotionScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (imu_ok_ && (key == LV_KEY_ENTER || key == ' ' || key == LV_KEY_DEL)) {
        impacts_.clear();
        impact_n_ = 0;
        peak_cur_ = 0.f;
        if (imp_cap_) lv_label_set_text(imp_cap_, "no impacts");
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> MotionScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {nullptr, nullptr},
        {view::ICON_CHECK, [this]() {
             impacts_.clear();
             impact_n_ = 0;
             if (imp_cap_) lv_label_set_text(imp_cap_, "no impacts");
         }},
        {nullptr, nullptr},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

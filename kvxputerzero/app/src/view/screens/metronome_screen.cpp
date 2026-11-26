#include "metronome_screen.h"

#include "asset_manager.h"
#include "audio.h"
#include "instrument.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>

namespace screen {
namespace {
constexpr int kMinBpm = 30;
constexpr int kMaxBpm = 300;
constexpr float kMaxAngle = 0.52f;  // ~30 degrees
constexpr float kPi = 3.14159265358979f;

void click_async() {
    std::thread([]() {
        std::string e;
        (void)platform::play_tone(1600.f, 14, e);
    }).detach();
}

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
void put_px(uint8_t* d, int st, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(d + y * st)[x] = c;
}
void line(uint8_t* d, int st, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put_px(d, st, w, h, x0, y0, c);
        put_px(d, st, w, h, x0 + 1, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void disc(uint8_t* d, int st, int w, int h, int cx, int cy, int r, uint16_t c) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r) put_px(d, st, w, h, cx + x, cy + y, c);
}
} // namespace

MetronomeScreen::MetronomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

MetronomeScreen::~MetronomeScreen() {
    if (anim_) lv_timer_delete(anim_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void MetronomeScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Metronome");

    auto* font = assets().load_standard_font(12);
    auto* big = assets().load_standard_font(44);

    chip_ = view::widgets::make_status_chip(root, "STOPPED", view::StatusPillKind::Idle, font, tok);

    bpm_lbl_ = view::widgets::make_readout(root, big, tok);
    lv_obj_align(bpm_lbl_, LV_ALIGN_TOP_MID, 0, 22);

    beat_cap_ = view::widgets::make_caption(root, font, tok);
    lv_obj_align(beat_cap_, LV_ALIGN_TOP_MID, 0, 68);

    cw_ = 200;
    ch_ = view::kContentHeight - 92;
    if (ch_ < 50) ch_ = 50;
    draw_ = lv_draw_buf_create(cw_, ch_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_align(canvas_, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    hint_ = view::widgets::make_hint_bar(root, "Enter run   Up/Down tempo   Tab click", font, tok);

    refresh();
    draw(kMaxAngle);  // resting at an extreme
    last_ms_ = lv_tick_get();
    anim_ = lv_timer_create(anim_cb, 16, this);
}

void MetronomeScreen::refresh() {
    if (bpm_lbl_) lv_label_set_text_fmt(bpm_lbl_, "%d BPM", bpm_);
    if (beat_cap_)
        lv_label_set_text_fmt(beat_cap_, "beat %ld   click: %s", beat_count_,
                              click_at_ == ClickAt::Middle ? "middle" : "sides");
    const auto tok = tokens();
    if (chip_)
        view::set_status_pill(chip_, running_ ? "RUNNING" : "STOPPED",
                              running_ ? view::StatusPillKind::Ok : view::StatusPillKind::Idle, tok);
}

void MetronomeScreen::anim_cb(lv_timer_t* t) {
    auto* self = static_cast<MetronomeScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_anim();
}

void MetronomeScreen::on_anim() {
    const uint32_t dt = lv_tick_elaps(last_ms_);
    last_ms_ = lv_tick_get();
    if (!running_) return;

    const double beat_ms = 60000.0 / std::clamp(bpm_, kMinBpm, kMaxBpm);
    const double prev = phase_;
    phase_ += static_cast<double>(dt) / beat_ms;

    // Pendulum at an extreme on integer phase, middle on half-integer.
    const double off = (click_at_ == ClickAt::Middle) ? 0.5 : 0.0;
    if (std::floor(phase_ - off) != std::floor(prev - off)) {
        ++beat_count_;
        click_async();
        if (beat_cap_)
            lv_label_set_text_fmt(beat_cap_, "beat %ld   click: %s", beat_count_,
                                  click_at_ == ClickAt::Middle ? "middle" : "sides");
    }

    draw(kMaxAngle * static_cast<float>(std::cos(phase_ * kPi)));
}

void MetronomeScreen::draw(float angle) {
    if (!draw_ || !draw_->data) return;
    auto* d = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(8, 10, 14);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) reinterpret_cast<uint16_t*>(d + y * stride_)[x] = bg;

    const int px = cw_ / 2, py = ch_ - 3;
    const int L = ch_ - 10;
    const int bx = px + static_cast<int>(L * std::sin(angle));
    const int by = py - static_cast<int>(L * std::cos(angle));

    // Guide ticks: center + the two extremes.
    const uint16_t guide = pack565(40, 46, 58);
    for (float a : {-kMaxAngle, 0.f, kMaxAngle}) {
        const int gx = px + static_cast<int>((L + 2) * std::sin(a));
        const int gy = py - static_cast<int>((L + 2) * std::cos(a));
        put_px(d, stride_, cw_, ch_, gx, gy, guide);
    }

    line(d, stride_, cw_, ch_, px, py, bx, by, pack565(120, 130, 150));
    disc(d, stride_, cw_, ch_, bx, by, 7, pack565(0x39, 0xFF, 0x14));  // bob (uses accent-ish tone)
    disc(d, stride_, cw_, ch_, px, py, 3, pack565(200, 210, 230));     // pivot
    if (canvas_) lv_obj_invalidate(canvas_);
}

void MetronomeScreen::set_bpm(int bpm) {
    bpm_ = std::clamp(bpm, kMinBpm, kMaxBpm);
    refresh();
}

void MetronomeScreen::toggle_run() {
    running_ = !running_;
    if (running_) {
        phase_ = 0.0;
        last_ms_ = lv_tick_get();
    } else {
        draw(kMaxAngle);
    }
    refresh();
}

void MetronomeScreen::toggle_click_mode() {
    click_at_ = (click_at_ == ClickAt::Middle) ? ClickAt::Sides : ClickAt::Middle;
    refresh();
}

bool MetronomeScreen::handle_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ENTER:
        case ' ':
            toggle_run();
            return true;
        case LV_KEY_UP:
        case '+':
        case '=':
            set_bpm(bpm_ + 1);
            return true;
        case LV_KEY_DOWN:
        case '-':
        case '_':
            set_bpm(bpm_ - 1);
            return true;
        case LV_KEY_RIGHT:
            set_bpm(bpm_ + 5);
            return true;
        case LV_KEY_LEFT:
            set_bpm(bpm_ - 5);
            return true;
        case LV_KEY_ESC:
            return KvxScreen::handle_key(key);
        default:
            break;
    }
    if (key == platform::kKeyTab) {
        toggle_click_mode();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> MetronomeScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { set_bpm(bpm_ - 5); }},
        {view::ICON_CHECK, [this]() { toggle_run(); }},
        {view::ICON_PLUS, [this]() { set_bpm(bpm_ + 5); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

#include "rf_spectrum_screen.h"
#include "asset_manager.h"

#include "audio.h"
#include "backlight.h"
#include "cap_detect.h"
#include "theme.h"
#include "tool_registry.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace screen {
namespace {

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t water_color(int dbm, int floor_db, int ref_db) {
    float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
    t = std::clamp(t, 0.f, 1.f);
    // Cool→hot so activity pops on a dark strip.
    const uint8_t r = static_cast<uint8_t>(20 + 220 * t);
    const uint8_t g = static_cast<uint8_t>(30 + 120 * t);
    const uint8_t b = static_cast<uint8_t>(90 + 40 * (1.f - t));
    return pack565(r, g, b);
}

void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    auto* row = reinterpret_cast<uint16_t*>(data + y * stride);
    row[x] = c;
}

void fill_rect(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w - 1, x1);
    y1 = std::min(h - 1, y1);
    for (int y = y0; y <= y1; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(data + y * stride);
        for (int x = x0; x <= x1; ++x) row[x] = c;
    }
}

void hline(uint8_t* data, int stride, int w, int h, int x0, int y, int x1, uint16_t c) {
    if (x0 > x1) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) put_px(data, stride, w, h, x, y, c);
}

void vline(uint8_t* data, int stride, int w, int h, int x, int y0, int y1, uint16_t c) {
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) put_px(data, stride, w, h, x, y, c);
}

void line(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    const int steps = std::max(1, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
    for (int s = 0; s <= steps; ++s) {
        const int x = x0 + (x1 - x0) * s / steps;
        const int y = y0 + (y1 - y0) * s / steps;
        put_px(data, stride, w, h, x, y, c);
        put_px(data, stride, w, h, x, y - 1, c); // 2px thick for readability
    }
}

void hatch_rect(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(w - 1, x1);
    y0 = std::max(0, y0);
    y1 = std::min(h - 1, y1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (((x + y) & 3) == 0) put_px(data, stride, w, h, x, y, c);
        }
    }
}

// Tiny 3x5 glyphs for axis labels on the canvas (WiFi analyzer style).
constexpr uint8_t kDigit[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, {0x2, 0x6, 0x2, 0x2, 0x7}, {0x7, 0x1, 0x7, 0x4, 0x7}, {0x7, 0x1, 0x7, 0x1, 0x7},
    {0x5, 0x5, 0x7, 0x1, 0x1}, {0x7, 0x4, 0x7, 0x1, 0x7}, {0x7, 0x4, 0x7, 0x5, 0x7}, {0x7, 0x1, 0x1, 0x1, 0x1},
    {0x7, 0x5, 0x7, 0x5, 0x7}, {0x7, 0x5, 0x7, 0x1, 0x7},
};

void draw_digit(uint8_t* data, int stride, int w, int h, int x, int y, int d, uint16_t c) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; ++row) {
        const uint8_t bits = kDigit[d][row];
        for (int col = 0; col < 3; ++col) {
            if (bits & (1 << (2 - col))) put_px(data, stride, w, h, x + col, y + row, c);
        }
    }
}

void draw_char(uint8_t* data, int stride, int w, int h, int x, int y, char ch, uint16_t c) {
    if (ch >= '0' && ch <= '9') {
        draw_digit(data, stride, w, h, x, y, ch - '0', c);
        return;
    }
    if (ch == '-') {
        hline(data, stride, w, h, x, y + 2, x + 2, c);
        return;
    }
    if (ch == '.') {
        put_px(data, stride, w, h, x + 1, y + 4, c);
        return;
    }
    if (ch == 'M') {
        put_px(data, stride, w, h, x, y, c);
        put_px(data, stride, w, h, x, y + 1, c);
        put_px(data, stride, w, h, x, y + 2, c);
        put_px(data, stride, w, h, x, y + 3, c);
        put_px(data, stride, w, h, x, y + 4, c);
        put_px(data, stride, w, h, x + 1, y + 1, c);
        put_px(data, stride, w, h, x + 2, y, c);
        put_px(data, stride, w, h, x + 2, y + 1, c);
        put_px(data, stride, w, h, x + 2, y + 2, c);
        put_px(data, stride, w, h, x + 2, y + 3, c);
        put_px(data, stride, w, h, x + 2, y + 4, c);
        return;
    }
}

void draw_text(uint8_t* data, int stride, int w, int h, int x, int y, const char* s, uint16_t c) {
    if (!s) return;
    int cx = x;
    for (const char* p = s; *p; ++p) {
        if (*p == ' ') {
            cx += 3;
            continue;
        }
        draw_char(data, stride, w, h, cx, y, *p, c);
        cx += 4;
    }
}

} // namespace

RfSpectrumScreen::RfSpectrumScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    settings_ = platform::load_rf_settings();
    center_hz_ = settings_.freq_hz;
    step_hz_ = settings_.step_hz;
    // Keep the user's tuned frequency; only set band/span defaults around it.
    preset_ = PresetId::Band433;
    span_hz_ = 2000000;
    if (center_hz_ >= 300000000 && center_hz_ < 348000000) preset_ = PresetId::Band315;
    else if (center_hz_ >= 779000000 && center_hz_ < 899000000) preset_ = PresetId::Band868;
    else if (center_hz_ >= 899000000) {
        preset_ = PresetId::Band915;
        span_hz_ = 4000000;
    }
    rebuild_freqs();
    init();
}

RfSpectrumScreen::~RfSpectrumScreen() {
    stop_worker();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
    }
    if (draw_) {
        lv_draw_buf_destroy(draw_);
        draw_ = nullptr;
    }
}

void RfSpectrumScreen::apply_preset(PresetId id) {
    preset_ = id;
    switch (id) {
        case PresetId::Band315:
            center_hz_ = 315000000;
            span_hz_ = 2000000;
            break;
        case PresetId::Band433:
            center_hz_ = 433920000;
            span_hz_ = 2000000;
            break;
        case PresetId::Band868:
            center_hz_ = 868300000;
            span_hz_ = 2000000;
            break;
        case PresetId::Band915:
            center_hz_ = 915000000;
            span_hz_ = 4000000;
            break;
        case PresetId::Full:
            center_hz_ = 614000000;
            span_hz_ = 628000000; // ~300–928
            break;
        case PresetId::Custom:
            center_hz_ = (custom_start_ / 2 + custom_stop_ / 2);
            span_hz_ = custom_stop_ > custom_start_ ? (custom_stop_ - custom_start_) : 1000000;
            break;
    }
    rebuild_freqs();
}

void RfSpectrumScreen::rebuild_freqs() {
    uint32_t start = 0, stop = 0;
    if (preset_ == PresetId::Full) {
        start = platform::kCc1101Bands[0].start_hz;
        stop = platform::kCc1101Bands[platform::kCc1101BandCount - 1].stop_hz;
    } else if (preset_ == PresetId::Custom) {
        start = custom_start_;
        stop = custom_stop_;
    } else {
        start = center_hz_ > span_hz_ / 2 ? center_hz_ - span_hz_ / 2 : 0;
        stop = center_hz_ + span_hz_ / 2;
    }
    gaps_ = platform::cc1101_unsupported_gaps(start, stop);
    uint32_t step = step_hz_;
    std::string err;
    freqs_.clear();
    if (!platform::cc1101_plan_sweep(start, stop, step, 96, freqs_, err)) {
        freqs_.clear();
    }
    step_hz_ = step;
    rssi_.assign(freqs_.size(), -120);
    peak_hold_.assign(freqs_.size(), -120);
    avg_acc_.assign(freqs_.size(), 0);
    avg_n_.assign(freqs_.size(), 0);
    peak_idx_ = -1;
    water_.fill(-120);
}

void RfSpectrumScreen::build() {
    auto* root = content();
    const auto tok = tokens();

    make_header(root, "RF Spectrum");

    auto* font = assets().load_standard_font(11);
    auto* font_sm = assets().load_standard_font(10);
    const lv_font_t* use = font ? font : &lv_font_montserrat_12;
    const lv_font_t* use_sm = font_sm ? font_sm : &lv_font_montserrat_12;

    // Two clear status lines — peak first (what users care about), then view.
    peak_lbl_ = lv_label_create(root);
    lv_obj_set_pos(peak_lbl_, 4, 18);
    lv_obj_set_width(peak_lbl_, 312);
    lv_obj_set_style_text_font(peak_lbl_, use, 0);
    lv_obj_set_style_text_color(peak_lbl_, tok.accent, 0);
    lv_label_set_long_mode(peak_lbl_, LV_LABEL_LONG_CLIP);

    view_lbl_ = lv_label_create(root);
    lv_obj_set_pos(view_lbl_, 4, 32);
    lv_obj_set_width(view_lbl_, 312);
    lv_obj_set_style_text_font(view_lbl_, use_sm, 0);
    lv_obj_set_style_text_color(view_lbl_, tok.ink, 0);
    lv_label_set_long_mode(view_lbl_, LV_LABEL_LONG_CLIP);

    constexpr int kTop = 46;
    constexpr int kFooterH = 12;
    plot_w_ = 320;
    plot_h_ = view::kContentHeight - kTop - kFooterH;
    water_h_ = plot_h_; // full-canvas waterfall when that mode is active
    draw_ = lv_draw_buf_create(plot_w_, plot_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, plot_w_, plot_h_);
        lv_obj_set_pos(canvas_, 0, kTop);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    footer_lbl_ = lv_label_create(root);
    lv_obj_set_pos(footer_lbl_, 4, view::kContentHeight - kFooterH);
    lv_obj_set_width(footer_lbl_, 312);
    lv_obj_set_style_text_font(footer_lbl_, use_sm, 0);
    lv_obj_set_style_text_color(footer_lbl_, tok.ink_dim, 0);
    lv_label_set_long_mode(footer_lbl_, LV_LABEL_LONG_CLIP);

    const auto cap = platform::detect_cc1101();
    cap_ok_ = cap.present;
    if (!cap_ok_) {
        cap_error_ = cap.error.empty() ? "Cap not present" : cap.error;
        demo_ = true;
    }
    rebuild_freqs();
    update_header();
    update_footer();
    draw_plot();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 80, this);
}

void RfSpectrumScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfSpectrumScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void RfSpectrumScreen::on_timer() {
    if (data_ready_.exchange(false)) {
        update_header();
        update_footer();
        draw_plot();
        if (threshold_alert_) {
            threshold_alert_ = false;
            pulse_alert();
        }
    }
}

const char* RfSpectrumScreen::band_name() const {
    switch (preset_) {
        case PresetId::Band315: return "315";
        case PresetId::Band433: return "433";
        case PresetId::Band868: return "868";
        case PresetId::Band915: return "915";
        case PresetId::Full: return "ALL";
        case PresetId::Custom: return "CUSTOM";
    }
    return "?";
}

const char* RfSpectrumScreen::display_mode_name() const {
    return display_mode_ == DisplayMode::Waterfall ? "waterfall" : "line";
}

void RfSpectrumScreen::cycle_display_mode() {
    display_mode_ = display_mode_ == DisplayMode::Line ? DisplayMode::Waterfall : DisplayMode::Line;
    status_extra_ = display_mode_ == DisplayMode::Waterfall ? " waterfall" : " line";
    update_header();
    update_footer();
    draw_plot();
}

void RfSpectrumScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        bool open = false;
        if (cap_ok_) {
            open = radio.open({}, err);
            if (!open) {
                cap_ok_ = false;
                cap_error_ = err;
                demo_ = true;
            }
        }
        while (!cancel_) {
            std::vector<uint32_t> freqs;
            {
                std::lock_guard<std::mutex> lock(data_mu_);
                freqs = freqs_;
            }
            if (freqs.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            std::vector<int> rssi(freqs.size(), -120);
            const auto t0 = std::chrono::steady_clock::now();
            if (open) {
                (void)radio.spectrum_sweep(freqs, rssi, err, &cancel_);
            } else {
                // Demo: synthetic peaks so UI can be exercised without Cap.
                static float phase = 0.f;
                phase += 0.15f;
                for (size_t i = 0; i < freqs.size(); ++i) {
                    const float x = static_cast<float>(i) / std::max<size_t>(1, freqs.size() - 1);
                    float v = -95.f + 25.f * std::sin(phase + x * 12.f);
                    if (std::fabs(x - 0.45f) < 0.03f) v = -35.f;
                    if (std::fabs(x - 0.72f) < 0.02f) v = -48.f;
                    rssi[i] = static_cast<int>(v);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            const auto t1 = std::chrono::steady_clock::now();
            const uint32_t ms =
                static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            std::lock_guard<std::mutex> lock(data_mu_);
            if (freqs.size() != freqs_.size()) continue;
            rssi_ = rssi;
            last_sweep_ms_ = ms;
            peak_idx_ = -1;
            peak_dbm_ = -120;
            peak_hz_ = 0;
            bool over = false;
            for (size_t i = 0; i < rssi_.size(); ++i) {
                if (peak_hold_on_) {
                    if (rssi_[i] > peak_hold_[i]) peak_hold_[i] = rssi_[i];
                    else if (peak_hold_decay_) peak_hold_[i] = std::max(peak_hold_[i] - 1, rssi_[i]);
                }
                avg_acc_[i] += rssi_[i];
                avg_n_[i] += 1;
                int shown = rssi_[i];
                if (trace_mode_ == TraceMode::Max) shown = peak_hold_on_ ? peak_hold_[i] : rssi_[i];
                if (trace_mode_ == TraceMode::Avg && avg_n_[i] > 0) shown = avg_acc_[i] / avg_n_[i];
                if (diff_on_ && has_baseline_ && i < baseline_.size()) {
                    shown = rssi_[i] - baseline_[i];
                }
                if (rssi_[i] > peak_dbm_) {
                    peak_dbm_ = rssi_[i];
                    peak_idx_ = static_cast<int>(i);
                    peak_hz_ = freqs_[i];
                }
                (void)shown;
                if (rssi_[i] >= settings_.threshold_dbm) over = true;
            }
            if (over) threshold_alert_ = true;

            // Always scroll waterfall history (used when display_mode_ == Waterfall).
            std::memmove(water_.data(), water_.data() + kWaterCols, (kWaterRows - 1) * kWaterCols);
            for (int c = 0; c < kWaterCols; ++c) {
                const size_t idx = freqs_.empty() ? 0 : (c * (freqs_.size() - 1)) / (kWaterCols - 1);
                int v = rssi_[std::min(idx, rssi_.size() - 1)];
                if (diff_on_ && has_baseline_ && idx < baseline_.size()) {
                    const int d = v - baseline_[idx];
                    v = d >= 8 ? settings_.ref_level_dbm : settings_.floor_dbm;
                }
                water_[(kWaterRows - 1) * kWaterCols + c] =
                    static_cast<int8_t>(std::clamp(v, -128, 127));
            }
            data_ready_ = true;
        }
        if (open) radio.close();
        running_ = false;
    });
}

void RfSpectrumScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

void RfSpectrumScreen::pulse_alert() {
    std::string e;
    (void)platform::beep(e);
    int32_t pct = 50;
    if (platform::read_brightness_percent(pct)) {
        (void)platform::write_brightness_percent(std::min(100, pct + 30));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        (void)platform::write_brightness_percent(pct);
    }
}

void RfSpectrumScreen::update_header() {
    if (!peak_lbl_ || !view_lbl_) return;
    char peak[96];
    char view[128];
    if (help_on_) {
        lv_label_set_text(peak_lbl_, "Keys: F/X pan   Z/C zoom   # type MHz");
        lv_label_set_text(view_lbl_, "H line/waterfall  Enter Listen  S find  ?=help");
        return;
    }
    if (entering_freq_) {
        std::snprintf(peak, sizeof(peak), "Type frequency (MHz)");
        std::snprintf(view, sizeof(view), "%s_   Enter set   Esc cancel", freq_entry_.c_str());
        lv_label_set_text(peak_lbl_, peak);
        lv_label_set_text(view_lbl_, view);
        return;
    }
    if (peak_hz_ > 0) {
        std::snprintf(peak, sizeof(peak), "Peak  %s MHz    %d dBm%s",
                      platform::rf_format_mhz(peak_hz_).c_str(), peak_dbm_, demo_ ? "  DEMO" : "");
    } else {
        std::snprintf(peak, sizeof(peak), "Peak  —%s", demo_ ? "  DEMO" : "");
    }
    const double half_mhz = span_hz_ / 2000000.0;
    std::snprintf(view, sizeof(view), "View  %s ±%.2f  %s  band %s  %ums%s",
                  platform::rf_format_mhz(center_hz_).c_str(), half_mhz, display_mode_name(), band_name(),
                  static_cast<unsigned>(last_sweep_ms_), status_extra_.c_str());
    lv_label_set_text(peak_lbl_, peak);
    lv_label_set_text(view_lbl_, view);
}

void RfSpectrumScreen::update_footer() {
    if (!footer_lbl_) return;
    if (help_on_) {
        lv_label_set_text(footer_lbl_, "Softkeys: Back  Zoom-  Listen  Zoom+  Home");
        return;
    }
    if (display_mode_ == DisplayMode::Waterfall) {
        lv_label_set_text(footer_lbl_, "Mode WATERFALL  (5 or H)  Enter=Listen at view");
    } else {
        lv_label_set_text(footer_lbl_, "Mode LINE  (5 or H → waterfall)  Enter=Listen");
    }
}

void RfSpectrumScreen::draw_plot() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const int canvas_h = plot_h_;
    const uint16_t bg = pack565(10, 12, 16);
    const uint16_t ink = pack565(200, 210, 220);
    const uint16_t ink_dim = pack565(110, 120, 130);
    const uint16_t grid = pack565(36, 40, 48);
    const uint16_t thr_col = pack565(255, 90, 90);
    const uint16_t trace = pack565(255, 210, 60);
    const uint16_t hot = pack565(255, 255, 120);
    const uint16_t peak_col = pack565(255, 160, 40);   // peak = orange (informational)
    const uint16_t center_col = pack565(40, 255, 100); // view center = green (fixed)
    const uint16_t fill_lo = pack565(40, 50, 28);

    fill_rect(data, stride_, plot_w_, canvas_h, 0, 0, plot_w_ - 1, canvas_h - 1, bg);

    constexpr int kLeft = 28;
    constexpr int kBottom = 8;
    const bool waterfall_mode = display_mode_ == DisplayMode::Waterfall;
    const int plot_top = 2;
    const int plot_bottom = canvas_h - kBottom - 1;
    const int plot_left = kLeft;
    const int plot_right = plot_w_ - 2;
    const int plot_inner_h = std::max(8, plot_bottom - plot_top);

    std::vector<uint32_t> freqs;
    std::vector<int> rssi;
    std::vector<int> hold;
    std::vector<std::pair<uint32_t, uint32_t>> gaps;
    int peak_idx = -1;
    uint32_t start = 0, stop = 1;
    uint32_t center = center_hz_;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        freqs = freqs_;
        rssi = rssi_;
        hold = peak_hold_;
        gaps = gaps_;
        peak_idx = peak_idx_;
        center = center_hz_;
        if (preset_ == PresetId::Full) {
            start = platform::kCc1101Bands[0].start_hz;
            stop = platform::kCc1101Bands[platform::kCc1101BandCount - 1].stop_hz;
        } else if (preset_ == PresetId::Custom) {
            start = custom_start_;
            stop = custom_stop_;
        } else {
            start = center_hz_ > span_hz_ / 2 ? center_hz_ - span_hz_ / 2 : 0;
            stop = center_hz_ + span_hz_ / 2;
        }
    }
    const double span = std::max(1.0, static_cast<double>(stop - start));

    int floor_db = settings_.floor_dbm;
    int ref_db = settings_.ref_level_dbm;
    if (auto_scale_ && !rssi.empty()) {
        int lo = 0, hi = -120;
        for (int v : rssi) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        floor_db = std::clamp(lo - 5, -120, -40);
        ref_db = std::clamp(std::max(hi + 8, floor_db + 25), floor_db + 20, 0);
    }

    auto x_of = [&](uint32_t hz) -> int {
        return plot_left +
               static_cast<int>((static_cast<double>(hz - start) / span) * (plot_right - plot_left));
    };
    auto y_of = [&](int dbm) -> int {
        float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
        t = std::clamp(t, 0.f, 1.f);
        return plot_bottom - static_cast<int>(t * (plot_inner_h - 1));
    };

    if (waterfall_mode) {
        // Full-canvas waterfall: newest row at bottom.
        const int rows = std::min(kWaterRows, plot_inner_h);
        const int wy0 = plot_bottom - rows + 1;
        draw_text(data, stride_, plot_w_, canvas_h, 1, 1, "wf", ink_dim);
        for (int r = 0; r < rows; ++r) {
            for (int col = 0; col < kWaterCols; ++col) {
                const int x0 = plot_left + col * (plot_right - plot_left) / kWaterCols;
                const int x1 = plot_left + (col + 1) * (plot_right - plot_left) / kWaterCols - 1;
                const int dbm = water_[(kWaterRows - rows + r) * kWaterCols + col];
                fill_rect(data, stride_, plot_w_, canvas_h, x0, wy0 + r, x1, wy0 + r,
                          water_color(dbm, floor_db, ref_db));
            }
        }
    } else {
        // Line spectrum mode.
        for (const auto& g : gaps) {
            hatch_rect(data, stride_, plot_w_, canvas_h, x_of(g.first), plot_top, x_of(g.second), plot_bottom,
                       pack565(50, 40, 40));
        }

        const int db_step = (ref_db - floor_db) > 50 ? 20 : 10;
        for (int db = ((floor_db + db_step - 1) / db_step) * db_step; db <= ref_db; db += db_step) {
            const int y = y_of(db);
            hline(data, stride_, plot_w_, canvas_h, plot_left, y, plot_right, grid);
            char lab[8];
            std::snprintf(lab, sizeof(lab), "%d", db);
            draw_text(data, stride_, plot_w_, canvas_h, 1, y - 2, lab, ink_dim);
        }

        {
            const int y = y_of(settings_.threshold_dbm);
            for (int x = plot_left; x <= plot_right; x += 2)
                put_px(data, stride_, plot_w_, canvas_h, x, y, thr_col);
            draw_text(data, stride_, plot_w_, canvas_h, plot_left + 2, y - 6, "thr", thr_col);
        }

        auto sample_at = [&](size_t i) -> int {
            int v = rssi[i];
            if (trace_mode_ == TraceMode::Max && peak_hold_on_ && i < hold.size()) v = hold[i];
            return v;
        };

        if (freqs.size() >= 2) {
            for (size_t i = 0; i + 1 < freqs.size(); ++i) {
                const int x0 = x_of(freqs[i]);
                const int x1 = x_of(freqs[i + 1]);
                const int d0 = sample_at(i);
                const int d1 = sample_at(i + 1);
                const int y0 = y_of(d0);
                const int y1 = y_of(d1);
                const int mid_y = (y0 + y1) / 2;
                for (int x = x0; x <= x1; ++x) {
                    for (int y = mid_y; y <= plot_bottom; y += 2)
                        put_px(data, stride_, plot_w_, canvas_h, x, y, fill_lo);
                }
                const bool hot_bin = d0 >= settings_.threshold_dbm || d1 >= settings_.threshold_dbm;
                line(data, stride_, plot_w_, canvas_h, x0, y0, x1, y1, hot_bin ? hot : trace);
            }
        }

        if (peak_hold_on_ && hold.size() == freqs.size() && freqs.size() >= 2) {
            const uint16_t ph = pack565(140, 255, 160);
            for (size_t i = 1; i < freqs.size(); ++i) {
                line(data, stride_, plot_w_, canvas_h, x_of(freqs[i - 1]), y_of(hold[i - 1]), x_of(freqs[i]),
                     y_of(hold[i]), ph);
            }
        }

        // Peak marker only when clearly above noise (avoids jittery edge dancing).
        if (show_peak_marker_ && peak_idx >= 0 && peak_idx < static_cast<int>(freqs.size()) &&
            peak_dbm_ >= settings_.threshold_dbm - 5) {
            const int x = x_of(freqs[static_cast<size_t>(peak_idx)]);
            const int y = y_of(rssi[static_cast<size_t>(peak_idx)]);
            for (int yy = plot_top; yy <= plot_bottom; yy += 2)
                put_px(data, stride_, plot_w_, canvas_h, x, yy, peak_col);
            for (int dx = -3; dx <= 3; ++dx) {
                put_px(data, stride_, plot_w_, canvas_h, x + dx, y, peak_col);
            }
        }
    }

    // Fixed view-center marker (user-controlled frequency) — both modes.
    {
        const int cx = x_of(center);
        vline(data, stride_, plot_w_, canvas_h, cx, plot_top, plot_bottom, center_col);
    }

    for (const auto& m : user_markers_) {
        if (!m.set) continue;
        const int x = x_of(m.hz);
        for (int y = plot_top; y <= plot_bottom; y += 2)
            put_px(data, stride_, plot_w_, canvas_h, x, y, pack565(255, 70, 70));
    }

    hline(data, stride_, plot_w_, canvas_h, plot_left, plot_bottom, plot_right, ink_dim);
    char a[12], b[12], c[12];
    std::snprintf(a, sizeof(a), "%.2f", start / 1000000.0);
    std::snprintf(b, sizeof(b), "%.2f", center / 1000000.0);
    std::snprintf(c, sizeof(c), "%.2f", stop / 1000000.0);
    const int ylab = plot_bottom + 2;
    draw_text(data, stride_, plot_w_, canvas_h, plot_left, ylab, a, ink);
    draw_text(data, stride_, plot_w_, canvas_h, std::clamp(x_of(center) - 12, plot_left, plot_right - 24), ylab,
              b, center_col);
    draw_text(data, stride_, plot_w_, canvas_h, plot_right - 28, ylab, c, ink);

    if (canvas_) lv_obj_invalidate(canvas_);
}

void RfSpectrumScreen::cycle_trace_mode() {
    if (trace_mode_ == TraceMode::Live) trace_mode_ = TraceMode::Max;
    else if (trace_mode_ == TraceMode::Max) trace_mode_ = TraceMode::Avg;
    else trace_mode_ = TraceMode::Live;
    update_header();
}

void RfSpectrumScreen::place_user_marker() {
    if (peak_idx_ < 0 || freqs_.empty()) return;
    auto& m = user_markers_[static_cast<size_t>(next_marker_ % 3)];
    m.set = true;
    m.idx = peak_idx_;
    m.hz = peak_hz_;
    m.dbm = peak_dbm_;
    next_marker_ = (next_marker_ + 1) % 3;
    draw_plot();
}

void RfSpectrumScreen::clear_user_markers() {
    for (auto& m : user_markers_) m.set = false;
    next_marker_ = 0;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        std::fill(peak_hold_.begin(), peak_hold_.end(), -120);
        std::fill(avg_acc_.begin(), avg_acc_.end(), 0);
        std::fill(avg_n_.begin(), avg_n_.end(), 0);
    }
    draw_plot();
}

void RfSpectrumScreen::open_peak_menu() {
    handoff_center(model::ToolId::RfListen);
}

void RfSpectrumScreen::handoff_peak(model::ToolId tool) {
    // Explicit peak handoff (E/W still jump to loudest bin when one exists).
    if (peak_hz_ == 0) {
        handoff_center(tool);
        return;
    }
    settings_.freq_hz = peak_hz_;
    platform::rf_push_recent(settings_, peak_hz_);
    platform::save_rf_settings(settings_);
    view_model().open_tool(tool);
}

void RfSpectrumScreen::handoff_center(model::ToolId tool) {
    settings_.freq_hz = center_hz_;
    platform::rf_push_recent(settings_, center_hz_);
    platform::save_rf_settings(settings_);
    view_model().open_tool(tool);
}

void RfSpectrumScreen::apply_freq_entry() {
    if (freq_entry_.empty()) {
        entering_freq_ = false;
        return;
    }
    const double mhz = std::strtod(freq_entry_.c_str(), nullptr);
    if (mhz < 280.0 || mhz > 928.0) {
        status_extra_ = " bad MHz";
        entering_freq_ = false;
        return;
    }
    center_hz_ = static_cast<uint32_t>(mhz * 1000000.0);
    settings_.freq_hz = center_hz_;
    platform::save_rf_settings(settings_);
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        rebuild_freqs();
    }
    entering_freq_ = false;
    status_extra_ = " tuned";
}

void RfSpectrumScreen::capture_baseline() {
    std::lock_guard<std::mutex> lock(data_mu_);
    baseline_ = rssi_;
    has_baseline_ = !baseline_.empty();
    status_extra_ = has_baseline_ ? " baseline" : "";
}

void RfSpectrumScreen::toggle_diff() {
    if (!has_baseline_) {
        status_extra_ = " no baseline";
        return;
    }
    diff_on_ = !diff_on_;
    status_extra_ = diff_on_ ? " DIFF" : "";
}

void RfSpectrumScreen::find_strongest() {
    status_extra_ = " scanning…";
    stop_worker();
    cancel_ = false;
    std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        const bool open = radio.open({}, err);
        struct Band {
            uint32_t start, stop;
        };
        const Band bands[] = {
            {314000000u, 316000000u},
            {433000000u, 434800000u},
            {868000000u, 868600000u},
            {915000000u, 916000000u},
        };
        int best_dbm = -120;
        uint32_t best_hz = center_hz_;
        for (const auto& b : bands) {
            if (cancel_) break;
            std::vector<uint32_t> freqs;
            uint32_t step = 25000;
            if (open) {
                (void)platform::cc1101_plan_sweep(b.start, b.stop, step, 40, freqs, err);
                std::vector<int> rssi;
                if (radio.spectrum_sweep(freqs, rssi, err, &cancel_)) {
                    for (size_t i = 0; i < freqs.size() && i < rssi.size(); ++i) {
                        if (rssi[i] > best_dbm) {
                            best_dbm = rssi[i];
                            best_hz = freqs[i];
                        }
                    }
                }
            } else {
                // Demo: pick mid of 433.
                best_hz = 433920000;
                best_dbm = -40;
                break;
            }
        }
        if (open) radio.close();
        center_hz_ = best_hz;
        peak_hz_ = best_hz;
        peak_dbm_ = best_dbm;
        settings_.freq_hz = best_hz;
        platform::rf_push_recent(settings_, best_hz);
        platform::save_rf_settings(settings_);
        {
            std::lock_guard<std::mutex> lock(data_mu_);
            rebuild_freqs();
        }
        char tmp[48];
        std::snprintf(tmp, sizeof(tmp), " LOCK %s/%d", platform::rf_format_mhz(best_hz).c_str(), best_dbm);
        status_extra_ = tmp;
        cancel_ = false;
        start_worker();
    }).detach();
}

bool RfSpectrumScreen::handle_key(uint32_t key) {
    if (entering_freq_) {
        if (key == LV_KEY_ESC) {
            entering_freq_ = false;
            return true;
        }
        if (key == LV_KEY_ENTER) {
            apply_freq_entry();
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == 8) {
            if (!freq_entry_.empty()) freq_entry_.pop_back();
            return true;
        }
        if ((key >= '0' && key <= '9') || key == '.') {
            if (freq_entry_.size() < 10) freq_entry_.push_back(static_cast<char>(key));
            return true;
        }
        return true;
    }
    if (key == LV_KEY_ESC) {
        stop_worker();
        settings_.freq_hz = center_hz_;
        settings_.step_hz = step_hz_;
        platform::save_rf_settings(settings_);
        return KvxScreen::handle_key(key);
    }
    if (key == '?' ) {
        help_on_ = !help_on_;
        update_header();
        update_footer();
        return true;
    }
    if (key == 'a' || key == 'A') {
        auto_scale_ = !auto_scale_;
        status_extra_ = auto_scale_ ? " autoY" : " fixedY";
        update_header();
        return true;
    }
    if (key == 'h' || key == 'H') {
        cycle_display_mode();
        return true;
    }
    if (key == '#') {
        entering_freq_ = true;
        freq_entry_.clear();
        return true;
    }
    if (key == 'e' || key == 'E') {
        handoff_peak(model::ToolId::RfRecord);
        return true;
    }
    if (key == 'w' || key == 'W') {
        handoff_peak(model::ToolId::RfReplay);
        return true;
    }
    if (key == 's' || key == 'S') {
        find_strongest();
        return true;
    }
    if (key == 'b' || key == 'B') {
        capture_baseline();
        return true;
    }
    if (key == 'd' || key == 'D') {
        toggle_diff();
        return true;
    }
    // Pan: F/X → LEFT/RIGHT (or LV keys)
    if (key == LV_KEY_LEFT || key == 'f' || key == 'F') {
        center_hz_ = center_hz_ > span_hz_ / 8 ? center_hz_ - span_hz_ / 8 : center_hz_;
        if (preset_ != PresetId::Full && preset_ != PresetId::Custom) {
            std::lock_guard<std::mutex> lock(data_mu_);
            rebuild_freqs();
        }
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'x' || key == 'X') {
        center_hz_ += span_hz_ / 8;
        if (preset_ != PresetId::Full && preset_ != PresetId::Custom) {
            std::lock_guard<std::mutex> lock(data_mu_);
            rebuild_freqs();
        }
        return true;
    }
    // Zoom span: Z/C → UP/DOWN
    if (key == LV_KEY_UP || key == 'z' || key == 'Z') {
        span_hz_ = std::max(200000u, span_hz_ / 2);
        std::lock_guard<std::mutex> lock(data_mu_);
        rebuild_freqs();
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'c' || key == 'C') {
        span_hz_ = std::min(628000000u, span_hz_ * 2);
        std::lock_guard<std::mutex> lock(data_mu_);
        rebuild_freqs();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        open_peak_menu();
        return true;
    }
    if (key == 'p' || key == 'P') {
        peak_hold_on_ = !peak_hold_on_;
        show_peak_marker_ = true;
        return true;
    }
    if (key == 'k' || key == 'K') {
        show_peak_marker_ = !show_peak_marker_;
        status_extra_ = show_peak_marker_ ? " peakON" : " peakOFF";
        update_header();
        draw_plot();
        return true;
    }
    if (key == 'm' || key == 'M') {
        place_user_marker();
        return true;
    }
    if (key == 'r' || key == 'R') {
        clear_user_markers();
        return true;
    }
    if (key == 't' || key == 'T') {
        cycle_trace_mode();
        return true;
    }
    if (key == '[') {
        settings_.threshold_dbm = std::max(-110, settings_.threshold_dbm - 2);
        platform::save_rf_settings(settings_);
        return true;
    }
    if (key == ']') {
        settings_.threshold_dbm = std::min(-10, settings_.threshold_dbm + 2);
        platform::save_rf_settings(settings_);
        return true;
    }
    if (key == '-' || key == '_') {
        settings_.ref_level_dbm = std::max(-40, settings_.ref_level_dbm - 5);
        platform::save_rf_settings(settings_);
        return true;
    }
    if (key == '=' || key == '+') {
        settings_.ref_level_dbm = std::min(0, settings_.ref_level_dbm + 5);
        platform::save_rf_settings(settings_);
        return true;
    }
    // Presets 1–6
    if (key == '1') {
        std::lock_guard<std::mutex> lock(data_mu_);
        apply_preset(PresetId::Band315);
        return true;
    }
    if (key == '2') {
        std::lock_guard<std::mutex> lock(data_mu_);
        apply_preset(PresetId::Band433);
        return true;
    }
    if (key == '3') {
        std::lock_guard<std::mutex> lock(data_mu_);
        apply_preset(PresetId::Band868);
        return true;
    }
    if (key == '4') {
        // action bar uses 4 — also allow preset via Fn path; here skip
    }
    if (key == '9') {
        std::lock_guard<std::mutex> lock(data_mu_);
        apply_preset(PresetId::Band915);
        return true;
    }
    if (key == '0') {
        std::lock_guard<std::mutex> lock(data_mu_);
        apply_preset(PresetId::Full);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfSpectrumScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        // Softkey 5: toggle line ↔ waterfall (also key H).
        {view::ICON_CH_RF, [this]() { cycle_display_mode(); }},
        {view::ICON_CHECK, [this]() { open_peak_menu(); }},
        {view::ICON_PLUS,
         [this]() {
             span_hz_ = std::max(200000u, span_hz_ / 2);
             std::lock_guard<std::mutex> lock(data_mu_);
             rebuild_freqs();
         }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

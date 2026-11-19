#include "rf_spectrum_screen.h"
#include "asset_manager.h"

#include "audio.h"
#include "backlight.h"
#include "cap_detect.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace screen {
namespace {

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t amp_color(float t) {
    // yellow → green
    t = std::clamp(t, 0.f, 1.f);
    const uint8_t r = static_cast<uint8_t>(255 * (1.f - t * 0.85f));
    const uint8_t g = static_cast<uint8_t>(180 + 75 * t);
    const uint8_t b = static_cast<uint8_t>(20 + 40 * (1.f - t));
    return pack565(r, g, b);
}

uint16_t water_color(int dbm, int floor_db, int ref_db) {
    float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
    t = std::clamp(t, 0.f, 1.f);
    const uint8_t r = static_cast<uint8_t>(20 + 200 * t);
    const uint8_t g = static_cast<uint8_t>(10 + 40 * t);
    const uint8_t b = static_cast<uint8_t>(40 + 180 * (1.f - t * 0.5f));
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

} // namespace

RfSpectrumScreen::RfSpectrumScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    settings_ = platform::load_rf_settings();
    center_hz_ = settings_.freq_hz;
    step_hz_ = settings_.step_hz;
    apply_preset(PresetId::Band433);
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

    header_lbl_ = lv_label_create(root);
    lv_label_set_text(header_lbl_, "…");
    auto* font = assets().load_standard_font(10);
    lv_obj_set_style_text_font(header_lbl_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(header_lbl_, tok.ink, 0);
    lv_obj_set_pos(header_lbl_, 4, 20);
    lv_obj_set_width(header_lbl_, 312);
    lv_label_set_long_mode(header_lbl_, LV_LABEL_LONG_CLIP);

    plot_w_ = 320;
    water_h_ = 0;
    plot_h_ = view::kContentHeight - 38;
    const int canvas_h = plot_h_;
    draw_ = lv_draw_buf_create(plot_w_, canvas_h, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 0, 36);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    const auto cap = platform::detect_cc1101();
    cap_ok_ = cap.present;
    if (!cap_ok_) {
        cap_error_ = cap.error.empty() ? "Cap not present" : cap.error;
        demo_ = true;
    }
    rebuild_freqs();
    update_header();
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
        draw_plot();
        if (threshold_alert_) {
            threshold_alert_ = false;
            pulse_alert();
        }
    }
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
            show_waterfall_ = ms > 0 && ms < 200 && plot_h_ > 70;
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
                if (shown > peak_dbm_) {
                    peak_dbm_ = shown;
                    peak_idx_ = static_cast<int>(i);
                    peak_hz_ = freqs_[i];
                }
                if (rssi_[i] >= settings_.threshold_dbm) over = true;
            }
            if (over) threshold_alert_ = true;

            if (show_waterfall_) {
                // Scroll water up one row.
                std::memmove(water_.data(), water_.data() + kWaterCols, (kWaterRows - 1) * kWaterCols);
                for (int c = 0; c < kWaterCols; ++c) {
                    const size_t idx = freqs_.empty() ? 0 : (c * (freqs_.size() - 1)) / (kWaterCols - 1);
                    water_[(kWaterRows - 1) * kWaterCols + c] =
                        static_cast<int8_t>(std::clamp(rssi_[std::min(idx, rssi_.size() - 1)], -128, 127));
                }
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
    if (!header_lbl_) return;
    char buf[160];
    const char* mode = "live";
    if (trace_mode_ == TraceMode::Max) mode = "max";
    if (trace_mode_ == TraceMode::Avg) mode = "avg";
    std::snprintf(buf, sizeof(buf), "%s MHz  span %s  step %uk  %ums  pk %s/%ddBm %s%s",
                  platform::rf_format_mhz(center_hz_).c_str(), platform::rf_format_mhz(span_hz_).c_str(),
                  static_cast<unsigned>(step_hz_ / 1000), static_cast<unsigned>(last_sweep_ms_),
                  platform::rf_format_mhz(peak_hz_).c_str(), peak_dbm_, mode, demo_ ? " DEMO" : "");
    lv_label_set_text(header_lbl_, buf);
}

void RfSpectrumScreen::draw_plot() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const int canvas_h = plot_h_;
    const uint16_t bg = pack565(8, 10, 14);
    fill_rect(data, stride_, plot_w_, canvas_h, 0, 0, plot_w_ - 1, canvas_h - 1, bg);

    const int water = show_waterfall_ ? 26 : 0;
    const int graph_h = canvas_h - water - 2;
    const int base_y = graph_h - 1;
    const int floor_db = settings_.floor_dbm;
    const int ref_db = settings_.ref_level_dbm;

    std::vector<uint32_t> freqs;
    std::vector<int> rssi;
    std::vector<int> hold;
    std::vector<std::pair<uint32_t, uint32_t>> gaps;
    int peak_idx = -1;
    uint32_t start = 0, stop = 1;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        freqs = freqs_;
        rssi = rssi_;
        hold = peak_hold_;
        gaps = gaps_;
        peak_idx = peak_idx_;
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

    auto x_of = [&](uint32_t hz) -> int {
        return static_cast<int>((static_cast<double>(hz - start) / span) * (plot_w_ - 1));
    };
    auto y_of = [&](int dbm) -> int {
        float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
        t = std::clamp(t, 0.f, 1.f);
        return base_y - static_cast<int>(t * (graph_h - 4));
    };

    // Hatch unsupported gaps.
    const uint16_t hatch = pack565(40, 42, 48);
    for (const auto& g : gaps) {
        hatch_rect(data, stride_, plot_w_, canvas_h, x_of(g.first), 0, x_of(g.second), base_y, hatch);
    }

    // Horizontal dB grid.
    const uint16_t grid = pack565(28, 32, 40);
    for (int db = floor_db; db <= ref_db; db += 20) {
        const int y = y_of(db);
        for (int x = 0; x < plot_w_; x += 2) put_px(data, stride_, plot_w_, canvas_h, x, y, grid);
    }

    // Threshold line.
    {
        const int y = y_of(settings_.threshold_dbm);
        const uint16_t th = pack565(255, 80, 80);
        for (int x = 0; x < plot_w_; ++x) put_px(data, stride_, plot_w_, canvas_h, x, y, th);
    }

    auto sample_at = [&](size_t i) -> int {
        int v = rssi[i];
        if (trace_mode_ == TraceMode::Max && peak_hold_on_) v = hold[i];
        if (trace_mode_ == TraceMode::Avg) {
            // approximate from current rssi (avg maintained in worker; use rssi here)
            v = rssi[i];
        }
        return v;
    };

    // Filled spectrum.
    if (freqs.size() >= 2) {
        for (size_t i = 0; i < freqs.size(); ++i) {
            const int x = x_of(freqs[i]);
            const int dbm = sample_at(i);
            const int y = y_of(dbm);
            float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
            const uint16_t col = amp_color(std::clamp(t, 0.f, 1.f));
            // Highlight above threshold.
            const uint16_t use = (dbm >= settings_.threshold_dbm) ? pack565(255, 220, 40) : col;
            for (int yy = y; yy <= base_y; ++yy) put_px(data, stride_, plot_w_, canvas_h, x, yy, use);
            if (i + 1 < freqs.size()) {
                const int x2 = x_of(freqs[i + 1]);
                for (int xx = x; xx < x2; ++xx) {
                    for (int yy = y; yy <= base_y; ++yy) put_px(data, stride_, plot_w_, canvas_h, xx, yy, use);
                }
            }
        }
    }

    // Peak hold outline.
    if (peak_hold_on_ && hold.size() == freqs.size()) {
        const uint16_t ph = pack565(180, 255, 180);
        for (size_t i = 1; i < freqs.size(); ++i) {
            const int x0 = x_of(freqs[i - 1]);
            const int x1 = x_of(freqs[i]);
            const int y0 = y_of(hold[i - 1]);
            const int y1 = y_of(hold[i]);
            // simple line
            const int steps = std::max(1, std::abs(x1 - x0));
            for (int s = 0; s <= steps; ++s) {
                const int x = x0 + (x1 - x0) * s / steps;
                const int y = y0 + (y1 - y0) * s / steps;
                put_px(data, stride_, plot_w_, canvas_h, x, y, ph);
            }
        }
    }

    // Green auto peak marker.
    if (peak_idx >= 0 && peak_idx < static_cast<int>(freqs.size())) {
        const int x = x_of(freqs[peak_idx]);
        const uint16_t g = pack565(40, 255, 80);
        for (int y = 0; y <= base_y; ++y) put_px(data, stride_, plot_w_, canvas_h, x, y, g);
    }

    // Red user markers.
    const uint16_t red = pack565(255, 60, 60);
    for (const auto& m : user_markers_) {
        if (!m.set) continue;
        const int x = x_of(m.hz);
        for (int y = 0; y <= base_y; y += 2) put_px(data, stride_, plot_w_, canvas_h, x, y, red);
    }

    // Waterfall strip.
    if (water > 0) {
        for (int r = 0; r < kWaterRows && r < water; ++r) {
            for (int c = 0; c < kWaterCols; ++c) {
                const int x0 = c * plot_w_ / kWaterCols;
                const int x1 = (c + 1) * plot_w_ / kWaterCols - 1;
                const int y = graph_h + r;
                const int dbm = water_[r * kWaterCols + c];
                const uint16_t col = water_color(dbm, floor_db, ref_db);
                fill_rect(data, stride_, plot_w_, canvas_h, x0, y, x1, y, col);
            }
        }
    }

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
    if (peak_hz_ == 0) return;
    settings_.freq_hz = peak_hz_;
    platform::rf_push_recent(settings_, peak_hz_);
    platform::save_rf_settings(settings_);
    // Navigate to Listen with shared freq.
    view_model().open_tool(model::ToolId::RfListen);
}

bool RfSpectrumScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_worker();
        settings_.freq_hz = center_hz_;
        settings_.step_hz = step_hz_;
        platform::save_rf_settings(settings_);
        return KvxScreen::handle_key(key);
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
        {view::ICON_MINUS,
         [this]() {
             span_hz_ = std::min(628000000u, span_hz_ * 2);
             std::lock_guard<std::mutex> lock(data_mu_);
             rebuild_freqs();
         }},
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

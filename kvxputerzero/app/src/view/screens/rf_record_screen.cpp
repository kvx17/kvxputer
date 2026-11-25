#include "rf_record_screen.h"
#include "asset_manager.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "cc1101.h"
#include "rf_decode.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace screen {
namespace {

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
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
        put_px(data, stride, w, h, x0 + (x1 - x0) * s / steps, y0 + (y1 - y0) * s / steps, c);
    }
}

} // namespace

RfRecordScreen::RfRecordScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets)
    : KvxScreen(vm, assets) {
    settings_ = platform::load_rf_settings();
    status_ = "Enter=arm  Esc=back";
    init();
}

RfRecordScreen::~RfRecordScreen() {
    stop_capture();
    stop_idle_rssi();
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void RfRecordScreen::stop_idle_rssi() {
    idle_cancel_ = true;
    if (idle_rssi_.joinable()) idle_rssi_.join();
}

void RfRecordScreen::start_idle_rssi() {
    stop_idle_rssi();
    idle_cancel_ = false;
    idle_rssi_ = std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        const auto cap = platform::detect_cc1101();
        bool open = false;
        if (cap.present) {
            open = radio.open({}, err);
            if (open) {
                (void)radio.apply_preset(platform::Cc1101Preset::OokAsync, err);
                (void)radio.set_frequency_hz(settings_.freq_hz, err, true);
            }
        }
        float ph = 0.f;
        while (!idle_cancel_) {
            if (phase_ == Phase::Armed || phase_ == Phase::Capturing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            int dbm = -120;
            if (open) {
                (void)radio.read_rssi_dbm(dbm, err);
            } else {
                ph += 0.25f;
                dbm = static_cast<int>(-85 + 12 * std::sin(ph));
            }
            live_rssi_ = dbm;
            peak_rssi_ = std::max(peak_rssi_, dbm);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        if (open) radio.close();
    });
}

void RfRecordScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "RF Record RAW");

    auto* font = assets().load_standard_font(11);
    auto* font_sm = assets().load_standard_font(10);
    const lv_font_t* use = font ? font : &lv_font_montserrat_12;
    const lv_font_t* use_sm = font_sm ? font_sm : &lv_font_montserrat_12;

    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 4, 18);
    lv_obj_set_width(label_, 312);
    lv_obj_set_style_text_font(label_, use, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);

    footer_lbl_ = lv_label_create(root);
    lv_obj_set_pos(footer_lbl_, 4, view::kContentHeight - 12);
    lv_obj_set_width(footer_lbl_, 312);
    lv_obj_set_style_text_font(footer_lbl_, use_sm, 0);
    lv_obj_set_style_text_color(footer_lbl_, tok.ink_dim, 0);
    lv_label_set_long_mode(footer_lbl_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(footer_lbl_, "Soft: Back  Trim-  Arm/Save  Trim+  Home");

    // Fill everything between status text and footer — was a tiny unset canvas before.
    constexpr int kLabelH = 36;
    constexpr int kFooterH = 12;
    plot_top_y_ = 18 + kLabelH;
    plot_w_ = 312;
    plot_h_ = std::max(48, view::kContentHeight - plot_top_y_ - kFooterH - 2);

    draw_ = lv_draw_buf_create(plot_w_, plot_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, plot_w_, plot_h_);
        lv_obj_set_pos(canvas_, 4, plot_top_y_);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
        clear_canvas(pack565(10, 12, 16));
    }
    start_idle_rssi();
    refresh();
    timer_ = lv_timer_create(timer_cb, 100, this);
}

void RfRecordScreen::clear_canvas(uint16_t color) {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    for (int y = 0; y < plot_h_; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(data + y * stride_);
        for (int x = 0; x < plot_w_; ++x) row[x] = color;
    }
    if (canvas_) lv_obj_invalidate(canvas_);
}

void RfRecordScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfRecordScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void RfRecordScreen::refresh() {
    if (!label_) return;
    char buf[320];
    if (help_on_) {
        std::snprintf(buf, sizeof(buf),
                      "Enter arm/save  Esc back\nZ/C trim  [ ] thr  ?=help");
        lv_label_set_text(label_, buf);
        if (footer_lbl_) lv_label_set_text(footer_lbl_, "Soft: Back  Trim-  Arm/Save  Trim+  Home");
        return;
    }
    if (phase_ == Phase::Armed) {
        std::snprintf(buf, sizeof(buf), "%s MHz  thr %d  arm %ds\nRSSI %d  peak %d  %s",
                      platform::rf_format_mhz(settings_.freq_hz).c_str(), settings_.threshold_dbm,
                      arm_left_s_, live_rssi_, peak_rssi_, status_.c_str());
    } else if (phase_ == Phase::Capturing) {
        std::snprintf(buf, sizeof(buf), "%s MHz  CAPTURING\nRSSI %d  peak %d  edges…",
                      platform::rf_format_mhz(settings_.freq_hz).c_str(), live_rssi_, peak_rssi_);
    } else if (phase_ == Phase::Preview) {
        std::snprintf(buf, sizeof(buf), "%s MHz  preview [%d..%d]/%zu\npeak %d dBm  Enter=save%s",
                      platform::rf_format_mhz(settings_.freq_hz).c_str(), trim_lo_, trim_hi_,
                      timings_.size(), peak_rssi_,
                      rssi_archive_ ? "  [archive]" : "");
    } else {
        std::snprintf(buf, sizeof(buf), "%s MHz  thr %d  max %dms\n%s  peak %d",
                      platform::rf_format_mhz(settings_.freq_hz).c_str(), settings_.threshold_dbm,
                      settings_.record_max_ms, status_.c_str(), peak_rssi_);
    }
    lv_label_set_text(label_, buf);

    // Keep a live RSSI sample even when idle (demo/noise floor).
    if (phase_ != Phase::Preview) {
        rssi_hist_.push_back(live_rssi_);
        while (rssi_hist_.size() > kHistMax) rssi_hist_.pop_front();
    }
    draw_scope();
}

void RfRecordScreen::draw_scope() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(10, 12, 16);
    const uint16_t grid = pack565(36, 40, 48);
    const uint16_t ink = pack565(180, 190, 200);
    const uint16_t thr_col = pack565(255, 90, 90);
    const uint16_t trace = pack565(255, 210, 60);
    const uint16_t mark = pack565(80, 255, 100);
    const uint16_t space = pack565(40, 60, 80);
    const uint16_t fill = pack565(30, 80, 50);

    clear_canvas(bg);

    constexpr int kLeft = 2;
    constexpr int kBottom = 2;
    const int plot_top = 2;
    const int plot_bottom = plot_h_ - kBottom - 1;
    const int plot_left = kLeft;
    const int plot_right = plot_w_ - 3;
    const int inner_h = std::max(8, plot_bottom - plot_top);

    if (phase_ == Phase::Preview && !timings_.empty()) {
        // Full-width OOK pulse preview.
        const int lo = std::clamp(trim_lo_, 0, static_cast<int>(timings_.size()));
        const int hi = std::clamp(trim_hi_, lo, static_cast<int>(timings_.size()));
        int64_t total = 0;
        for (int i = lo; i < hi; ++i) total += std::abs(timings_[i]);
        if (total <= 0) total = 1;
        int64_t acc = 0;
        const int mid = (plot_top + plot_bottom) / 2;
        hline(data, stride_, plot_w_, plot_h_, plot_left, mid, plot_right, grid);
        for (int i = lo; i < hi; ++i) {
            const int64_t w = std::abs(timings_[i]);
            const int x0 = plot_left + static_cast<int>(acc * (plot_right - plot_left) / total);
            acc += w;
            const int x1 = plot_left + static_cast<int>(acc * (plot_right - plot_left) / total);
            const bool high = timings_[i] > 0;
            const int y0 = high ? plot_top + 4 : mid;
            const int y1 = high ? mid : plot_bottom - 2;
            const uint16_t c = high ? mark : space;
            for (int x = x0; x <= x1 && x <= plot_right; ++x) {
                for (int y = y0; y <= y1; ++y) put_px(data, stride_, plot_w_, plot_h_, x, y, c);
            }
        }
        if (canvas_) lv_obj_invalidate(canvas_);
        return;
    }

    // Live RSSI scope (Idle / Armed / Capturing) — full canvas.
    const int floor_db = -110;
    const int ref_db = -20;
    auto y_of = [&](int dbm) -> int {
        float t = (static_cast<float>(dbm - floor_db)) / static_cast<float>(ref_db - floor_db);
        t = std::clamp(t, 0.f, 1.f);
        return plot_bottom - static_cast<int>(t * (inner_h - 1));
    };

    hline(data, stride_, plot_w_, plot_h_, plot_left, plot_bottom, plot_right, grid);
    const int thr_y = y_of(settings_.threshold_dbm);
    for (int x = plot_left; x <= plot_right; x += 2)
        put_px(data, stride_, plot_w_, plot_h_, x, thr_y, thr_col);

    if (rssi_hist_.size() >= 2) {
        const size_t n = rssi_hist_.size();
        for (size_t i = 1; i < n; ++i) {
            const int x0 = plot_left + static_cast<int>((i - 1) * (plot_right - plot_left) / (n - 1));
            const int x1 = plot_left + static_cast<int>(i * (plot_right - plot_left) / (n - 1));
            const int y0 = y_of(rssi_hist_[i - 1]);
            const int y1 = y_of(rssi_hist_[i]);
            line(data, stride_, plot_w_, plot_h_, x0, y0, x1, y1, trace);
            // Soft fill under latest segment when armed/capturing.
            if (phase_ == Phase::Armed || phase_ == Phase::Capturing) {
                const int mid_y = (y0 + y1) / 2;
                for (int x = x0; x <= x1; ++x)
                    for (int y = mid_y; y <= plot_bottom; y += 2)
                        put_px(data, stride_, plot_w_, plot_h_, x, y, fill);
            }
        }
    } else {
        // Empty hint line.
        hline(data, stride_, plot_w_, plot_h_, plot_left, (plot_top + plot_bottom) / 2, plot_right,
              pack565(50, 55, 60));
    }

    // Live level marker on the right edge.
    const int ly = y_of(live_rssi_);
    vline(data, stride_, plot_w_, plot_h_, plot_right, ly - 3, ly + 3, mark);
    put_px(data, stride_, plot_w_, plot_h_, plot_right - 1, ly, mark);

    (void)ink;
    if (canvas_) lv_obj_invalidate(canvas_);
}

void RfRecordScreen::arm_or_capture() {
    if (phase_ == Phase::Preview) {
        save_file();
        return;
    }
    stop_capture();
    const auto cap = platform::detect_cc1101();
    if (!cap.present) {
        status_ = "Cap not present";
        // Still show a demo RSSI wiggle so the scope isn't dead on desktop.
        live_rssi_ = -70;
        refresh();
        return;
    }
    phase_ = Phase::Armed;
    status_ = "Armed (waiting)…";
    cancel_ = false;
    timings_.clear();
    peak_rssi_ = -120;
    live_rssi_ = -120;
    arm_left_s_ = 30;
    rssi_archive_ = false;
    rssi_hist_.clear();
    worker_ = std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        if (!radio.open({}, err)) {
            status_ = err;
            phase_ = Phase::Idle;
            return;
        }
        (void)radio.apply_preset(settings_.preset == platform::Cc1101Preset::Fsk2
                                     ? platform::Cc1101Preset::OokAsync
                                     : settings_.preset,
                                 err);
        (void)radio.set_frequency_hz(settings_.freq_hz, err, true);
        if (!platform::request_cc1101_gdo0_edges(err)) {
            status_ = err.empty() ? "GDO0 edges failed" : err;
            phase_ = Phase::Idle;
            radio.close();
            return;
        }

        std::deque<platform::GpioEdgeEvent> pre;
        std::vector<platform::GpioEdgeEvent> edges;

        const auto deadline_arm = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        bool triggered = false;
        while (!cancel_ && std::chrono::steady_clock::now() < deadline_arm) {
            const auto left = std::chrono::duration_cast<std::chrono::seconds>(
                deadline_arm - std::chrono::steady_clock::now());
            arm_left_s_ = static_cast<int>(std::max<int64_t>(0, left.count()));
            int dbm = -120;
            (void)radio.read_rssi_dbm(dbm, err);
            live_rssi_ = dbm;
            peak_rssi_ = std::max(peak_rssi_, dbm);
            std::vector<platform::GpioEdgeEvent> ev;
            (void)platform::read_cc1101_gdo0_edges(ev, err);
            for (auto& e : ev) {
                pre.push_back(e);
                while (pre.size() > 64) pre.pop_front();
            }
            if (dbm >= settings_.threshold_dbm) {
                triggered = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!triggered || cancel_) {
            status_ = cancel_ ? "Cancelled" : "Timeout";
            phase_ = Phase::Idle;
            platform::release_cc1101_gdo0();
            radio.close();
            return;
        }
        phase_ = Phase::Capturing;
        status_ = "Capturing…";
        edges.assign(pre.begin(), pre.end());
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(settings_.record_max_ms);
        while (!cancel_ && std::chrono::steady_clock::now() < end) {
            std::vector<platform::GpioEdgeEvent> ev;
            (void)platform::read_cc1101_gdo0_edges(ev, err);
            for (auto& e : ev) edges.push_back(e);
            int dbm = -120;
            (void)radio.read_rssi_dbm(dbm, err);
            live_rssi_ = dbm;
            peak_rssi_ = std::max(peak_rssi_, dbm);
            if (edges.size() > 8) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                std::vector<platform::GpioEdgeEvent> more;
                (void)platform::read_cc1101_gdo0_edges(more, err);
                if (more.empty()) break;
                for (auto& e : more) edges.push_back(e);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        platform::release_cc1101_gdo0();
        radio.close();

        std::vector<int> timings;
        if (edges.size() >= 2) {
            for (size_t i = 1; i < edges.size(); ++i) {
                const int64_t dt =
                    static_cast<int64_t>(edges[i].timestamp_ns - edges[i - 1].timestamp_ns) / 1000;
                if (dt <= 0 || dt > 500000) continue;
                const int signed_us = edges[i - 1].rising ? static_cast<int>(dt) : -static_cast<int>(dt);
                if (!timings.empty() && ((timings.back() > 0) == (signed_us > 0))) timings.back() += signed_us;
                else timings.push_back(signed_us);
            }
        }
        if (timings.size() < 4) {
            status_ = "Few edges — RSSI archive (not replayable)";
            rssi_archive_ = true;
            std::vector<int> samples;
            platform::Cc1101Session r2;
            if (r2.open({}, err)) {
                r2.set_frequency_hz(settings_.freq_hz, err, true);
                r2.listen_rssi(100, 5, samples, err, &cancel_);
                r2.close();
            }
            for (int v : samples) {
                timings.push_back(v > settings_.threshold_dbm ? 400 : 100);
                timings.push_back(-200);
            }
        }
        timings_ = std::move(timings);
        trim_lo_ = 0;
        trim_hi_ = static_cast<int>(timings_.size());
        phase_ = Phase::Preview;
        if (!rssi_archive_) status_ = "Preview — Enter save";
    });
}

void RfRecordScreen::stop_capture() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

void RfRecordScreen::save_file() {
    if (timings_.empty()) {
        status_ = "Nothing to save";
        return;
    }
    const int lo = std::clamp(trim_lo_, 0, static_cast<int>(timings_.size()));
    const int hi = std::clamp(trim_hi_, lo, static_cast<int>(timings_.size()));
    std::vector<int> slice(timings_.begin() + lo, timings_.begin() + hi);
    const char* mod = settings_.preset == platform::Cc1101Preset::Fsk2 ? "FSK" : "OOK";
    const std::string name = platform::rf_auto_name(settings_.freq_hz, mod);
    const std::string path = platform::rf_dir() + "/" + name;
    std::ostringstream comment;
    comment << "# RSSI_peak: " << peak_rssi_ << "\n";
    comment << "# Modulation: " << mod << "\n";
    comment << "# Timestamp: " << std::time(nullptr) << "\n";
    if (rssi_archive_) comment << "# Archive: RSSI (not replayable)\n";
    std::string err;
    const char* preset = platform::rf_flipper_preset_name(settings_.preset);
    if (!platform::write_sub_raw(path, settings_.freq_hz, slice, err, preset, comment.str())) {
        status_ = err;
        return;
    }
    path_saved_ = path;
    status_ = rssi_archive_ ? ("Saved archive " + name) : ("Saved " + name);
    const auto dec = platform::decode_ook_timings(slice);
    if (dec.ok) status_ += " | " + dec.summary;
    phase_ = Phase::Idle;
}

void RfRecordScreen::trim_start() {
    if (trim_lo_ + 1 < trim_hi_) ++trim_lo_;
    draw_scope();
}
void RfRecordScreen::trim_end() {
    if (trim_hi_ > trim_lo_ + 1) --trim_hi_;
    draw_scope();
}

bool RfRecordScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_capture();
        return KvxScreen::handle_key(key);
    }
    if (phase_ == Phase::Armed || phase_ == Phase::Capturing) {
        stop_capture();
        phase_ = Phase::Idle;
        status_ = "Stopped";
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        arm_or_capture();
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
        trim_start();
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
        trim_end();
        return true;
    }
    if (key == LV_KEY_DEL || key == 127) {
        timings_.clear();
        phase_ = Phase::Idle;
        status_ = "Cleared";
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
    if (key == '?' || key == '/') {
        help_on_ = !help_on_;
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfRecordScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { trim_start(); }},
        {view::ICON_CHECK, [this]() { arm_or_capture(); }},
        {view::ICON_PLUS, [this]() { trim_end(); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

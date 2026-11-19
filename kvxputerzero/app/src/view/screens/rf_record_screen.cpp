#include "rf_record_screen.h"
#include "asset_manager.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "cc1101.h"
#include "gpio_line.h"
#include "rf_decode.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sstream>

namespace screen {
namespace {

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
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
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void RfRecordScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "RF Record RAW");

    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 4, 20);
    lv_obj_set_width(label_, 312);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);

    draw_ = lv_draw_buf_create(312, 70, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 4, 70);
    }
    refresh();
    timer_ = lv_timer_create(timer_cb, 150, this);
}

void RfRecordScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfRecordScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void RfRecordScreen::refresh() {
    if (!label_) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%s MHz thr %d dBm max %dms\n%s\n[%d..%d]/%zu  peak %d dBm\nZ/C trim  Enter save/arm  Del reset",
                  platform::rf_format_mhz(settings_.freq_hz).c_str(), settings_.threshold_dbm,
                  settings_.record_max_ms, status_.c_str(), trim_lo_, trim_hi_, timings_.size(), peak_rssi_);
    lv_label_set_text(label_, buf);
    if (phase_ == Phase::Preview) draw_preview();
}

void RfRecordScreen::draw_preview() {
    if (!draw_ || !draw_->data || timings_.empty()) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(8, 10, 14);
    for (int y = 0; y < 70; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(data + y * stride_);
        for (int x = 0; x < 312; ++x) row[x] = bg;
    }
    const int lo = std::clamp(trim_lo_, 0, static_cast<int>(timings_.size()));
    const int hi = std::clamp(trim_hi_, lo, static_cast<int>(timings_.size()));
    int64_t total = 0;
    for (int i = lo; i < hi; ++i) total += std::abs(timings_[i]);
    if (total <= 0) total = 1;
    int64_t acc = 0;
    const uint16_t mark = pack565(80, 255, 100);
    const uint16_t space = pack565(40, 60, 80);
    for (int i = lo; i < hi; ++i) {
        const int64_t w = std::abs(timings_[i]);
        const int x0 = static_cast<int>(acc * 311 / total);
        acc += w;
        const int x1 = static_cast<int>(acc * 311 / total);
        const bool high = timings_[i] > 0;
        const int y0 = high ? 10 : 40;
        const int y1 = high ? 40 : 60;
        const uint16_t c = high ? mark : space;
        for (int x = x0; x <= x1 && x < 312; ++x) {
            for (int y = y0; y <= y1; ++y) reinterpret_cast<uint16_t*>(data + y * stride_)[x] = c;
        }
    }
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
        refresh();
        return;
    }
    phase_ = Phase::Armed;
    status_ = "Armed (RSSI thr)…";
    cancel_ = false;
    timings_.clear();
    peak_rssi_ = -120;
    worker_ = std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        if (!radio.open({}, err)) {
            status_ = err;
            phase_ = Phase::Idle;
            return;
        }
        (void)radio.apply_preset(platform::Cc1101Preset::OokAsync, err);
        (void)radio.set_frequency_hz(settings_.freq_hz, err, true);
        (void)platform::request_cc1101_gdo0_edges(err);

        // Pre-trigger ring of GDO edges converted to timings.
        std::deque<platform::GpioEdgeEvent> pre;
        std::vector<platform::GpioEdgeEvent> edges;
        platform::GpioLine line("/dev/gpiochip0", 15);
        (void)line.request_edge_events(err);

        const auto deadline_arm = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        bool triggered = false;
        while (!cancel_ && std::chrono::steady_clock::now() < deadline_arm) {
            int dbm = -120;
            (void)radio.read_rssi_dbm(dbm, err);
            peak_rssi_ = std::max(peak_rssi_, dbm);
            std::vector<platform::GpioEdgeEvent> ev;
            (void)line.read_edge_events(ev, err);
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
            radio.close();
            return;
        }
        phase_ = Phase::Capturing;
        status_ = "Capturing…";
        edges.assign(pre.begin(), pre.end());
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(settings_.record_max_ms);
        while (!cancel_ && std::chrono::steady_clock::now() < end) {
            std::vector<platform::GpioEdgeEvent> ev;
            (void)line.read_edge_events(ev, err);
            for (auto& e : ev) edges.push_back(e);
            int dbm = -120;
            (void)radio.read_rssi_dbm(dbm, err);
            peak_rssi_ = std::max(peak_rssi_, dbm);
            // Idle end: no edges for 8 ms after we have some.
            if (edges.size() > 8) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                std::vector<platform::GpioEdgeEvent> more;
                (void)line.read_edge_events(more, err);
                if (more.empty()) break;
                for (auto& e : more) edges.push_back(e);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        radio.close();

        // Convert edges to Flipper timings.
        std::vector<int> timings;
        if (edges.size() >= 2) {
            for (size_t i = 1; i < edges.size(); ++i) {
                const int64_t dt = static_cast<int64_t>(edges[i].timestamp_ns - edges[i - 1].timestamp_ns) / 1000;
                if (dt <= 0 || dt > 500000) continue;
                const int signed_us = edges[i - 1].rising ? static_cast<int>(dt) : -static_cast<int>(dt);
                if (!timings.empty() && ((timings.back() > 0) == (signed_us > 0))) timings.back() += signed_us;
                else timings.push_back(signed_us);
            }
        }
        // Fallback if no edges: RSSI archival
        if (timings.size() < 4) {
            status_ = "Few edges; RSSI archive";
            std::vector<int> samples;
            // already closed — reopen briefly
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
        status_ = "Preview — Enter save";
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
    const char* mod = settings_.modulation == platform::RfModHint::Fsk ? "FSK" : "OOK";
    const std::string name = platform::rf_auto_name(settings_.freq_hz, mod);
    const std::string path = platform::rf_dir() + "/" + name;
    std::ostringstream comment;
    comment << "# RSSI_peak: " << peak_rssi_ << "\n";
    comment << "# Modulation: " << mod << "\n";
    comment << "# Timestamp: " << std::time(nullptr) << "\n";
    std::string err;
    if (!platform::write_sub_raw(path, settings_.freq_hz, slice, err, "FuriHalSubGhzPresetOok650Async",
                                 comment.str())) {
        status_ = err;
        return;
    }
    path_saved_ = path;
    status_ = "Saved " + name;
    // Optional decode
    const auto dec = platform::decode_ook_timings(slice);
    if (dec.ok) status_ += " | " + dec.summary;
    phase_ = Phase::Idle;
}

void RfRecordScreen::trim_start() {
    if (trim_lo_ + 1 < trim_hi_) ++trim_lo_;
    draw_preview();
}
void RfRecordScreen::trim_end() {
    if (trim_hi_ > trim_lo_ + 1) --trim_hi_;
    draw_preview();
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

#include "jam_detect_screen.h"

#include "asset_manager.h"
#include "elevate.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"
#include "wifi_scan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace screen {
namespace {

constexpr uint8_t kChannels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr int kNch = 11;

constexpr uint16_t kPalette565[] = {0x9818, 0x07E0, 0xFD20, 0xF800, 0xFFE0, 0x780F};

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t rgb_to_565(uint32_t rgb) {
    return pack565(static_cast<uint8_t>((rgb >> 16) & 0xff), static_cast<uint8_t>((rgb >> 8) & 0xff),
                   static_cast<uint8_t>(rgb & 0xff));
}

void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
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
    if (y < 0 || y >= h) return;
    if (x0 > x1) std::swap(x0, x1);
    auto* row = reinterpret_cast<uint16_t*>(data + y * stride);
    for (int x = std::max(0, x0); x <= std::min(w - 1, x1); ++x) row[x] = c;
}

void draw_rect(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    hline(data, stride, w, h, x0, y0, x1, c);
    hline(data, stride, w, h, x0, y1, x1, c);
    for (int y = y0; y <= y1; ++y) {
        put_px(data, stride, w, h, x0, y, c);
        put_px(data, stride, w, h, x1, y, c);
    }
}

constexpr uint8_t kDigit[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, {0x2, 0x6, 0x2, 0x2, 0x7}, {0x7, 0x1, 0x7, 0x4, 0x7},
    {0x7, 0x1, 0x7, 0x1, 0x7}, {0x5, 0x5, 0x7, 0x1, 0x1}, {0x7, 0x4, 0x7, 0x1, 0x7},
    {0x7, 0x4, 0x7, 0x5, 0x7}, {0x7, 0x1, 0x1, 0x1, 0x1}, {0x7, 0x5, 0x7, 0x5, 0x7},
    {0x7, 0x5, 0x7, 0x1, 0x7},
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

void draw_ch_label(uint8_t* data, int stride, int w, int h, int x, int y, uint8_t ch, uint16_t c) {
    if (ch >= 10) {
        draw_digit(data, stride, w, h, x, y, ch / 10, c);
        draw_digit(data, stride, w, h, x + 4, y, ch % 10, c);
    } else {
        draw_digit(data, stride, w, h, x + 2, y, ch, c);
    }
}

uint8_t load_threshold() {
    std::ifstream in(tools::config_file("jam_detect_threshold.txt"));
    int v = 70;
    if (in) in >> v;
    if (v < 20) v = 20;
    if (v > 95) v = 95;
    return static_cast<uint8_t>(v);
}

void save_threshold(uint8_t v) {
    std::ofstream out(tools::config_file("jam_detect_threshold.txt"), std::ios::trunc);
    if (out) out << static_cast<int>(v) << '\n';
}

} // namespace

JamDetectScreen::JamDetectScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    platform::wifi_analyzer_reset(state_);
    threshold_ = load_threshold();
    init();
}

JamDetectScreen::~JamDetectScreen() {
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

void JamDetectScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Jam Detect");

    auto* font = assets().load_standard_font(10);
    constexpr int kHeaderH = 18;
    constexpr int kFooterH = 12;
    plot_w_ = 320;
    plot_h_ = view::kContentHeight - kHeaderH - kFooterH;
    draw_ = lv_draw_buf_create(plot_w_, plot_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, plot_w_, plot_h_);
        lv_obj_set_pos(canvas_, 0, kHeaderH);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    status_lbl_ = lv_label_create(root);
    lv_label_set_text(status_lbl_, "…");
    lv_obj_set_style_text_font(status_lbl_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_lbl_, tok.ink_dim, 0);
    lv_obj_set_pos(status_lbl_, 180, 2);
    lv_obj_set_width(status_lbl_, 136);
    lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_CLIP);

    footer_lbl_ = lv_label_create(root);
    lv_label_set_text(footer_lbl_, "");
    lv_obj_set_style_text_font(footer_lbl_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer_lbl_, tok.ink, 0);
    lv_obj_set_pos(footer_lbl_, 8, view::kContentHeight - kFooterH);
    lv_obj_set_width(footer_lbl_, 304);
    lv_label_set_long_mode(footer_lbl_, LV_LABEL_LONG_CLIP);

    draw_bars();
    update_footer();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 80, this);
}

void JamDetectScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<JamDetectScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void JamDetectScreen::on_timer() {
    if (!data_ready_.exchange(false)) return;
    draw_bars();
    update_footer();
}

void JamDetectScreen::adjust_threshold(int delta) {
    int v = static_cast<int>(threshold_) + delta;
    if (v < 20) v = 20;
    if (v > 95) v = 95;
    threshold_ = static_cast<uint8_t>(v);
    save_threshold(threshold_);
    data_ready_ = true;
}

void JamDetectScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this]() {
        std::string err;
        const std::string saved = platform::load_monitor_iface();
        const bool want_monitor = platform::net_helpers_ready() &&
                                  !platform::monitor_iface_is_onboard(saved) &&
                                  platform::monitor_iface_valid(saved, err);

        platform::WifiMonitorSession session;
        bool use_monitor = false;
        if (want_monitor && session.start(saved, err)) use_monitor = true;

        {
            std::lock_guard<std::mutex> lock(data_mu_);
            platform::wifi_analyzer_reset(state_);
            if (use_monitor) {
                status_msg_ = "monitor " + saved;
                alert_msg_.clear();
            } else {
                status_msg_ = want_monitor ? ("mon fail: " + err) : "onboard (need USB mon for airtime)";
                alert_msg_.clear();
            }
            data_ready_ = true;
        }

        if (!use_monitor) {
            // Onboard: show AP density as a stand-in; no true airtime.
            while (!cancel_) {
                std::string scan_err;
                bool cancelled = cancel_.load();
                auto aps = platform::wifi_scan(scan_err, &cancelled);
                if (cancel_ || cancelled) break;
                {
                    std::lock_guard<std::mutex> lock(data_mu_);
                    if (!scan_err.empty() && aps.empty()) {
                        status_msg_ = scan_err;
                    } else {
                        platform::wifi_analyzer_apply_scan(state_, aps);
                        status_msg_ = "onboard scan (approx)";
                        alert_msg_.clear();
                        for (int ch = 1; ch <= 11; ++ch) {
                            int beacons = 0;
                            for (int a = 0; a < platform::WifiAnalyzerState::kApMax; ++a) {
                                if (state_.aps[a].used && state_.aps[a].channel == ch) ++beacons;
                            }
                            if (state_.load[ch] >= threshold_ && beacons <= 1) {
                                char buf[48];
                                std::snprintf(buf, sizeof(buf), "ALERT ch%d load=%u%% few APs", ch,
                                              static_cast<unsigned>(state_.load[ch]));
                                alert_msg_ = buf;
                                break;
                            }
                        }
                    }
                    data_ready_ = true;
                }
                for (int left = 800; left > 0 && !cancel_; left -= 50)
                    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(left, 50)));
            }
            running_ = false;
            return;
        }

        uint8_t last_set = 0;
        int sweep_idx = 0;
        while (!cancel_) {
            uint8_t ch = kChannels[sweep_idx % kNch];
            if (ch != last_set) {
                if (session.set_channel(ch)) last_set = ch;
                else if (last_set) ch = last_set;
            }
            {
                std::lock_guard<std::mutex> lock(data_mu_);
                state_.sweep_ch = ch;
                state_.nav_ch = ch;
            }

            uint32_t bytes = 0;
            uint32_t pkts = 0;
            int8_t peak_rssi = -128;
            uint32_t beacons = 0;
            const auto t0 = std::chrono::steady_clock::now();
            while (!cancel_) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t0)
                                         .count();
                if (elapsed >= 350) break;
                std::vector<uint8_t> frame;
                std::string ferr;
                if (!session.read_frame(frame, 40, ferr)) continue;
                size_t hdr = 0;
                int8_t rssi = -128;
                uint8_t rch = 0;
                if (!platform::wifi_parse_radiotap(frame.data(), frame.size(), hdr, rssi, rch)) continue;
                if (hdr >= frame.size()) continue;
                const uint8_t* body = frame.data() + hdr;
                const size_t blen = frame.size() - hdr;
                if (blen > 0 && (body[0] == 0x80 || body[0] == 0x50)) ++beacons;
                std::lock_guard<std::mutex> lock(data_mu_);
                platform::wifi_analyzer_ingest_frame(state_, body, blen, rssi, ch ? ch : rch, bytes, pkts,
                                                    peak_rssi);
            }
            if (cancel_) break;

            const auto elapsed_ms = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                    .count());
            {
                std::lock_guard<std::mutex> lock(data_mu_);
                platform::wifi_analyzer_finish_dwell(state_, bytes, pkts, peak_rssi, elapsed_ms);
                const uint8_t load = state_.load[ch];
                char buf[64];
                std::snprintf(buf, sizeof(buf), "ch%u load=%u%% thr=%u", static_cast<unsigned>(ch),
                              static_cast<unsigned>(load), static_cast<unsigned>(threshold_));
                status_msg_ = buf;
                if (load >= threshold_ && beacons <= 1) {
                    std::snprintf(buf, sizeof(buf), "ALERT ch%u load=%u%% beacons=%u",
                                  static_cast<unsigned>(ch), static_cast<unsigned>(load),
                                  static_cast<unsigned>(beacons));
                    alert_msg_ = buf;
                } else if (!alert_msg_.empty() && load < threshold_) {
                    // keep last alert briefly; clear when quiet
                    alert_msg_.clear();
                }
                data_ready_ = true;
            }
            sweep_idx++;
        }
        session.stop();
        running_ = false;
    });
}

void JamDetectScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

void JamDetectScreen::draw_bars() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = rgb_to_565(view_model().bg_color());
    const uint16_t pri = rgb_to_565(view_model().pri_color());
    const uint16_t sec = rgb_to_565(view_model().sec_color());
    const uint16_t white = pack565(255, 255, 255);
    const uint16_t alert_col = pack565(255, 40, 40);

    fill_rect(data, stride_, plot_w_, plot_h_, 0, 0, plot_w_ - 1, plot_h_ - 1, bg);

    platform::WifiAnalyzerState st;
    uint8_t thr = 70;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        st = state_;
        thr = threshold_;
    }

    const int x0 = 8;
    const int top = 4;
    const int labelH = 8;
    const int baseline = plot_h_ - labelH - 2;
    const int plotH = std::max(8, baseline - top);
    const int plotW = plot_w_ - 2 * x0;
    const int slot = plotW / kNch;
    const int barW = std::max(4, (slot * 2) / 3);
    const int barOff = (slot - barW) / 2;

    hline(data, stride_, plot_w_, plot_h_, x0, baseline, x0 + plotW - 1, pri);
    // Threshold guide line
    const int thrY = baseline - (plotH * static_cast<int>(thr) / 100);
    hline(data, stride_, plot_w_, plot_h_, x0, thrY, x0 + plotW - 1, alert_col);

    for (int i = 0; i < kNch; ++i) {
        const uint8_t ch = kChannels[i];
        const int cellX = x0 + i * slot;
        const int bx = cellX + barOff;
        const bool hot = st.load[ch] >= thr;
        const uint16_t col = hot ? alert_col : kPalette565[i % 6];
        const int bh = plotH * static_cast<int>(st.load[ch]) / 100;
        if (bh > 0) {
            fill_rect(data, stride_, plot_w_, plot_h_, bx, baseline - bh, bx + barW - 1, baseline - 1, col);
        }
        if (ch == st.nav_ch) {
            draw_rect(data, stride_, plot_w_, plot_h_, bx - 1, top, bx + barW, baseline, sec);
        }
        const int ph = plotH * static_cast<int>(st.peak[ch]) / 100;
        if (ph > 0) {
            hline(data, stride_, plot_w_, plot_h_, bx, baseline - ph, bx + barW - 1, white);
        }
        draw_ch_label(data, stride_, plot_w_, plot_h_, bx + std::max(0, (barW - 7) / 2), baseline + 2, ch,
                      hot ? alert_col : pri);
    }

    if (canvas_) {
        lv_obj_invalidate(canvas_);
        lv_obj_move_foreground(canvas_);
    }
}

void JamDetectScreen::update_footer() {
    if (!footer_lbl_) return;
    std::string status, alert;
    uint8_t thr = 70;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        status = status_msg_;
        alert = alert_msg_;
        thr = threshold_;
    }
    if (!alert.empty()) {
        lv_label_set_text(footer_lbl_, alert.c_str());
    } else {
        char foot[80];
        std::snprintf(foot, sizeof(foot), "thr=%u%%  Z/C thr  Esc=back", static_cast<unsigned>(thr));
        lv_label_set_text(footer_lbl_, foot);
    }
    if (status_lbl_) {
        lv_label_set_text(status_lbl_, status.empty() ? "…" : status.c_str());
    }
}

bool JamDetectScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_worker();
        return KvxScreen::handle_key(key);
    }
    if (key == LV_KEY_UP || key == 'z' || key == 'Z') {
        adjust_threshold(5);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'c' || key == 'C') {
        adjust_threshold(-5);
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'f' || key == 'F') {
        std::lock_guard<std::mutex> lock(data_mu_);
        int idx = 0;
        for (int i = 0; i < kNch; ++i) {
            if (kChannels[i] == state_.nav_ch) {
                idx = i;
                break;
            }
        }
        state_.nav_ch = kChannels[(idx + kNch - 1) % kNch];
        data_ready_ = true;
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'x' || key == 'X') {
        std::lock_guard<std::mutex> lock(data_mu_);
        int idx = 0;
        for (int i = 0; i < kNch; ++i) {
            if (kChannels[i] == state_.nav_ch) {
                idx = i;
                break;
            }
        }
        state_.nav_ch = kChannels[(idx + 1) % kNch];
        data_ready_ = true;
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> JamDetectScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             stop_worker();
             view_model().go_back();
         }},
        {view::ICON_MINUS, [this]() { adjust_threshold(-5); }},
        {view::ICON_CHECK, []() {}},
        {view::ICON_PLUS, [this]() { adjust_threshold(5); }},
        {view::ICON_HOUSE,
         [this]() {
             stop_worker();
             view_model().go_home();
         }},
    }};
}

} // namespace screen

#include "wifi_analyzer_screen.h"

#include "asset_manager.h"
#include "elevate.h"
#include "theme.h"
#include "ui_const.h"
#include "wifi_scan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace screen {
namespace {

constexpr uint8_t kChannels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr int kNch = 11;

// Same EVA-01 RGB565 palette as Adv channel_analyzer.cpp
constexpr uint16_t kPalette565[] = {
    0x9818, // purple
    0x07E0, // green
    0xFD20, // orange
    0xF800, // red
    0xFFE0, // yellow
    0x780F, // deep violet
};

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t rgb_to_565(uint32_t rgb) {
    return pack565(static_cast<uint8_t>((rgb >> 16) & 0xff), static_cast<uint8_t>((rgb >> 8) & 0xff),
                   static_cast<uint8_t>(rgb & 0xff));
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
    if (y < 0 || y >= h) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(w - 1, x1);
    auto* row = reinterpret_cast<uint16_t*>(data + y * stride);
    for (int x = x0; x <= x1; ++x) row[x] = c;
}

void vline(uint8_t* data, int stride, int w, int h, int x, int y0, int y1, uint16_t c) {
    if (x < 0 || x >= w) return;
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) put_px(data, stride, w, h, x, y, c);
}

void draw_rect(uint8_t* data, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    hline(data, stride, w, h, x0, y0, x1, c);
    hline(data, stride, w, h, x0, y1, x1, c);
    vline(data, stride, w, h, x0, y0, y1, c);
    vline(data, stride, w, h, x1, y0, y1, c);
}

// 3x5 digit glyphs (MSB left). Space for '1'–'9' and two-digit 10/11.
constexpr uint8_t kDigit[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
    {0x2, 0x6, 0x2, 0x2, 0x7}, // 1
    {0x7, 0x1, 0x7, 0x4, 0x7}, // 2
    {0x7, 0x1, 0x7, 0x1, 0x7}, // 3
    {0x5, 0x5, 0x7, 0x1, 0x1}, // 4
    {0x7, 0x4, 0x7, 0x1, 0x7}, // 5
    {0x7, 0x4, 0x7, 0x5, 0x7}, // 6
    {0x7, 0x1, 0x1, 0x1, 0x1}, // 7
    {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
    {0x7, 0x5, 0x7, 0x1, 0x7}, // 9
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

} // namespace

WifiAnalyzerScreen::WifiAnalyzerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    platform::wifi_analyzer_reset(state_);
    init();
}

WifiAnalyzerScreen::~WifiAnalyzerScreen() {
    stop_worker();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
    }
    if (list_cont_ && lv_obj_is_valid(list_cont_)) {
        lv_obj_delete(list_cont_);
        list_cont_ = nullptr;
    }
    if (draw_) {
        lv_draw_buf_destroy(draw_);
        draw_ = nullptr;
    }
}

void WifiAnalyzerScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    // Adv title is "WiFi Analyz" — keep that chrome.
    make_header(root, "WiFi Analyz");

    auto* font = assets().load_standard_font(10);

    // Plot fills the content area under the header, above the Adv-style footer.
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

    // Tiny status tucked into the header area (right side) — Adv had no second status line.
    status_lbl_ = lv_label_create(root);
    lv_label_set_text(status_lbl_, "scan…");
    lv_obj_set_style_text_font(status_lbl_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_lbl_, tok.ink_dim, 0);
    lv_obj_set_pos(status_lbl_, 200, 2);
    lv_obj_set_width(status_lbl_, 116);
    lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_CLIP);

    footer_lbl_ = lv_label_create(root);
    lv_label_set_text(footer_lbl_, "");
    lv_obj_set_style_text_font(footer_lbl_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer_lbl_, tok.ink, 0);
    lv_obj_set_pos(footer_lbl_, 8, view::kContentHeight - kFooterH);
    lv_obj_set_width(footer_lbl_, 304);
    lv_label_set_long_mode(footer_lbl_, LV_LABEL_LONG_CLIP);

    list_cont_ = lv_obj_create(root);
    lv_obj_set_pos(list_cont_, 0, kHeaderH);
    lv_obj_set_size(list_cont_, 320, plot_h_);
    lv_obj_set_style_bg_opa(list_cont_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(list_cont_, tok.bg, 0);
    lv_obj_set_style_border_width(list_cont_, 0, 0);
    lv_obj_set_style_pad_all(list_cont_, 2, 0);
    lv_obj_add_flag(list_cont_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(list_cont_, LV_OBJ_FLAG_SCROLLABLE);

    want_ch_ = state_.nav_ch;
    want_dwell_ = state_.dwell_ms;
    want_lock_ = state_.lock_ch;
    draw_bars();
    update_footer();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 80, this);
}

void WifiAnalyzerScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<WifiAnalyzerScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void WifiAnalyzerScreen::on_timer() {
    if (!data_ready_.exchange(false)) return;
    if (mode_ == Mode::Bars) {
        draw_bars();
        update_footer();
    } else {
        refresh_list_ui();
        update_footer();
    }
}

void WifiAnalyzerScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this]() {
        std::string err;
        const std::string saved = platform::load_monitor_iface();
        // Without the helper's network capabilities, stay on nmcli so every
        // channel still comes from the scan instead of a stuck monitor radio.
        const bool want_monitor = platform::net_helpers_ready() &&
                                  !platform::monitor_iface_is_onboard(saved) &&
                                  platform::monitor_iface_valid(saved, err);

        platform::WifiMonitorSession session;
        bool use_monitor = false;
        if (want_monitor) {
            if (session.start(saved, err)) {
                use_monitor = true;
            } else {
                std::lock_guard<std::mutex> lock(data_mu_);
                error_msg_.clear();
                status_msg_ = "monitor fail → scan: " + err;
            }
        }

        {
            std::lock_guard<std::mutex> lock(data_mu_);
            session_ok_ = true;
            if (use_monitor) {
                error_msg_.clear();
                status_msg_ = "monitor " + saved;
            } else if (status_msg_.empty()) {
                status_msg_ = "onboard scan";
            }
            platform::wifi_analyzer_reset(state_);
            data_ready_ = true;
        }

        if (!use_monitor) {
            // Default path: repeated nmcli scans → channel bars + AP list.
            while (!cancel_) {
                {
                    std::lock_guard<std::mutex> lock(data_mu_);
                    if (status_msg_.find("APs") == std::string::npos) {
                        status_msg_ = "scanning all channels";
                        data_ready_ = true;
                    }
                }
                std::string scan_err;
                bool cancelled = cancel_.load();
                auto aps = platform::wifi_scan(scan_err, &cancelled);
                if (cancel_ || cancelled) break;
                {
                    std::lock_guard<std::mutex> lock(data_mu_);
                    state_.dwell_ms = want_dwell_.load();
                    state_.lock_ch = want_lock_.load();
                    if (!scan_err.empty() && aps.empty()) {
                        error_msg_ = scan_err;
                        status_msg_ = "onboard scan";
                    } else {
                        error_msg_.clear();
                        platform::wifi_analyzer_apply_scan(state_, aps);
                        char buf[72];
                        std::snprintf(buf, sizeof(buf), "scan  %d APs  dwell=%ums  %s", state_.ap_count,
                                      static_cast<unsigned>(state_.dwell_ms),
                                      state_.lock_ch ? "lock" : "hop");
                        status_msg_ = buf;
                    }
                    data_ready_ = true;
                }
                // Pace scans with dwell (150–1000 ms); allow Esc between.
                const int pause = static_cast<int>(want_dwell_.load());
                for (int left = pause; left > 0 && !cancel_; left -= 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(left, 50)));
                }
            }
            running_ = false;
            return;
        }

        uint8_t radio_ch = 0;
        while (!cancel_) {
            platform::WifiAnalyzerState local;
            uint16_t dwell = 350;
            uint8_t lock = 0;
            {
                std::lock_guard<std::mutex> lock_g(data_mu_);
                local = state_;
                dwell = want_dwell_.load();
                lock = want_lock_.load();
                local.lock_ch = lock;
                local.dwell_ms = dwell;
                if (lock) local.sweep_ch = lock;
                else if (local.sweep_ch < 1 || local.sweep_ch > 11)
                    local.sweep_ch = kChannels[local.sweep_idx % kNch];
            }

            if (local.sweep_ch != radio_ch && session.set_channel(local.sweep_ch)) {
                radio_ch = local.sweep_ch;
            }

            uint32_t bytes = 0;
            uint32_t pkts = 0;
            int8_t peak_rssi = -128;
            const auto t0 = std::chrono::steady_clock::now();
            while (!cancel_) {
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
                if (elapsed >= dwell) break;

                std::vector<uint8_t> frame;
                std::string rderr;
                if (!session.read_frame(frame, 20, rderr)) {
                    if (!rderr.empty() &&
                        (rderr.find("helper") != std::string::npos || rderr.find("EOF") != std::string::npos)) {
                        std::lock_guard<std::mutex> lock_g(data_mu_);
                        error_msg_ = rderr;
                        data_ready_ = true;
                        cancel_ = true;
                        break;
                    }
                    continue;
                }
                size_t hdr = 0;
                int8_t rssi = -128;
                uint8_t ch = 0;
                if (!platform::wifi_parse_radiotap(frame.data(), frame.size(), hdr, rssi, ch)) continue;
                if (hdr >= frame.size()) continue;
                const uint8_t* body = frame.data() + hdr;
                const size_t blen = frame.size() - hdr;
                if (!ch) ch = radio_ch ? radio_ch : local.sweep_ch;

                std::lock_guard<std::mutex> lock_g(data_mu_);
                platform::wifi_analyzer_ingest_frame(state_, body, blen, rssi, ch, bytes, pkts, peak_rssi);
            }
            if (cancel_) break;

            const auto t1 = std::chrono::steady_clock::now();
            const uint32_t elapsed_ms = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            {
                std::lock_guard<std::mutex> lock_g(data_mu_);
                state_.dwell_ms = dwell;
                state_.lock_ch = lock;
                state_.sweep_ch = radio_ch ? radio_ch : local.sweep_ch;
                platform::wifi_analyzer_finish_dwell(state_, bytes, pkts, peak_rssi, elapsed_ms);
                char buf[72];
                std::snprintf(buf, sizeof(buf), "monitor %s  dwell=%ums  %s", saved.c_str(),
                              static_cast<unsigned>(dwell), lock ? "lock" : "hop");
                status_msg_ = buf;
                error_msg_.clear();
                data_ready_ = true;
            }
        }

        session.stop();
        running_ = false;
    });
}

void WifiAnalyzerScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

void WifiAnalyzerScreen::draw_bars() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);

    // Match Adv: theme bg / pri / sec (selection), EVA palette bars, white peak.
    const uint16_t bg = rgb_to_565(view_model().bg_color());
    const uint16_t pri = rgb_to_565(view_model().pri_color());
    const uint16_t sec = rgb_to_565(view_model().sec_color());
    const uint16_t white = pack565(255, 255, 255);

    fill_rect(data, stride_, plot_w_, plot_h_, 0, 0, plot_w_ - 1, plot_h_ - 1, bg);

    platform::WifiAnalyzerState st;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        st = state_;
    }

    // Adv geometry: x0=8, labels under baseline, bars above.
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

    for (int i = 0; i < kNch; ++i) {
        const uint8_t ch = kChannels[i];
        const int cellX = x0 + i * slot;
        const int bx = cellX + barOff;
        const bool isNav = (ch == st.nav_ch);
        const uint16_t col = kPalette565[i % 6];

        // Clear column band (Adv dirty-column style, here full redraw).
        fill_rect(data, stride_, plot_w_, plot_h_, bx - 1, top, bx + barW, baseline, bg);

        const int bh = plotH * static_cast<int>(st.load[ch]) / 100;
        if (bh > 0) {
            fill_rect(data, stride_, plot_w_, plot_h_, bx, baseline - bh, bx + barW - 1, baseline - 1, col);
        }
        // Adv: green (sec) rectangle around selected channel column.
        if (isNav) {
            draw_rect(data, stride_, plot_w_, plot_h_, bx - 1, top, bx + barW, baseline, sec);
        }
        const int ph = plotH * static_cast<int>(st.peak[ch]) / 100;
        if (ph > 0) {
            hline(data, stride_, plot_w_, plot_h_, bx, baseline - ph, bx + barW - 1, white);
        }

        // Channel number under the bar (Adv tft.drawString).
        const uint16_t label_col = isNav ? sec : pri;
        draw_ch_label(data, stride_, plot_w_, plot_h_, bx + std::max(0, (barW - 7) / 2), baseline + 2, ch,
                      label_col);
    }

    if (canvas_) {
        lv_obj_invalidate(canvas_);
        lv_obj_move_foreground(canvas_);
    }
}

void WifiAnalyzerScreen::update_footer() {
    if (!footer_lbl_) return;
    platform::WifiAnalyzerState st;
    std::string mode;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        st = state_;
        mode = error_msg_.empty() ? status_msg_ : error_msg_;
    }
    // Adv footer: "ChN load% pk% rssi OK=ch L=all"
    char foot[80];
    std::snprintf(foot, sizeof(foot), "Ch%u %u%% pk%u%% %ddBm OK=ch L=all",
                  static_cast<unsigned>(st.nav_ch), static_cast<unsigned>(st.load[st.nav_ch]),
                  static_cast<unsigned>(st.peak[st.nav_ch]), static_cast<int>(st.rssi[st.nav_ch]));
    lv_label_set_text(footer_lbl_, foot);
    if (status_lbl_ && !mode.empty()) {
        // Short mode tag on the right of the title bar.
        const char* tag = "scan";
        if (mode.find("monitor") != std::string::npos) tag = "mon";
        else if (mode.find("fail") != std::string::npos) tag = "scan!";
        lv_label_set_text(status_lbl_, tag);
    }
}

void WifiAnalyzerScreen::refresh_list_ui() {
    if (!list_cont_) return;
    lv_obj_clean(list_cont_);
    const auto tok = tokens();
    auto* font = assets().load_standard_font(10);

    platform::WifiAnalyzerState st;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        st = state_;
    }

    if (mode_ == Mode::Detail && detail_ap_ >= 0 && detail_ap_ < platform::WifiAnalyzerState::kApMax &&
        st.aps[detail_ap_].used) {
        const auto fields = platform::wifi_ap_detail_fields(st.aps[detail_ap_]);
        int y = 2;
        for (const auto& f : fields) {
            auto* lbl = lv_label_create(list_cont_);
            const std::string line = f.first + ": " + f.second;
            lv_label_set_text(lbl, line.c_str());
            lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl, tok.ink, 0);
            lv_obj_set_pos(lbl, 4, y);
            lv_obj_set_width(lbl, 310);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
            y += 12;
        }
        if (footer_lbl_) lv_label_set_text(footer_lbl_, "Esc=list");
        return;
    }

    list_rows_ = platform::wifi_ap_row_labels(st, list_filter_);
    if (list_rows_.empty()) {
        auto* lbl = lv_label_create(list_cont_);
        lv_label_set_text(lbl, "(no APs yet)");
        lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, tok.ink, 0);
        lv_obj_set_pos(lbl, 4, 4);
    } else {
        if (list_sel_ >= static_cast<int>(list_rows_.size())) list_sel_ = static_cast<int>(list_rows_.size()) - 1;
        if (list_sel_ < 0) list_sel_ = 0;
        const int visible = std::min(8, static_cast<int>(list_rows_.size()));
        int start = std::max(0, list_sel_ - visible + 1);
        for (int i = 0; i < visible; ++i) {
            const int idx = start + i;
            if (idx >= static_cast<int>(list_rows_.size())) break;
            auto* lbl = lv_label_create(list_cont_);
            const std::string prefix = (idx == list_sel_) ? "> " : "  ";
            lv_label_set_text(lbl, (prefix + list_rows_[idx]).c_str());
            lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl, idx == list_sel_ ? tok.accent : tok.ink, 0);
            lv_obj_set_pos(lbl, 2, 2 + i * 12);
            lv_obj_set_width(lbl, 316);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        }
    }
    if (footer_lbl_) {
        char f[48];
        std::snprintf(f, sizeof(f), "%s  Ent=detail Esc=bars", list_filter_ ? "ch filter" : "all APs");
        lv_label_set_text(footer_lbl_, f);
    }
}

void WifiAnalyzerScreen::open_ap_list(uint8_t filter_ch) {
    mode_ = Mode::ApList;
    list_filter_ = filter_ch;
    list_sel_ = 0;
    if (canvas_) lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    if (list_cont_) lv_obj_remove_flag(list_cont_, LV_OBJ_FLAG_HIDDEN);
    refresh_list_ui();
}

void WifiAnalyzerScreen::open_detail(int ap_idx) {
    if (ap_idx < 0) return;
    mode_ = Mode::Detail;
    detail_ap_ = ap_idx;
    refresh_list_ui();
}

void WifiAnalyzerScreen::leave_mode() {
    if (mode_ == Mode::Detail) {
        mode_ = Mode::ApList;
        refresh_list_ui();
        return;
    }
    if (mode_ == Mode::ApList) {
        mode_ = Mode::Bars;
        if (list_cont_) lv_obj_add_flag(list_cont_, LV_OBJ_FLAG_HIDDEN);
        if (canvas_) lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
        draw_bars();
        update_footer();
        return;
    }
}

void WifiAnalyzerScreen::export_csv() {
    std::string path, err;
    platform::WifiAnalyzerState st;
    {
        std::lock_guard<std::mutex> lock(data_mu_);
        st = state_;
    }
    if (!platform::wifi_analyzer_export_csv(st, path, err)) {
        std::lock_guard<std::mutex> lock(data_mu_);
        error_msg_ = err;
    } else {
        std::lock_guard<std::mutex> lock(data_mu_);
        status_msg_ = "exported " + path;
        error_msg_.clear();
    }
    data_ready_ = true;
}

bool WifiAnalyzerScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        if (mode_ != Mode::Bars) {
            leave_mode();
            return true;
        }
        stop_worker();
        return KvxScreen::handle_key(key);
    }

    if (mode_ == Mode::ApList) {
        if (key == LV_KEY_UP || key == 'z' || key == 'Z') {
            list_sel_ = std::max(0, list_sel_ - 1);
            refresh_list_ui();
            return true;
        }
        if (key == LV_KEY_DOWN || key == 'c' || key == 'C') {
            list_sel_++;
            refresh_list_ui();
            return true;
        }
        if (key == LV_KEY_ENTER || key == ' ') {
            platform::WifiAnalyzerState st;
            {
                std::lock_guard<std::mutex> lock(data_mu_);
                st = state_;
            }
            const int idx = platform::wifi_ap_index_at_row(st, list_filter_, list_sel_);
            open_detail(idx);
            return true;
        }
        return true;
    }
    if (mode_ == Mode::Detail) {
        return true;
    }

    // Bars mode
    if (key == LV_KEY_LEFT || key == 'f' || key == 'F') {
        std::lock_guard<std::mutex> lock(data_mu_);
        int idx = 0;
        for (int i = 0; i < kNch; ++i) {
            if (kChannels[i] == state_.nav_ch) {
                idx = i;
                break;
            }
        }
        idx = (idx + kNch - 1) % kNch;
        state_.nav_ch = kChannels[idx];
        want_ch_ = state_.nav_ch;
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
        idx = (idx + 1) % kNch;
        state_.nav_ch = kChannels[idx];
        want_ch_ = state_.nav_ch;
        data_ready_ = true;
        return true;
    }
    if (key == LV_KEY_UP || key == 'z' || key == 'Z') {
        std::lock_guard<std::mutex> lock(data_mu_);
        int d = static_cast<int>(state_.dwell_ms) + 100;
        platform::wifi_analyzer_set_dwell(state_, d);
        want_dwell_ = state_.dwell_ms;
        data_ready_ = true;
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'c' || key == 'C') {
        std::lock_guard<std::mutex> lock(data_mu_);
        int d = static_cast<int>(state_.dwell_ms) - 100;
        platform::wifi_analyzer_set_dwell(state_, d);
        want_dwell_ = state_.dwell_ms;
        data_ready_ = true;
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        uint8_t ch = 1;
        {
            std::lock_guard<std::mutex> lock(data_mu_);
            ch = state_.nav_ch;
        }
        open_ap_list(ch);
        return true;
    }
    if (key == 'l' || key == 'L') {
        open_ap_list(0);
        return true;
    }
    if (key == 'k' || key == 'K') {
        std::lock_guard<std::mutex> lock(data_mu_);
        platform::wifi_analyzer_toggle_lock(state_);
        want_lock_ = state_.lock_ch;
        data_ready_ = true;
        return true;
    }
    if (key == 'e' || key == 'E') {
        export_csv();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> WifiAnalyzerScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             if (mode_ != Mode::Bars) leave_mode();
             else {
                 stop_worker();
                 view_model().go_back();
             }
         }},
        {view::ICON_MINUS,
         [this]() {
             std::lock_guard<std::mutex> lock(data_mu_);
             platform::wifi_analyzer_set_dwell(state_, static_cast<int>(state_.dwell_ms) - 100);
             want_dwell_ = state_.dwell_ms;
             data_ready_ = true;
         }},
        {view::ICON_CHECK,
         [this]() {
             if (mode_ == Mode::Bars) {
                 uint8_t ch = 1;
                 {
                     std::lock_guard<std::mutex> lock(data_mu_);
                     ch = state_.nav_ch;
                 }
                 open_ap_list(ch);
             } else if (mode_ == Mode::ApList) {
                 platform::WifiAnalyzerState st;
                 {
                     std::lock_guard<std::mutex> lock(data_mu_);
                     st = state_;
                 }
                 open_detail(platform::wifi_ap_index_at_row(st, list_filter_, list_sel_));
             }
         }},
        {view::ICON_PLUS,
         [this]() {
             std::lock_guard<std::mutex> lock(data_mu_);
             platform::wifi_analyzer_set_dwell(state_, static_cast<int>(state_.dwell_ms) + 100);
             want_dwell_ = state_.dwell_ms;
             data_ready_ = true;
         }},
        {view::ICON_HOUSE,
         [this]() {
             stop_worker();
             view_model().go_home();
         }},
    }};
}

} // namespace screen

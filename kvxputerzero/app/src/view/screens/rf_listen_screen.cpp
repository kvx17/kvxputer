#include "rf_listen_screen.h"
#include "asset_manager.h"

#include "audio.h"
#include "cap_detect.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

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

void draw_text(uint8_t* data, int stride, int w, int h, int x, int y, const char* s, uint16_t c) {
    int cx = x;
    for (const char* p = s; p && *p; ++p) {
        if (*p == ' ') {
            cx += 3;
            continue;
        }
        if (*p == '.') {
            put_px(data, stride, w, h, cx + 1, y + 4, c);
            cx += 3;
            continue;
        }
        if (*p == '-') {
            hline(data, stride, w, h, cx, y + 2, cx + 2, c);
            cx += 4;
            continue;
        }
        if (*p >= '0' && *p <= '9') {
            draw_digit(data, stride, w, h, cx, y, *p - '0', c);
            cx += 4;
        }
    }
}

} // namespace

RfListenScreen::RfListenScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets)
    : KvxScreen(vm, assets) {
    settings_ = platform::load_rf_settings();
    init();
}

RfListenScreen::~RfListenScreen() {
    stop_worker();
    if (timer_) lv_timer_delete(timer_);
    if (mini_ && lv_obj_is_valid(mini_)) lv_obj_delete(mini_);
    if (mini_buf_) lv_draw_buf_destroy(mini_buf_);
}

void RfListenScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "RF Listen");

    auto* font = assets().load_standard_font(12);
    auto* font_sm = assets().load_standard_font(10);
    const lv_font_t* use = font ? font : &lv_font_montserrat_14;
    const lv_font_t* use_sm = font_sm ? font_sm : &lv_font_montserrat_12;

    tuned_lbl_ = lv_label_create(root);
    lv_obj_set_pos(tuned_lbl_, 4, 18);
    lv_obj_set_width(tuned_lbl_, 312);
    lv_obj_set_style_text_font(tuned_lbl_, use, 0);
    lv_obj_set_style_text_color(tuned_lbl_, tok.accent, 0);
    lv_label_set_long_mode(tuned_lbl_, LV_LABEL_LONG_CLIP);

    signal_lbl_ = lv_label_create(root);
    lv_obj_set_pos(signal_lbl_, 4, 34);
    lv_obj_set_width(signal_lbl_, 312);
    lv_obj_set_style_text_font(signal_lbl_, use_sm, 0);
    lv_obj_set_style_text_color(signal_lbl_, tok.ink, 0);
    lv_label_set_long_mode(signal_lbl_, LV_LABEL_LONG_CLIP);

    bar_scale_ = lv_label_create(root);
    lv_obj_set_pos(bar_scale_, 4, 48);
    lv_obj_set_width(bar_scale_, 312);
    lv_obj_set_style_text_font(bar_scale_, use_sm, 0);
    lv_obj_set_style_text_color(bar_scale_, tok.ink_dim, 0);
    lv_label_set_text(bar_scale_, "RSSI  weak                         strong");

    bar_bg_ = lv_obj_create(root);
    lv_obj_remove_style_all(bar_bg_);
    lv_obj_set_size(bar_bg_, 312, 12);
    lv_obj_set_pos(bar_bg_, 4, 62);
    lv_obj_set_style_bg_color(bar_bg_, tok.surface, 0);
    lv_obj_set_style_bg_opa(bar_bg_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_bg_, 2, 0);

    bar_fg_ = lv_obj_create(bar_bg_);
    lv_obj_remove_style_all(bar_fg_);
    lv_obj_set_size(bar_fg_, 2, 12);
    lv_obj_set_pos(bar_fg_, 0, 0);
    lv_obj_set_style_bg_color(bar_fg_, tok.main, 0);
    lv_obj_set_style_bg_opa(bar_fg_, LV_OPA_COVER, 0);

    mini_w_ = 312;
    mini_h_ = 56;
    mini_buf_ = lv_draw_buf_create(mini_w_, mini_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (mini_buf_ && mini_buf_->data) {
        mini_stride_ = static_cast<int>(mini_buf_->header.stride);
        mini_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(mini_, mini_buf_);
        lv_obj_set_size(mini_, mini_w_, mini_h_);
        lv_obj_set_pos(mini_, 4, 78);
    }

    footer_lbl_ = lv_label_create(root);
    lv_obj_set_pos(footer_lbl_, 4, view::kContentHeight - 12);
    lv_obj_set_width(footer_lbl_, 312);
    lv_obj_set_style_text_font(footer_lbl_, use_sm, 0);
    lv_obj_set_style_text_color(footer_lbl_, tok.ink_dim, 0);
    lv_label_set_long_mode(footer_lbl_, LV_LABEL_LONG_CLIP);

    const auto cap = platform::detect_cc1101();
    cap_ok_ = cap.present;
    demo_ = !cap_ok_;
    refresh_labels();
    update_bar();
    draw_mini();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 100, this);
}

void RfListenScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfListenScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void RfListenScreen::on_timer() {
    if (!ready_.exchange(false)) return;
    refresh_labels();
    update_bar();
    draw_mini();
}

void RfListenScreen::update_bar() {
    // Map −110…−30 dBm across the bar so typical Sub-GHz levels fill usefully.
    const float t = std::clamp((rssi_dbm_ + 110) / 80.f, 0.f, 1.f);
    if (bar_fg_) {
        lv_obj_set_width(bar_fg_, std::max(2, static_cast<int>(312 * t)));
        // Green when strong, yellow mid, dim when weak.
        lv_color_t col;
        if (rssi_dbm_ >= settings_.threshold_dbm) col = lv_color_hex(0x63e2b7);
        else if (rssi_dbm_ >= settings_.threshold_dbm - 15) col = lv_color_hex(0xf2c97d);
        else col = lv_color_hex(0x70c0e8);
        lv_obj_set_style_bg_color(bar_fg_, col, 0);
    }
}

void RfListenScreen::draw_mini() {
    if (!mini_buf_ || !mini_buf_->data) return;
    auto* data = static_cast<uint8_t*>(mini_buf_->data);
    const uint16_t bg = pack565(10, 12, 16);
    const uint16_t grid = pack565(36, 40, 48);
    const uint16_t ink = pack565(180, 190, 200);
    const uint16_t ink_dim = pack565(100, 110, 120);
    const uint16_t trace = pack565(255, 200, 60);
    const uint16_t center_col = pack565(40, 255, 100);
    const uint16_t thr_col = pack565(255, 90, 90);

    for (int y = 0; y < mini_h_; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(data + y * mini_stride_);
        for (int x = 0; x < mini_w_; ++x) row[x] = bg;
    }

    constexpr int kLeft = 2;
    constexpr int kRightPad = 2;
    constexpr int kBottom = 8;
    const int plot_top = 10;
    const int plot_bottom = mini_h_ - kBottom - 1;
    const int plot_left = kLeft;
    const int plot_right = mini_w_ - kRightPad - 1;

    // Title strip
    draw_text(data, mini_stride_, mini_w_, mini_h_, 2, 1, "near", ink_dim);

    std::vector<int> rssi;
    std::vector<uint32_t> freqs;
    uint32_t tuned = settings_.freq_hz;
    {
        std::lock_guard<std::mutex> lock(mu_);
        rssi = mini_rssi_;
        freqs = mini_freqs_;
        tuned = settings_.freq_hz;
    }

    if (freqs.size() < 2 || rssi.size() != freqs.size()) {
        draw_text(data, mini_stride_, mini_w_, mini_h_, 80, 24, "scanning", ink_dim);
        if (mini_) lv_obj_invalidate(mini_);
        return;
    }

    int lo = rssi[0], hi = rssi[0];
    for (int v : rssi) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    peak_local_ = hi;
    const int floor_db = std::clamp(lo - 5, -120, -40);
    const int ref_db = std::clamp(std::max(hi + 8, floor_db + 20), floor_db + 15, 0);

    const uint32_t start = freqs.front();
    const uint32_t stop = freqs.back();
    const double span = std::max(1.0, static_cast<double>(stop - start));

    auto x_of = [&](uint32_t hz) -> int {
        return plot_left +
               static_cast<int>((static_cast<double>(hz - start) / span) * (plot_right - plot_left));
    };
    auto y_of = [&](int dbm) -> int {
        float t = (static_cast<float>(dbm - floor_db)) / std::max(1.f, static_cast<float>(ref_db - floor_db));
        t = std::clamp(t, 0.f, 1.f);
        return plot_bottom - static_cast<int>(t * (plot_bottom - plot_top));
    };

    hline(data, mini_stride_, mini_w_, mini_h_, plot_left, plot_bottom, plot_right, grid);
    const int thr_y = y_of(settings_.threshold_dbm);
    for (int x = plot_left; x <= plot_right; x += 2)
        put_px(data, mini_stride_, mini_w_, mini_h_, x, thr_y, thr_col);

    for (size_t i = 1; i < freqs.size(); ++i) {
        line(data, mini_stride_, mini_w_, mini_h_, x_of(freqs[i - 1]), y_of(rssi[i - 1]), x_of(freqs[i]),
             y_of(rssi[i]), trace);
    }

    // Center = tuned frequency.
    const int cx = x_of(tuned);
    vline(data, mini_stride_, mini_w_, mini_h_, cx, plot_top, plot_bottom, center_col);
    put_px(data, mini_stride_, mini_w_, mini_h_, cx, y_of(rssi_dbm_), center_col);

    char a[12], b[12], c[12];
    std::snprintf(a, sizeof(a), "%.2f", start / 1000000.0);
    std::snprintf(b, sizeof(b), "%.2f", tuned / 1000000.0);
    std::snprintf(c, sizeof(c), "%.2f", stop / 1000000.0);
    const int ylab = plot_bottom + 2;
    draw_text(data, mini_stride_, mini_w_, mini_h_, plot_left, ylab, a, ink);
    draw_text(data, mini_stride_, mini_w_, mini_h_, std::clamp(cx - 12, plot_left, plot_right - 24), ylab, b,
              center_col);
    draw_text(data, mini_stride_, mini_w_, mini_h_, plot_right - 24, ylab, c, ink);

    if (mini_) lv_obj_invalidate(mini_);
}

void RfListenScreen::geiger_tick() {
    if (!geiger_on_) return;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const int strength = std::clamp(rssi_dbm_ + 100, 0, 80);
    const int period = 800 - strength * 9;
    if (ms - last_geiger_ms_ < period) return;
    last_geiger_ms_ = ms;
    std::string err;
    (void)platform::play_tone(1200.f + static_cast<float>(strength) * 8.f, 12, err);
}

bool RfListenScreen::is_favourite() const {
    return std::find(settings_.favourites.begin(), settings_.favourites.end(), settings_.freq_hz) !=
           settings_.favourites.end();
}

void RfListenScreen::refresh_labels() {
    geiger_tick();
    if (!tuned_lbl_ || !signal_lbl_ || !footer_lbl_) return;

    if (help_on_) {
        lv_label_set_text(tuned_lbl_, "Z/C fine tune   F/X coarse");
        lv_label_set_text(signal_lbl_, "G geiger sound   V favourite   O mod   ?=help");
        lv_label_set_text(footer_lbl_, "Soft: Back  -step  Fav  +step  Home");
        return;
    }

    char tuned[80];
    std::snprintf(tuned, sizeof(tuned), "Tuned  %s MHz%s%s",
                  platform::rf_format_mhz(settings_.freq_hz).c_str(), demo_ ? "  DEMO" : "",
                  is_favourite() ? "  ★" : "");
    lv_label_set_text(tuned_lbl_, tuned);

    // Plain-language activity instead of "FSK/carrier?"
    const char* strength = "weak";
    if (rssi_dbm_ >= settings_.threshold_dbm) strength = "strong";
    else if (rssi_dbm_ >= settings_.threshold_dbm - 15) strength = "medium";

    char sig[120];
    std::snprintf(sig, sizeof(sig), "Signal  %d dBm (%s)  %s   near peak %d   step %u kHz%s", rssi_dbm_,
                  strength, activity_.c_str(), peak_local_, static_cast<unsigned>(settings_.step_hz / 1000),
                  geiger_on_ ? "  GEIGER" : "");
    lv_label_set_text(signal_lbl_, sig);

    lv_label_set_text(footer_lbl_, "Green line = tuned freq    Enter opens Record");
}

void RfListenScreen::retune(int delta_steps) {
    const int64_t next = static_cast<int64_t>(settings_.freq_hz) +
                         static_cast<int64_t>(delta_steps) * static_cast<int64_t>(settings_.step_hz);
    if (next < 300000000 || next > 928000000) return;
    settings_.freq_hz = static_cast<uint32_t>(next);
    platform::rf_push_recent(settings_, settings_.freq_hz);
    platform::save_rf_settings(settings_);
    start_worker();
}

void RfListenScreen::toggle_favourite() {
    auto& f = settings_.favourites;
    const auto it = std::find(f.begin(), f.end(), settings_.freq_hz);
    if (it != f.end()) f.erase(it);
    else {
        f.push_back(settings_.freq_hz);
        if (f.size() > 12) f.erase(f.begin());
    }
    platform::save_rf_settings(settings_);
    refresh_labels();
}

void RfListenScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    worker_ = std::thread([this]() {
        platform::Cc1101Session radio;
        std::string err;
        bool open = false;
        if (cap_ok_) {
            open = radio.open({}, err);
            if (open) {
                (void)radio.apply_preset(settings_.modulation == platform::RfModHint::Fsk
                                             ? platform::Cc1101Preset::Fsk2
                                             : platform::Cc1101Preset::OokAsync,
                                         err);
                (void)radio.set_frequency_hz(settings_.freq_hz, err, true);
            } else {
                demo_ = true;
            }
        }
        while (!cancel_) {
            int dbm = -120;
            std::vector<int> mini;
            std::vector<uint32_t> freqs;
            if (open) {
                (void)radio.read_rssi_dbm(dbm, err);
                const uint32_t start = settings_.freq_hz > settings_.step_hz * 16
                                           ? settings_.freq_hz - settings_.step_hz * 16
                                           : settings_.freq_hz;
                const uint32_t stop = settings_.freq_hz + settings_.step_hz * 16;
                uint32_t step = settings_.step_hz;
                (void)platform::cc1101_plan_sweep(start, stop, step, 32, freqs, err);
                (void)radio.spectrum_sweep(freqs, mini, err, &cancel_);
                (void)radio.set_frequency_hz(settings_.freq_hz, err, true);

                // Activity heuristic from GDO0 duty — keep plain language.
                int high = 0, samples = 0;
                for (int i = 0; i < 40; ++i) {
                    bool g = false;
                    if (radio.read_gdo0(g, err)) {
                        if (g) ++high;
                        ++samples;
                    }
                }
                if (samples > 10) {
                    const float duty = static_cast<float>(high) / samples;
                    if (duty > 0.05f && duty < 0.95f) activity_ = "pulses";
                    else if (duty >= 0.95f) activity_ = "carrier";
                    else activity_ = "quiet";
                }
            } else {
                static float ph = 0;
                ph += 0.2f;
                dbm = static_cast<int>(-70 + 20 * std::sin(ph));
                freqs.resize(32);
                mini.resize(32);
                for (int i = 0; i < 32; ++i) {
                    freqs[i] = settings_.freq_hz - 16 * settings_.step_hz + i * settings_.step_hz;
                    mini[i] = -90 + static_cast<int>(15 * std::sin(ph + i * 0.3f));
                }
                activity_ = "demo";
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                rssi_dbm_ = dbm;
                mini_rssi_ = mini;
                mini_freqs_ = freqs;
            }
            ready_ = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        if (open) radio.close();
    });
}

void RfListenScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

bool RfListenScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_worker();
        platform::save_rf_settings(settings_);
        return KvxScreen::handle_key(key);
    }
    if (key == '?' ) {
        help_on_ = !help_on_;
        refresh_labels();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        // Hand off tuned freq into Record.
        platform::save_rf_settings(settings_);
        view_model().open_tool(model::ToolId::RfRecord);
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
        retune(-1);
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
        retune(1);
        return true;
    }
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        retune(-10);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        retune(10);
        return true;
    }
    if (key == 's' || key == 'S') {
        static const uint32_t kSteps[] = {1000, 5000, 10000, 25000, 100000, 250000};
        size_t idx = 0;
        for (size_t i = 0; i < 6; ++i) {
            if (kSteps[i] == settings_.step_hz) {
                idx = i;
                break;
            }
        }
        settings_.step_hz = kSteps[(idx + 1) % 6];
        platform::save_rf_settings(settings_);
        refresh_labels();
        return true;
    }
    if (key == 'v' || key == 'V') {
        toggle_favourite();
        return true;
    }
    if (key == 'o' || key == 'O') {
        settings_.modulation = settings_.modulation == platform::RfModHint::Ook ? platform::RfModHint::Fsk
                                                                                : platform::RfModHint::Ook;
        platform::save_rf_settings(settings_);
        start_worker();
        return true;
    }
    if (key == 'g' || key == 'G') {
        geiger_on_ = !geiger_on_;
        refresh_labels();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfListenScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { retune(-1); }},
        {view::ICON_CHECK, [this]() { toggle_favourite(); }},
        {view::ICON_PLUS, [this]() { retune(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

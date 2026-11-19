#include "rf_listen_screen.h"
#include "asset_manager.h"

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

    info_ = lv_label_create(root);
    lv_obj_set_pos(info_, 4, 22);
    lv_obj_set_width(info_, 312);
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(info_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_, tok.ink, 0);

    bar_bg_ = lv_obj_create(root);
    lv_obj_remove_style_all(bar_bg_);
    lv_obj_set_size(bar_bg_, 300, 14);
    lv_obj_set_pos(bar_bg_, 10, 56);
    lv_obj_set_style_bg_color(bar_bg_, tok.surface, 0);
    lv_obj_set_style_bg_opa(bar_bg_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_bg_, 3, 0);

    bar_fg_ = lv_obj_create(bar_bg_);
    lv_obj_remove_style_all(bar_fg_);
    lv_obj_set_size(bar_fg_, 2, 14);
    lv_obj_set_pos(bar_fg_, 0, 0);
    lv_obj_set_style_bg_color(bar_fg_, tok.main, 0);
    lv_obj_set_style_bg_opa(bar_fg_, LV_OPA_COVER, 0);

    mini_buf_ = lv_draw_buf_create(300, 48, LV_COLOR_FORMAT_RGB565, 0);
    if (mini_buf_ && mini_buf_->data) {
        mini_stride_ = static_cast<int>(mini_buf_->header.stride);
        mini_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(mini_, mini_buf_);
        lv_obj_set_pos(mini_, 10, 78);
    }

    const auto cap = platform::detect_cc1101();
    cap_ok_ = cap.present;
    demo_ = !cap_ok_;
    refresh_labels();
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
    // Update bar
    const float t =
        std::clamp((rssi_dbm_ + 100) / 80.f, 0.f, 1.f);
    if (bar_fg_) lv_obj_set_width(bar_fg_, std::max(2, static_cast<int>(300 * t)));
    // Mini spectrum
    if (mini_buf_ && mini_buf_->data && !mini_rssi_.empty()) {
        auto* data = static_cast<uint8_t*>(mini_buf_->data);
        const uint16_t bg = pack565(8, 10, 14);
        for (int y = 0; y < 48; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(data + y * mini_stride_);
            for (int x = 0; x < 300; ++x) row[x] = bg;
        }
        const int n = static_cast<int>(mini_rssi_.size());
        for (int i = 0; i < n; ++i) {
            const int x = i * 299 / std::max(1, n - 1);
            const float a = std::clamp((mini_rssi_[i] + 100) / 80.f, 0.f, 1.f);
            const int h = static_cast<int>(a * 46);
            const uint16_t col = pack565(255, static_cast<uint8_t>(180 + 40 * a), 40);
            for (int y = 47 - h; y < 48; ++y) {
                reinterpret_cast<uint16_t*>(data + y * mini_stride_)[x] = col;
            }
        }
        if (mini_) lv_obj_invalidate(mini_);
    }
}

void RfListenScreen::refresh_labels() {
    if (!info_) return;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s MHz  step %uk  %d dBm  %s%s\nZ/C tune  Fav:Sym+F  Recents in settings",
                  platform::rf_format_mhz(settings_.freq_hz).c_str(),
                  static_cast<unsigned>(settings_.step_hz / 1000), rssi_dbm_, mod_hint_.c_str(),
                  demo_ ? " DEMO" : "");
    lv_label_set_text(info_, buf);
}

void RfListenScreen::retune(int delta_steps) {
    const int64_t next = static_cast<int64_t>(settings_.freq_hz) +
                         static_cast<int64_t>(delta_steps) * static_cast<int64_t>(settings_.step_hz);
    if (next < 300000000 || next > 928000000) return;
    settings_.freq_hz = static_cast<uint32_t>(next);
    platform::rf_push_recent(settings_, settings_.freq_hz);
    platform::save_rf_settings(settings_);
    // Restart worker to apply frequency.
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
                // Mini sweep ± step*16
                const uint32_t start = settings_.freq_hz > settings_.step_hz * 16
                                           ? settings_.freq_hz - settings_.step_hz * 16
                                           : settings_.freq_hz;
                const uint32_t stop = settings_.freq_hz + settings_.step_hz * 16;
                uint32_t step = settings_.step_hz;
                (void)platform::cc1101_plan_sweep(start, stop, step, 32, freqs, err);
                (void)radio.spectrum_sweep(freqs, mini, err, &cancel_);
                (void)radio.set_frequency_hz(settings_.freq_hz, err, true);

                // Modulation heuristic from GDO0 duty.
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
                    if (duty > 0.05f && duty < 0.95f) mod_hint_ = "OOK?";
                    else if (duty >= 0.95f) mod_hint_ = "FSK/carrier?";
                    else mod_hint_ = "quiet";
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
                mod_hint_ = "demo";
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
        // Cycle step
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

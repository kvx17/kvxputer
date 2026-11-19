#include "rf_send_test_screen.h"
#include "asset_manager.h"

#include "cap_detect.h"
#include "cc1101.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>

namespace screen {

RfSendTestScreen::RfSendTestScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets)
    : KvxScreen(vm, assets) {
    settings_ = platform::load_rf_settings();
    init();
}

RfSendTestScreen::~RfSendTestScreen() {
    stop_tx();
    if (timer_) lv_timer_delete(timer_);
}

void RfSendTestScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "RF Send Test");

    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 6, 24);
    lv_obj_set_width(label_, 308);
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);

    status_lbl_ = lv_label_create(root);
    lv_obj_set_pos(status_lbl_, 6, 90);
    lv_obj_set_width(status_lbl_, 308);
    lv_obj_set_style_text_font(status_lbl_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_lbl_, tok.accent, 0);

    (void)platform::rf_tx_allowed(settings_.region, settings_.freq_hz, block_reason_);
    refresh();
    timer_ = lv_timer_create(timer_cb, 100, this);
}

void RfSendTestScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfSendTestScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void RfSendTestScreen::refresh() {
    if (!label_) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Freq %s MHz\nPA %d  Dur %d ms  Region %s\nZ/C freq  F/X dur  P pa  Enter TX\nAny key stops TX",
                  platform::rf_format_mhz(settings_.freq_hz).c_str(), static_cast<int>(settings_.pa),
                  settings_.tx_duration_ms, settings_.region == platform::RfRegion::Us ? "US" : "EU");
    lv_label_set_text(label_, buf);
    if (status_lbl_) {
        if (!block_reason_.empty() && !tx_active_) lv_label_set_text(status_lbl_, block_reason_.c_str());
        else lv_label_set_text(status_lbl_, status_.c_str());
    }
}

void RfSendTestScreen::start_tx() {
    stop_tx();
    if (!platform::rf_tx_allowed(settings_.region, settings_.freq_hz, block_reason_)) {
        status_ = "TX blocked";
        refresh();
        return;
    }
    const auto cap = platform::detect_cc1101();
    if (!cap.present) {
        status_ = "Cap not present";
        refresh();
        return;
    }
    cancel_ = false;
    tx_active_ = true;
    status_ = "TX CARRIER";
    refresh();
    const uint32_t hz = settings_.freq_hz;
    const auto pa = platform::rf_clamp_pa(settings_.region, settings_.pa);
    const int dur = std::min(2000, settings_.tx_duration_ms);
    worker_ = std::thread([this, hz, pa, dur]() {
        platform::Cc1101Session radio;
        std::string err;
        if (!radio.open({}, err)) {
            status_ = err;
            tx_active_ = false;
            return;
        }
        (void)radio.tx_carrier(hz, pa, dur, err, &cancel_);
        (void)radio.stop_tx(err);
        radio.close();
        status_ = cancel_ ? "Stopped" : "Done";
        tx_active_ = false;
    });
}

void RfSendTestScreen::stop_tx() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    tx_active_ = false;
    if (status_ == "TX CARRIER") status_ = "Stopped";
}

bool RfSendTestScreen::handle_key(uint32_t key) {
    if (tx_active_) {
        stop_tx();
        refresh();
        if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
        return true;
    }
    if (key == LV_KEY_ESC) {
        platform::save_rf_settings(settings_);
        return KvxScreen::handle_key(key);
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        start_tx();
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
        settings_.freq_hz = settings_.freq_hz > settings_.step_hz ? settings_.freq_hz - settings_.step_hz
                                                                  : settings_.freq_hz;
        (void)platform::rf_tx_allowed(settings_.region, settings_.freq_hz, block_reason_);
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
        settings_.freq_hz += settings_.step_hz;
        (void)platform::rf_tx_allowed(settings_.region, settings_.freq_hz, block_reason_);
        return true;
    }
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        settings_.tx_duration_ms = std::min(2000, settings_.tx_duration_ms + 100);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        settings_.tx_duration_ms = std::max(100, settings_.tx_duration_ms - 100);
        return true;
    }
    if (key == 'p' || key == 'P') {
        settings_.pa = static_cast<platform::Cc1101PaLevel>((static_cast<int>(settings_.pa) + 1) % 4);
        return true;
    }
    if (key == 'g' || key == 'G') {
        settings_.region = settings_.region == platform::RfRegion::Eu ? platform::RfRegion::Us
                                                                     : platform::RfRegion::Eu;
        (void)platform::rf_tx_allowed(settings_.region, settings_.freq_hz, block_reason_);
        platform::save_rf_settings(settings_);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfSendTestScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS,
         [this]() {
             settings_.tx_duration_ms = std::max(100, settings_.tx_duration_ms - 100);
             refresh();
         }},
        {view::ICON_PLAY, [this]() { start_tx(); }},
        {view::ICON_STOP, [this]() { stop_tx(); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

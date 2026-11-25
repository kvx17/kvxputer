#include "rf_send_code_screen.h"
#include "asset_manager.h"

#include "cap_detect.h"
#include "cc1101.h"
#include "rf_decode.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace screen {
namespace {

const char* kProtos[] = {"Princeton", "EV1527", "PT2262", "CAME", "Linear", "NICE_FLO", "Holtek"};
constexpr int kProtoCount = 7;

int default_bits(int idx) {
    if (idx == 4) return 10; // Linear
    if (idx == 3 || idx == 5 || idx == 6) return 12;
    return 24;
}

} // namespace

RfSendCodeScreen::RfSendCodeScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets)
    : KvxScreen(vm, assets) {
    settings_ = platform::load_rf_settings();
    bits_ = default_bits(proto_idx_);
    init();
}

RfSendCodeScreen::~RfSendCodeScreen() {
    stop_tx();
    if (timer_) lv_timer_delete(timer_);
}

void RfSendCodeScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Send Code");
    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 4, 20);
    lv_obj_set_width(label_, 312);
    lv_obj_set_height(label_, 120);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    refresh();
    timer_ = lv_timer_create(timer_cb, 200, this);
}

void RfSendCodeScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfSendCodeScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void RfSendCodeScreen::refresh() {
    if (!label_) return;
    char buf[400];
    if (editing_key_) {
        std::snprintf(buf, sizeof(buf), "Key hex:\n%s_\nEnter accept  Esc cancel", key_buf_.c_str());
        lv_label_set_text(label_, buf);
        return;
    }
    auto mark = [&](Field f) { return field_ == f ? ">" : " "; };
    std::snprintf(buf, sizeof(buf),
                  "%sProto %s\n"
                  "%sBits  %d\n"
                  "%sKey   0x%llX\n"
                  "%sFreq  %s MHz  %s\n"
                  "F/X field  Z/C change  K edit key\nEnter TX  %s",
                  mark(Field::Proto), kProtos[proto_idx_], mark(Field::Bits), bits_, mark(Field::Key),
                  static_cast<unsigned long long>(key_), mark(Field::Freq),
                  platform::rf_format_mhz(settings_.freq_hz).c_str(),
                  settings_.region == platform::RfRegion::Us ? "US" : "EU", status_.c_str());
    lv_label_set_text(label_, buf);
}

void RfSendCodeScreen::cycle_proto(int delta) {
    proto_idx_ = (proto_idx_ + delta + kProtoCount) % kProtoCount;
    bits_ = default_bits(proto_idx_);
}

void RfSendCodeScreen::stop_tx() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    tx_active_ = false;
}

void RfSendCodeScreen::start_tx() {
    stop_tx();
    std::string reason;
    if (!platform::rf_tx_allowed(settings_.region, settings_.freq_hz, reason)) {
        status_ = reason;
        return;
    }
    std::vector<int> timings;
    std::string err;
    if (!platform::encode_ook_protocol(kProtos[proto_idx_], key_, bits_, timings, err)) {
        status_ = err;
        return;
    }
    int on_air = 0;
    for (int t : timings) {
        if (t > 0) on_air += (t + 999) / 1000;
    }
    if (!platform::rf_duty_reserve(settings_.freq_hz, on_air, reason)) {
        status_ = reason;
        return;
    }
    if (!platform::detect_cc1101().present) {
        status_ = "Cap not present";
        return;
    }
    cancel_ = false;
    tx_active_ = true;
    status_ = "TX…";
    const uint32_t hz = settings_.freq_hz;
    const auto pa = platform::rf_clamp_pa(settings_.region, settings_.pa);
    worker_ = std::thread([this, hz, timings, pa]() {
        platform::Cc1101Session radio;
        std::string e;
        if (!radio.open({}, e)) {
            status_ = e;
            tx_active_ = false;
            return;
        }
        (void)radio.tx_raw_timings(hz, pa, timings, 1, 0, e, &cancel_);
        radio.close();
        status_ = cancel_ ? "Stopped" : "Sent";
        tx_active_ = false;
    });
}

bool RfSendCodeScreen::handle_key(uint32_t key) {
    if (tx_active_) {
        stop_tx();
        if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
        return true;
    }
    if (editing_key_) {
        if (key == LV_KEY_ESC) {
            editing_key_ = false;
            return true;
        }
        if (key == LV_KEY_ENTER) {
            if (!key_buf_.empty()) key_ = std::strtoull(key_buf_.c_str(), nullptr, 16);
            editing_key_ = false;
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == 8) {
            if (!key_buf_.empty()) key_buf_.pop_back();
            return true;
        }
        if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F')) {
            if (key_buf_.size() < 16) key_buf_.push_back(static_cast<char>(key));
            return true;
        }
        return true;
    }
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        field_ = static_cast<Field>((static_cast<int>(field_) + static_cast<int>(Field::Count) - 1) %
                                    static_cast<int>(Field::Count));
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        field_ = static_cast<Field>((static_cast<int>(field_) + 1) % static_cast<int>(Field::Count));
        return true;
    }
    if (key == 'k' || key == 'K') {
        editing_key_ = true;
        char tmp[24];
        std::snprintf(tmp, sizeof(tmp), "%llX", static_cast<unsigned long long>(key_));
        key_buf_ = tmp;
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z' || key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
        const int d = (key == LV_KEY_RIGHT || key == 'c' || key == 'C') ? 1 : -1;
        switch (field_) {
            case Field::Proto: cycle_proto(d); break;
            case Field::Bits: bits_ = std::clamp(bits_ + d, 8, 32); break;
            case Field::Key: key_ = d > 0 ? key_ + 1 : (key_ ? key_ - 1 : 0); break;
            case Field::Freq:
                settings_.freq_hz = d > 0 ? settings_.freq_hz + settings_.step_hz
                                          : (settings_.freq_hz > settings_.step_hz
                                                 ? settings_.freq_hz - settings_.step_hz
                                                 : settings_.freq_hz);
                platform::save_rf_settings(settings_);
                break;
            case Field::Count: break;
        }
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        start_tx();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfSendCodeScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP,
         [this]() {
             field_ = static_cast<Field>((static_cast<int>(field_) + static_cast<int>(Field::Count) - 1) %
                                         static_cast<int>(Field::Count));
         }},
        {view::ICON_PLAY, [this]() { start_tx(); }},
        {view::ICON_CARET_DOWN,
         [this]() {
             field_ = static_cast<Field>((static_cast<int>(field_) + 1) % static_cast<int>(Field::Count));
         }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

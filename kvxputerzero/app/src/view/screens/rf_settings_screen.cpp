#include "rf_settings_screen.h"
#include "asset_manager.h"

#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screen {
namespace {

const char* preset_label(platform::Cc1101Preset p) {
    switch (p) {
        case platform::Cc1101Preset::SpectrumWide: return "OOK650";
        case platform::Cc1101Preset::OokAsync: return "OOK270";
        case platform::Cc1101Preset::Fsk2: return "2-FSK";
    }
    return "OOK270";
}

} // namespace

RfSettingsScreen::RfSettingsScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets)
    : KvxScreen(vm, assets) {
    settings_ = platform::load_rf_settings();
    init();
}

void RfSettingsScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "RF Settings");
    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 4, 20);
    lv_obj_set_width(label_, 312);
    lv_obj_set_height(label_, 120);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    refresh();
}

void RfSettingsScreen::refresh() {
    if (!label_) return;
    auto mark = [&](Row r) { return row_ == static_cast<int>(r) ? ">" : " "; };
    char buf[512];
    std::snprintf(
        buf, sizeof(buf),
        "%sRegion  %s\n"
        "%sPreset  %s\n"
        "%sPA      %d\n"
        "%sStep    %u Hz\n"
        "%sRX BW   %.0f kHz\n"
        "%sThr     %d dBm\n"
        "%sFloor   %d dBm\n"
        "%sRef     %d dBm\n"
        "%sRSSI cal %+d dB\n"
        "%sRecord  %d ms\n"
        "F/X row  Z/C change  Enter save",
        mark(Row::Region), settings_.region == platform::RfRegion::Us ? "US" : "EU",
        mark(Row::Preset), preset_label(settings_.preset), mark(Row::Pa), static_cast<int>(settings_.pa),
        mark(Row::Step), settings_.step_hz, mark(Row::RxBw), static_cast<double>(settings_.rx_bw_khz),
        mark(Row::Threshold), settings_.threshold_dbm, mark(Row::Floor), settings_.floor_dbm,
        mark(Row::Ref), settings_.ref_level_dbm, mark(Row::RssiCal), settings_.rssi_cal_db,
        mark(Row::RecordMax), settings_.record_max_ms);
    lv_label_set_text(label_, buf);
}

void RfSettingsScreen::adjust(int delta) {
    switch (static_cast<Row>(row_)) {
        case Row::Region:
            settings_.region =
                settings_.region == platform::RfRegion::Eu ? platform::RfRegion::Us : platform::RfRegion::Eu;
            break;
        case Row::Preset: {
            int p = static_cast<int>(settings_.preset) + delta;
            if (p < 0) p = 2;
            if (p > 2) p = 0;
            settings_.preset = static_cast<platform::Cc1101Preset>(p);
            settings_.modulation =
                settings_.preset == platform::Cc1101Preset::Fsk2 ? platform::RfModHint::Fsk
                                                                : platform::RfModHint::Ook;
            break;
        }
        case Row::Pa: {
            int p = static_cast<int>(settings_.pa) + delta;
            settings_.pa = static_cast<platform::Cc1101PaLevel>(std::clamp(p, 0, 3));
            break;
        }
        case Row::Step: {
            if (delta > 0) settings_.step_hz = std::min(500000u, settings_.step_hz * 2);
            else settings_.step_hz = std::max(1000u, settings_.step_hz / 2);
            break;
        }
        case Row::RxBw: {
            static const float kBw[] = {58.f, 101.f, 203.f, 270.f, 406.f, 650.f, 812.f};
            int idx = 3;
            for (int i = 0; i < 7; ++i) {
                if (std::fabs(settings_.rx_bw_khz - kBw[i]) < 1.f) {
                    idx = i;
                    break;
                }
            }
            idx = std::clamp(idx + delta, 0, 6);
            settings_.rx_bw_khz = kBw[idx];
            break;
        }
        case Row::Threshold:
            settings_.threshold_dbm = std::clamp(settings_.threshold_dbm + delta * 2, -110, -10);
            break;
        case Row::Floor:
            settings_.floor_dbm = std::clamp(settings_.floor_dbm + delta * 2, -120, -40);
            break;
        case Row::Ref:
            settings_.ref_level_dbm = std::clamp(settings_.ref_level_dbm + delta * 5, -40, 0);
            break;
        case Row::RssiCal:
            settings_.rssi_cal_db = std::clamp(settings_.rssi_cal_db + delta, -30, 30);
            break;
        case Row::RecordMax:
            settings_.record_max_ms = std::clamp(settings_.record_max_ms + delta * 500, 500, 30000);
            break;
        case Row::Count:
            break;
    }
    refresh();
}

void RfSettingsScreen::save() {
    platform::save_rf_settings(settings_);
    refresh();
}

bool RfSettingsScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        save();
        return KvxScreen::handle_key(key);
    }
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        row_ = (row_ + static_cast<int>(Row::Count) - 1) % static_cast<int>(Row::Count);
        refresh();
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        row_ = (row_ + 1) % static_cast<int>(Row::Count);
        refresh();
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
        adjust(-1);
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
        adjust(1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        save();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfSettingsScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP,
         [this]() {
             row_ = (row_ + static_cast<int>(Row::Count) - 1) % static_cast<int>(Row::Count);
             refresh();
         }},
        {view::ICON_CHECK, [this]() { save(); }},
        {view::ICON_CARET_DOWN,
         [this]() {
             row_ = (row_ + 1) % static_cast<int>(Row::Count);
             refresh();
         }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

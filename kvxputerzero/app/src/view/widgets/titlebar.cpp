/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "titlebar.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

#include "asset_manager.h"
#include "device_status.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

constexpr uint32_t kDeviceRefreshTicks = 5;

const char* wifi_icon(int32_t strength_percent, bool connected) {
    if (!connected || strength_percent < 0) {
        return ICON_WIFI_NONE;
    }
    if (strength_percent < 35) {
        return ICON_WIFI_LOW;
    }
    if (strength_percent < 70) {
        return ICON_WIFI_MEDIUM;
    }
    return ICON_WIFI_HIGH;
}

const char* battery_icon(int32_t capacity_percent, bool charging) {
    if (charging) {
        return ICON_BAT_CHARGING;
    }
    if (capacity_percent >= 95) {
        return ICON_BAT_FULL;
    }
    if (capacity_percent >= 70) {
        return ICON_BAT_HIGH;
    }
    if (capacity_percent >= 35) {
        return ICON_BAT_MEDIUM;
    }
    if (capacity_percent >= 10) {
        return ICON_BAT_LOW;
    }
    return ICON_BAT_EMPTY;
}

void set_visible(lv_obj_t* obj, bool visible) {
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

TitleBar::TitleBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

TitleBar::~TitleBar() {
    if (refresh_timer_) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
}

void TitleBar::build() {
    if (core_obj_) {
        return;
    }

    const auto tokens = view::make_tokens(view_model_.pri_color(), view_model_.sec_color(),
                                          view_model_.bg_color());

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kTitleBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    view::style_bar(core_obj_, tokens);
    lv_obj_set_style_border_width(core_obj_, 1, 0);
    lv_obj_set_style_border_side(core_obj_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(core_obj_, tokens.rule, 0);
    lv_obj_set_style_border_opa(core_obj_, LV_OPA_COVER, 0);

    auto* title = lv_label_create(core_obj_);
    lv_label_bind_text(title, view_model_.title_subject(), nullptr);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 110);
    const lv_font_t* title_font = assets_.load_standard_font(11);
    lv_obj_set_style_text_font(title, title_font ? title_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, tokens.ink, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 6, 0);

    auto* status = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, 200, LV_PCT(100));
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 2, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t* icon_font = assets_.load_font("Phosphor-Fill.ttf", 12);
    if (!icon_font) {
        icon_font = &lv_font_montserrat_12;
    }
    const lv_font_t* status_font =
        assets_.load_standard_font(11, app::StandardFontWeight::Regular);
    if (!status_font) {
        status_font = &lv_font_montserrat_12;
    }
    auto style_status = [&](lv_obj_t* label, const lv_font_t* font, int32_t width) {
        lv_obj_set_width(label, width);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, tokens.ink, 0);
    };
    wifi_label_ = lv_label_create(status);
    style_status(wifi_label_, icon_font, 14);
    ethernet_label_ = lv_label_create(status);
    style_status(ethernet_label_, icon_font, 14);
    battery_icon_label_ = lv_label_create(status);
    style_status(battery_icon_label_, icon_font, 14);
    battery_percent_label_ = lv_label_create(status);
    style_status(battery_percent_label_, status_font, 30);
    time_label_ = lv_label_create(status);
    style_status(time_label_, status_font, 34);

    refresh_time();
    refresh_device_status();
    refresh_timer_ = lv_timer_create(refresh_timer_cb, 1000, this);
}

void TitleBar::refresh_time() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    std::array<char, 6> text{};
    std::strftime(text.data(), text.size(), "%H:%M", &local_time);
    lv_label_set_text(time_label_, text.data());
}

void TitleBar::refresh_device_status() {
    const auto status = platform::read_device_status();

    lv_label_set_text(wifi_label_, wifi_icon(status.wifi_strength_percent, status.wifi_connected));
    set_visible(wifi_label_, status.wifi_connected);

    lv_label_set_text(ethernet_label_, ICON_ETHERNET);
    set_visible(ethernet_label_, status.ethernet_connected);

    lv_label_set_text(battery_icon_label_,
                      battery_icon(status.battery_percent, status.battery_charging));
    set_visible(battery_icon_label_, status.battery_present);
    set_visible(battery_percent_label_, status.battery_present);
    if (status.battery_present) {
        std::array<char, 6> percent{};
        std::snprintf(percent.data(), percent.size(), "%d%%", std::clamp(status.battery_percent, 0, 100));
        lv_label_set_text(battery_percent_label_, percent.data());
    }
}

void TitleBar::refresh_timer_cb(lv_timer_t* timer) {
    auto* title_bar = static_cast<TitleBar*>(lv_timer_get_user_data(timer));
    if (!title_bar) {
        return;
    }
    title_bar->refresh_time();
    if (++title_bar->refresh_tick_ >= kDeviceRefreshTicks) {
        title_bar->refresh_tick_ = 0;
        title_bar->refresh_device_status();
    }
}

} // namespace view::widgets

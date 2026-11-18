/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */


#include "icon_button.h"

#include "theme.h"

namespace view::widgets {
namespace {

constexpr int32_t kIndicatorWidth  = 2;
constexpr int32_t kIndicatorHeight = 6;
constexpr int32_t kLabelY          = -3;
constexpr int32_t kIndicatorY      = 0;

} // namespace

IconButton::IconButton(lv_obj_t* parent,
                       viewmodel::BaseViewModel& view_model,
                       int32_t width,
                       int32_t height,
                       const char* text,
                       const lv_font_t* font,
                       lv_color_t light_color,
                       lv_color_t dark_color,
                       lv_event_cb_t click_cb,
                       void* user_data,
                       bool show_indicator)
    : BaseWidgets(parent),
      view_model_(view_model),
      width_(width),
      height_(height),
      text_(text ? text : ""),
      font_(font),
      light_color_(light_color),
      dark_color_(dark_color),
      click_cb_(click_cb),
      user_data_(user_data),
      show_indicator_(show_indicator) {}

void IconButton::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_button_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, width_, height_);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(core_obj_, 0, 0);
    lv_obj_set_style_shadow_width(core_obj_, 0, 0);
    lv_obj_set_style_pad_all(core_obj_, 0, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    if (click_cb_) {
        lv_obj_add_event_cb(core_obj_, click_cb_, LV_EVENT_CLICKED, user_data_ ? user_data_ : &view_model_);
    }

    label_ = lv_label_create(core_obj_);
    lv_label_set_text(label_, text_.c_str());
    if (font_) {
        lv_obj_set_style_text_font(label_, font_, 0);
    }
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, kLabelY);

    if (show_indicator_) {
        indicator_ = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(indicator_);
        lv_obj_set_size(indicator_, kIndicatorWidth, kIndicatorHeight);
        lv_obj_set_style_radius(indicator_, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(indicator_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(indicator_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(indicator_, LV_ALIGN_BOTTOM_MID, 0, kIndicatorY);
    }

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
    apply_theme(view_model_.is_dark_mode());
}

void IconButton::set_text(const char* text) {
    text_ = text ? text : "";
    if (label_) {
        lv_label_set_text(label_, text_.c_str());
        lv_obj_align(label_, LV_ALIGN_CENTER, 0, kLabelY);
    }
    apply_indicator_style();
}

void IconButton::set_font(const lv_font_t* font) {
    font_ = font;
    if (label_ && font_) {
        lv_obj_set_style_text_font(label_, font_, 0);
        lv_obj_align(label_, LV_ALIGN_CENTER, 0, kLabelY);
    }
}

void IconButton::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!core_obj_) {
        return;
    }

    if (enabled_) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    }
    apply_theme(view_model_.is_dark_mode());
}

void IconButton::theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* button = static_cast<IconButton*>(lv_observer_get_user_data(observer));
    if (!button) {
        return;
    }

    button->apply_theme(lv_subject_get_int(subject) != 0);
}

void IconButton::apply_theme(bool dark_mode) {
    if (!core_obj_ || !label_) {
        return;
    }

    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_TRANSP, 0);
    if (!enabled_) {
        lv_obj_set_style_text_color(label_, view::palette(dark_mode).text_disabled, 0);
        apply_indicator_style();
        return;
    }

    lv_obj_set_style_text_color(label_, dark_mode ? dark_color_ : light_color_, 0);
    apply_indicator_style();
}

void IconButton::apply_indicator_style() {
    if (!indicator_) {
        return;
    }

    const bool show_indicator = enabled_ && !text_.empty();
    lv_obj_set_style_bg_opa(indicator_, show_indicator ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(indicator_, view_model_.is_dark_mode() ? dark_color_ : light_color_, 0);
    lv_obj_align(indicator_, LV_ALIGN_BOTTOM_MID, 0, kIndicatorY);
}

} // namespace view::widgets

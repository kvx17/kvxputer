/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "apple_screen.h"

#include "asset_manager.h"
#include "bindings.h"

namespace screen {
namespace {

struct AppleTextFontBinding {
    lv_font_t* regular;
    lv_font_t* bold;
};

constexpr const char* kIconKbFn          = "\uE080";
constexpr const char* kIconKbH           = "\uE084";
constexpr const char* kIconKbJ           = "\uE08C";
constexpr const char* kIconKbPrintScreen = "\uE0B1";
constexpr const char* kIconKbHelp        = "\uEA28";

void cleanup_font_binding(lv_event_t* event) {
    delete static_cast<AppleTextFontBinding*>(lv_event_get_user_data(event));
}

void apple_text_font_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    auto* fonts = static_cast<AppleTextFontBinding*>(lv_observer_get_user_data(observer));
    if (!label || !fonts) {
        return;
    }

    auto* font = lv_subject_get_int(subject) ? fonts->bold : fonts->regular;
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_20, 0);
}

void info_visible_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    if (!label) {
        return;
    }

    if (lv_subject_get_int(subject)) {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

AppleScreen::AppleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AppleScreen::build_content(lv_obj_t* content) {
    auto* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 4, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(group);

    auto* hello = lv_label_create(group);
    lv_label_bind_text(hello, view_model().greeting_subject(), nullptr);
    auto* fonts = new AppleTextFontBinding{
        assets().load_standard_font(20, app::StandardFontWeight::Regular),
        assets().load_standard_font(20),
    };
    auto* initial_font = lv_subject_get_int(view_model().bold_text_subject()) ? fonts->bold : fonts->regular;
    lv_obj_set_style_text_font(hello, initial_font ? initial_font : &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(hello, cleanup_font_binding, LV_EVENT_DELETE, fonts);
    lv_subject_add_observer_obj(view_model().bold_text_subject(), apple_text_font_observer, hello, fonts);
    reactive::bind_theme(hello, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* info = lv_label_create(group);
    lv_label_set_text_fmt(info, "LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    auto* info_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    lv_obj_set_style_text_font(info, info_font ? info_font : &lv_font_montserrat_12, 0);
    if (!lv_subject_get_int(view_model().info_visible_subject())) {
        lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
    }
    lv_subject_add_observer_obj(view_model().info_visible_subject(), info_visible_observer, info, nullptr);
    reactive::bind_theme(info, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* hints = lv_obj_create(group);
    lv_obj_remove_style_all(hints);
    lv_obj_set_size(hints, 220, 50);
    lv_obj_set_flex_flow(hints, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hints,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hints, 2, 0);
    lv_obj_add_flag(hints, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(hints, LV_OBJ_FLAG_SCROLLABLE);

    auto* key_icon_font = assets().load_font("kenney_input_keyboard_and_mouse.ttf", 34);
    auto* extra_key_icon_font = assets().load_font("kenny_keyboard_extra.ttf", 34);
    auto* hint_font = assets().load_standard_font(11, app::StandardFontWeight::Regular);
    const auto add_hint = [&](const char* letter_icon,
                              const char* letter_fallback,
                              const char* action_icon,
                              const char* action_fallback,
                              const lv_font_t* action_font,
                              const char* description_text) {
        auto* item = lv_obj_create(hints);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 196, 24);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 4, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        const auto add_key = [&](const char* icon,
                                 const char* fallback,
                                 const lv_font_t* icon_font) {
            auto* key = lv_obj_create(item);
            lv_obj_remove_style_all(key);
            lv_obj_set_size(key, 26, 24);
            lv_obj_add_flag(key, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);

            auto* icon_label = lv_label_create(key);
            lv_label_set_text(icon_label, icon_font ? icon : fallback);
            lv_obj_set_style_text_font(icon_label,
                                       icon_font ? icon_font : &lv_font_montserrat_12,
                                       0);
            lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, icon_font ? -3 : 0);
            reactive::bind_theme(icon_label,
                                 view_model().dark_mode_subject(),
                                 reactive::ThemeRole::Text);
        };

        const auto add_text = [&](const char* text) {
            auto* label = lv_label_create(item);
            lv_label_set_text(label, text);
            lv_obj_set_style_text_font(label,
                                       hint_font ? hint_font : &lv_font_montserrat_12,
                                       0);
            reactive::bind_theme(label,
                                 view_model().dark_mode_subject(),
                                 reactive::ThemeRole::Text);
        };

        add_key(kIconKbFn, "FN", key_icon_font);
        add_text("+");
        add_key(letter_icon, letter_fallback, key_icon_font);
        add_text("=");
        add_key(action_icon, action_fallback, action_font);
        add_text(description_text);
    };

    add_hint(kIconKbH, "H", kIconKbHelp, "?", extra_key_icon_font, "Help");
    add_hint(kIconKbJ,
             "J",
             kIconKbPrintScreen,
             "PRSC",
             key_icon_font,
             "Screenshot");
}

} // namespace screen

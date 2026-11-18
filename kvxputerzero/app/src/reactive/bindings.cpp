/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "bindings.h"

#include "theme.h"

#include <cstdint>

namespace reactive {
namespace {

struct BoolTextBinding {
    const char* false_text;
    const char* true_text;
};

void cleanup_bool_text_binding(lv_event_t* event) {
    delete static_cast<BoolTextBinding*>(lv_event_get_user_data(event));
}

void bool_text_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    auto* binding = static_cast<BoolTextBinding*>(lv_observer_get_user_data(observer));
    if (!label || !binding) {
        return;
    }

    lv_label_set_text(label, lv_subject_get_int(subject) ? binding->true_text : binding->false_text);
}

void set_common_box_style(lv_obj_t* obj) {
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

void theme_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* obj = lv_observer_get_target_obj(observer);
    if (!obj) {
        return;
    }

    const auto role = static_cast<ThemeRole>(reinterpret_cast<uintptr_t>(lv_observer_get_user_data(observer)));
    const auto colors = view::palette(lv_subject_get_int(subject) != 0);

    switch (role) {
        case ThemeRole::Screen:
            set_common_box_style(obj);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, colors.background, 0);
            lv_obj_set_style_text_color(obj, colors.text, 0);
            break;
        case ThemeRole::Surface:
            set_common_box_style(obj);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, colors.surface, 0);
            lv_obj_set_style_text_color(obj, colors.text, 0);
            break;
        case ThemeRole::Bar:
            set_common_box_style(obj);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, colors.bar, 0);
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_color(obj, colors.border, 0);
            lv_obj_set_style_text_color(obj, colors.text, 0);
            break;
        case ThemeRole::Button:
            lv_obj_set_style_radius(obj, 4, 0);
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_color(obj, colors.border, 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(obj, colors.button, 0);
            lv_obj_set_style_text_color(obj, colors.text, 0);
            lv_obj_set_style_pad_hor(obj, 8, 0);
            lv_obj_set_style_pad_ver(obj, 3, 0);
            break;
        case ThemeRole::Text:
            lv_obj_set_style_text_color(obj, colors.text, 0);
            break;
    }
}

} // namespace

void bind_theme(lv_obj_t* obj, lv_subject_t* dark_mode_subject, ThemeRole role) {
    if (!obj || !dark_mode_subject) {
        return;
    }

    lv_subject_add_observer_obj(dark_mode_subject,
                                theme_observer,
                                obj,
                                reinterpret_cast<void*>(static_cast<uintptr_t>(role)));
}

void bind_theme(lv_obj_t* obj, Subject& dark_mode_subject, ThemeRole role) {
    bind_theme(obj, dark_mode_subject.native(), role);
}

void bind_bool_text(lv_obj_t* label,
                    lv_subject_t* bool_subject,
                    const char* false_text,
                    const char* true_text) {
    if (!label || !bool_subject) {
        return;
    }

    auto* binding = new BoolTextBinding{false_text, true_text};
    lv_obj_add_event_cb(label, cleanup_bool_text_binding, LV_EVENT_DELETE, binding);
    lv_subject_add_observer_obj(bool_subject, bool_text_observer, label, binding);
}

void bind_bool_text(lv_obj_t* label,
                    Subject& bool_subject,
                    const char* false_text,
                    const char* true_text) {
    bind_bool_text(label, bool_subject.native(), false_text, true_text);
}

lv_observer_t* observe(lv_subject_t* subject, lv_observer_cb_t callback, void* user_data) {
    if(!subject || !callback) {
        return nullptr;
    }

    return lv_subject_add_observer(subject, callback, user_data);
}

lv_observer_t* observe(Subject& subject, lv_observer_cb_t callback, void* user_data) {
    return observe(subject.native(), callback, user_data);
}

lv_observer_t* observe_obj(lv_obj_t* obj,
                           lv_subject_t* subject,
                           lv_observer_cb_t callback,
                           void* user_data) {
    if(!obj || !subject || !callback) {
        return nullptr;
    }

    return lv_subject_add_observer_obj(subject, callback, obj, user_data);
}

lv_observer_t* observe_obj(lv_obj_t* obj,
                           Subject& subject,
                           lv_observer_cb_t callback,
                           void* user_data) {
    return observe_obj(obj, subject.native(), callback, user_data);
}

lv_observer_t* observe_target(lv_subject_t* subject,
                              lv_observer_cb_t callback,
                              void* target,
                              void* user_data) {
    if(!subject || !callback) {
        return nullptr;
    }

    return lv_subject_add_observer_with_target(subject, callback, target, user_data);
}

lv_observer_t* observe_target(Subject& subject,
                              lv_observer_cb_t callback,
                              void* target,
                              void* user_data) {
    return observe_target(subject.native(), callback, target, user_data);
}

lv_observer_t* bind_label_text(lv_obj_t* label, lv_subject_t* subject, const char* format) {
    if(!label || !subject) {
        return nullptr;
    }

    return lv_label_bind_text(label, subject, format);
}

lv_observer_t* bind_label_text(lv_obj_t* label, Subject& subject, const char* format) {
    return bind_label_text(label, subject.native(), format);
}

lv_observer_t* bind_slider_value(lv_obj_t* slider, lv_subject_t* subject) {
    if(!slider || !subject) {
        return nullptr;
    }

    return lv_slider_bind_value(slider, subject);
}

lv_observer_t* bind_slider_value(lv_obj_t* slider, Subject& subject) {
    return bind_slider_value(slider, subject.native());
}

lv_observer_t* bind_arc_value(lv_obj_t* arc, lv_subject_t* subject) {
    if(!arc || !subject) {
        return nullptr;
    }

    return lv_arc_bind_value(arc, subject);
}

lv_observer_t* bind_arc_value(lv_obj_t* arc, Subject& subject) {
    return bind_arc_value(arc, subject.native());
}

lv_observer_t* bind_bar_value(lv_obj_t* bar, lv_subject_t* subject) {
    if(!bar || !subject) {
        return nullptr;
    }

    return lv_bar_bind_value(bar, subject);
}

lv_observer_t* bind_bar_value(lv_obj_t* bar, Subject& subject) {
    return bind_bar_value(bar, subject.native());
}

lv_observer_t* bind_dropdown_value(lv_obj_t* dropdown, lv_subject_t* subject) {
    if(!dropdown || !subject) {
        return nullptr;
    }

    return lv_dropdown_bind_value(dropdown, subject);
}

lv_observer_t* bind_dropdown_value(lv_obj_t* dropdown, Subject& subject) {
    return bind_dropdown_value(dropdown, subject.native());
}

lv_observer_t* bind_roller_value(lv_obj_t* roller, lv_subject_t* subject) {
    if(!roller || !subject) {
        return nullptr;
    }

    return lv_roller_bind_value(roller, subject);
}

lv_observer_t* bind_roller_value(lv_obj_t* roller, Subject& subject) {
    return bind_roller_value(roller, subject.native());
}

lv_observer_t* bind_spinbox_value(lv_obj_t* spinbox, lv_subject_t* subject) {
    if(!spinbox || !subject) {
        return nullptr;
    }

    return lv_spinbox_bind_value(spinbox, subject);
}

lv_observer_t* bind_spinbox_value(lv_obj_t* spinbox, Subject& subject) {
    return bind_spinbox_value(spinbox, subject.native());
}

lv_observer_t* bind_image_src(lv_obj_t* image, lv_subject_t* subject) {
    if(!image || !subject) {
        return nullptr;
    }

    return lv_image_bind_src(image, subject);
}

lv_observer_t* bind_image_src(lv_obj_t* image, Subject& subject) {
    return bind_image_src(image, subject.native());
}

lv_observer_t* bind_checked(lv_obj_t* obj, lv_subject_t* subject) {
    if(!obj || !subject) {
        return nullptr;
    }

    return lv_obj_bind_checked(obj, subject);
}

lv_observer_t* bind_checked(lv_obj_t* obj, Subject& subject) {
    return bind_checked(obj, subject.native());
}

lv_observer_t* bind_flag(lv_obj_t* obj,
                         lv_subject_t* subject,
                         lv_obj_flag_t flag,
                         int32_t ref_value,
                         Compare compare) {
    if(!obj || !subject) {
        return nullptr;
    }

    switch(compare) {
        case Compare::Equal:
            return lv_obj_bind_flag_if_eq(obj, subject, flag, ref_value);
        case Compare::NotEqual:
            return lv_obj_bind_flag_if_not_eq(obj, subject, flag, ref_value);
        case Compare::Greater:
            return lv_obj_bind_flag_if_gt(obj, subject, flag, ref_value);
        case Compare::GreaterOrEqual:
            return lv_obj_bind_flag_if_ge(obj, subject, flag, ref_value);
        case Compare::Less:
            return lv_obj_bind_flag_if_lt(obj, subject, flag, ref_value);
        case Compare::LessOrEqual:
            return lv_obj_bind_flag_if_le(obj, subject, flag, ref_value);
    }

    return nullptr;
}

lv_observer_t* bind_flag(lv_obj_t* obj,
                         Subject& subject,
                         lv_obj_flag_t flag,
                         int32_t ref_value,
                         Compare compare) {
    return bind_flag(obj, subject.native(), flag, ref_value, compare);
}

lv_observer_t* bind_state(lv_obj_t* obj,
                          lv_subject_t* subject,
                          lv_state_t state,
                          int32_t ref_value,
                          Compare compare) {
    if(!obj || !subject) {
        return nullptr;
    }

    switch(compare) {
        case Compare::Equal:
            return lv_obj_bind_state_if_eq(obj, subject, state, ref_value);
        case Compare::NotEqual:
            return lv_obj_bind_state_if_not_eq(obj, subject, state, ref_value);
        case Compare::Greater:
            return lv_obj_bind_state_if_gt(obj, subject, state, ref_value);
        case Compare::GreaterOrEqual:
            return lv_obj_bind_state_if_ge(obj, subject, state, ref_value);
        case Compare::Less:
            return lv_obj_bind_state_if_lt(obj, subject, state, ref_value);
        case Compare::LessOrEqual:
            return lv_obj_bind_state_if_le(obj, subject, state, ref_value);
    }

    return nullptr;
}

lv_observer_t* bind_state(lv_obj_t* obj,
                          Subject& subject,
                          lv_state_t state,
                          int32_t ref_value,
                          Compare compare) {
    return bind_state(obj, subject.native(), state, ref_value, compare);
}

lv_observer_t* bind_style(lv_obj_t* obj,
                          const lv_style_t* style,
                          lv_style_selector_t selector,
                          lv_subject_t* subject,
                          int32_t ref_value) {
    if(!obj || !style || !subject) {
        return nullptr;
    }

    return lv_obj_bind_style(obj, style, selector, subject, ref_value);
}

lv_observer_t* bind_style(lv_obj_t* obj,
                          const lv_style_t* style,
                          lv_style_selector_t selector,
                          Subject& subject,
                          int32_t ref_value) {
    return bind_style(obj, style, selector, subject.native(), ref_value);
}

lv_observer_t* bind_style_prop(lv_obj_t* obj,
                               lv_style_prop_t prop,
                               lv_style_selector_t selector,
                               lv_subject_t* subject) {
    if(!obj || !subject) {
        return nullptr;
    }

    return lv_obj_bind_style_prop(obj, prop, selector, subject);
}

lv_observer_t* bind_style_prop(lv_obj_t* obj,
                               lv_style_prop_t prop,
                               lv_style_selector_t selector,
                               Subject& subject) {
    return bind_style_prop(obj, prop, selector, subject.native());
}

void on_event_set(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, int32_t value) {
    if(obj && subject) {
        lv_obj_add_subject_set_int_event(obj, subject, trigger, value);
    }
}

void on_event_set(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, int32_t value) {
    on_event_set(obj, subject.native(), trigger, value);
}

#if LV_USE_FLOAT
void on_event_set(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, float value) {
    if(obj && subject) {
        lv_obj_add_subject_set_float_event(obj, subject, trigger, value);
    }
}

void on_event_set(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, float value) {
    on_event_set(obj, subject.native(), trigger, value);
}
#endif

void on_event_set_string(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, const char* value) {
    if(obj && subject) {
        lv_obj_add_subject_set_string_event(obj, subject, trigger, value);
    }
}

void on_event_set_string(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, const char* value) {
    on_event_set_string(obj, subject.native(), trigger, value);
}

void on_event_toggle(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger) {
    if(obj && subject) {
        lv_obj_add_subject_toggle_event(obj, subject, trigger);
    }
}

void on_event_toggle(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger) {
    on_event_toggle(obj, subject.native(), trigger);
}

lv_subject_increment_dsc_t* on_event_increment(lv_obj_t* obj,
                                               lv_subject_t* subject,
                                               lv_event_code_t trigger,
                                               int32_t step) {
    if(!obj || !subject) {
        return nullptr;
    }

    return lv_obj_add_subject_increment_event(obj, subject, trigger, step);
}

lv_subject_increment_dsc_t* on_event_increment(lv_obj_t* obj,
                                               Subject& subject,
                                               lv_event_code_t trigger,
                                               int32_t step) {
    return on_event_increment(obj, subject.native(), trigger, step);
}

} // namespace reactive

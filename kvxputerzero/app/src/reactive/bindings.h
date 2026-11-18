/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"

#include "lvgl.h"

namespace reactive {

enum class Compare {
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
};

enum class ThemeRole {
    Screen,
    Surface,
    Bar,
    Button,
    Text,
};

void bind_theme(lv_obj_t* obj, lv_subject_t* dark_mode_subject, ThemeRole role);
void bind_theme(lv_obj_t* obj, Subject& dark_mode_subject, ThemeRole role);

void bind_bool_text(lv_obj_t* label,
                    lv_subject_t* bool_subject,
                    const char* false_text,
                    const char* true_text);
void bind_bool_text(lv_obj_t* label,
                    Subject& bool_subject,
                    const char* false_text,
                    const char* true_text);

lv_observer_t* observe(lv_subject_t* subject, lv_observer_cb_t callback, void* user_data = nullptr);
lv_observer_t* observe(Subject& subject, lv_observer_cb_t callback, void* user_data = nullptr);
lv_observer_t* observe_obj(lv_obj_t* obj,
                           lv_subject_t* subject,
                           lv_observer_cb_t callback,
                           void* user_data = nullptr);
lv_observer_t* observe_obj(lv_obj_t* obj,
                           Subject& subject,
                           lv_observer_cb_t callback,
                           void* user_data = nullptr);
lv_observer_t* observe_target(lv_subject_t* subject,
                              lv_observer_cb_t callback,
                              void* target,
                              void* user_data = nullptr);
lv_observer_t* observe_target(Subject& subject,
                              lv_observer_cb_t callback,
                              void* target,
                              void* user_data = nullptr);

lv_observer_t* bind_label_text(lv_obj_t* label, lv_subject_t* subject, const char* format = nullptr);
lv_observer_t* bind_label_text(lv_obj_t* label, Subject& subject, const char* format = nullptr);
lv_observer_t* bind_slider_value(lv_obj_t* slider, lv_subject_t* subject);
lv_observer_t* bind_slider_value(lv_obj_t* slider, Subject& subject);
lv_observer_t* bind_arc_value(lv_obj_t* arc, lv_subject_t* subject);
lv_observer_t* bind_arc_value(lv_obj_t* arc, Subject& subject);
lv_observer_t* bind_bar_value(lv_obj_t* bar, lv_subject_t* subject);
lv_observer_t* bind_bar_value(lv_obj_t* bar, Subject& subject);
lv_observer_t* bind_dropdown_value(lv_obj_t* dropdown, lv_subject_t* subject);
lv_observer_t* bind_dropdown_value(lv_obj_t* dropdown, Subject& subject);
lv_observer_t* bind_roller_value(lv_obj_t* roller, lv_subject_t* subject);
lv_observer_t* bind_roller_value(lv_obj_t* roller, Subject& subject);
lv_observer_t* bind_spinbox_value(lv_obj_t* spinbox, lv_subject_t* subject);
lv_observer_t* bind_spinbox_value(lv_obj_t* spinbox, Subject& subject);
lv_observer_t* bind_image_src(lv_obj_t* image, lv_subject_t* subject);
lv_observer_t* bind_image_src(lv_obj_t* image, Subject& subject);
lv_observer_t* bind_checked(lv_obj_t* obj, lv_subject_t* subject);
lv_observer_t* bind_checked(lv_obj_t* obj, Subject& subject);

lv_observer_t* bind_flag(lv_obj_t* obj,
                         lv_subject_t* subject,
                         lv_obj_flag_t flag,
                         int32_t ref_value,
                         Compare compare = Compare::Equal);
lv_observer_t* bind_flag(lv_obj_t* obj,
                         Subject& subject,
                         lv_obj_flag_t flag,
                         int32_t ref_value,
                         Compare compare = Compare::Equal);
lv_observer_t* bind_state(lv_obj_t* obj,
                          lv_subject_t* subject,
                          lv_state_t state,
                          int32_t ref_value,
                          Compare compare = Compare::Equal);
lv_observer_t* bind_state(lv_obj_t* obj,
                          Subject& subject,
                          lv_state_t state,
                          int32_t ref_value,
                          Compare compare = Compare::Equal);
lv_observer_t* bind_style(lv_obj_t* obj,
                          const lv_style_t* style,
                          lv_style_selector_t selector,
                          lv_subject_t* subject,
                          int32_t ref_value);
lv_observer_t* bind_style(lv_obj_t* obj,
                          const lv_style_t* style,
                          lv_style_selector_t selector,
                          Subject& subject,
                          int32_t ref_value);
lv_observer_t* bind_style_prop(lv_obj_t* obj,
                               lv_style_prop_t prop,
                               lv_style_selector_t selector,
                               lv_subject_t* subject);
lv_observer_t* bind_style_prop(lv_obj_t* obj,
                               lv_style_prop_t prop,
                               lv_style_selector_t selector,
                               Subject& subject);

void on_event_set(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, int32_t value);
void on_event_set(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, int32_t value);
#if LV_USE_FLOAT
void on_event_set(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, float value);
void on_event_set(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, float value);
#endif
void on_event_set_string(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger, const char* value);
void on_event_set_string(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger, const char* value);
void on_event_toggle(lv_obj_t* obj, lv_subject_t* subject, lv_event_code_t trigger);
void on_event_toggle(lv_obj_t* obj, Subject& subject, lv_event_code_t trigger);
lv_subject_increment_dsc_t* on_event_increment(lv_obj_t* obj,
                                               lv_subject_t* subject,
                                               lv_event_code_t trigger,
                                               int32_t step);
lv_subject_increment_dsc_t* on_event_increment(lv_obj_t* obj,
                                               Subject& subject,
                                               lv_event_code_t trigger,
                                               int32_t step);

} // namespace reactive

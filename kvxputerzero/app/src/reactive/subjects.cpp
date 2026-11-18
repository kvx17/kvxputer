/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "subjects.h"

namespace reactive {

Subject::~Subject() {
    deinit();
}

lv_subject_t* Subject::native() {
    return &subject_;
}

const lv_subject_t* Subject::native() const {
    return &subject_;
}

lv_subject_type_t Subject::type() const {
    return static_cast<lv_subject_type_t>(subject_.type);
}

void Subject::notify() {
    lv_subject_notify(native());
}

void Subject::deinit() {
    if(initialized_) {
        lv_subject_deinit(&subject_);
        initialized_ = false;
    }
}

IntSubject::IntSubject(int32_t value) {
    lv_subject_init_int(&subject_, value);
    initialized_ = true;
}

int32_t IntSubject::value() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&subject_));
}

int32_t IntSubject::previous() const {
    return lv_subject_get_previous_int(const_cast<lv_subject_t*>(&subject_));
}

void IntSubject::set(int32_t value) {
    lv_subject_set_int(native(), value);
}

void IntSubject::set_min(int32_t value) {
    lv_subject_set_min_value_int(native(), value);
}

void IntSubject::set_max(int32_t value) {
    lv_subject_set_max_value_int(native(), value);
}

BoolSubject::BoolSubject(bool value) {
    lv_subject_init_int(&subject_, value ? 1 : 0);
    initialized_ = true;
}

bool BoolSubject::value() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&subject_)) != 0;
}

bool BoolSubject::previous() const {
    return lv_subject_get_previous_int(const_cast<lv_subject_t*>(&subject_)) != 0;
}

void BoolSubject::set(bool value) {
    lv_subject_set_int(native(), value ? 1 : 0);
}

void BoolSubject::toggle() {
    set(!value());
}

#if LV_USE_FLOAT
FloatSubject::FloatSubject(float value) {
    lv_subject_init_float(&subject_, value);
    initialized_ = true;
}

float FloatSubject::value() const {
    return lv_subject_get_float(const_cast<lv_subject_t*>(&subject_));
}

float FloatSubject::previous() const {
    return lv_subject_get_previous_float(const_cast<lv_subject_t*>(&subject_));
}

void FloatSubject::set(float value) {
    lv_subject_set_float(native(), value);
}

void FloatSubject::set_min(float value) {
    lv_subject_set_min_value_float(native(), value);
}

void FloatSubject::set_max(float value) {
    lv_subject_set_max_value_float(native(), value);
}
#endif

ColorSubject::ColorSubject(lv_color_t value) {
    lv_subject_init_color(&subject_, value);
    initialized_ = true;
}

lv_color_t ColorSubject::value() const {
    return lv_subject_get_color(const_cast<lv_subject_t*>(&subject_));
}

lv_color_t ColorSubject::previous() const {
    return lv_subject_get_previous_color(const_cast<lv_subject_t*>(&subject_));
}

void ColorSubject::set(lv_color_t value) {
    lv_subject_set_color(native(), value);
}

} // namespace reactive

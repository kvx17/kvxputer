/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view::widgets {

class BaseWidgets {
public:
    explicit BaseWidgets(lv_obj_t* parent) : parent_(parent) {}
    virtual ~BaseWidgets() {
        if (core_obj_ && lv_obj_is_valid(core_obj_)) {
            lv_obj_delete(core_obj_);
        }
    }

    BaseWidgets(const BaseWidgets&) = delete;
    BaseWidgets& operator=(const BaseWidgets&) = delete;

    virtual void build() = 0;

    lv_obj_t* root() const { return core_obj_; }

protected:
    lv_obj_t* parent_{nullptr};
    lv_obj_t* core_obj_{nullptr};
};

} // namespace view::widgets

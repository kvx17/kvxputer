/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

#include "ui_const.h"

namespace view {

ThemePalette palette(bool dark_mode) {
    if (dark_mode) {
        return {
            lv_color_hex(color::dark::kBody),
            lv_color_hex(color::dark::kCard),
            lv_color_hex(color::dark::kAction),
            lv_color_hex(color::dark::kButton),
            lv_color_hex(color::dark::kBorder),
            lv_color_hex(color::dark::kTextPrimary),
            lv_color_hex(color::dark::kTextDisabled),
            lv_color_hex(color::dark::kPrimary),
            lv_color_hex(color::dark::kInfo),
            lv_color_hex(color::dark::kSuccess),
            lv_color_hex(color::dark::kWarning),
            lv_color_hex(color::dark::kError),
        };
    }

    return {
        lv_color_hex(color::light::kBody),
        lv_color_hex(color::light::kCard),
        lv_color_hex(color::light::kAction),
        lv_color_hex(color::light::kButton),
        lv_color_hex(color::light::kBorder),
        lv_color_hex(color::light::kTextPrimary),
        lv_color_hex(color::light::kTextDisabled),
        lv_color_hex(color::light::kPrimary),
        lv_color_hex(color::light::kInfo),
        lv_color_hex(color::light::kSuccess),
        lv_color_hex(color::light::kWarning),
        lv_color_hex(color::light::kError),
    };
}

void apply_lvgl_theme(lv_display_t* display, bool dark_mode, const lv_font_t* font) {
    if (!display) {
        return;
    }

    const auto colors = palette(dark_mode);
    lv_theme_t* theme = lv_theme_default_init(display, colors.primary, colors.info, dark_mode, font);
    lv_display_set_theme(display, theme);
}

} // namespace view

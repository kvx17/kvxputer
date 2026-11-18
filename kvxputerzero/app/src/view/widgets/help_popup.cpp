/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "help_popup.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "theme.h"

#include <array>
#include <cstddef>

namespace view::widgets {
namespace {

struct HelpRow {
    const char* key;
    const char* action;
};

constexpr std::array<HelpRow, 5> kGridRows = {{
    {"F X Z C", "Up down left right"},
    {"5 / 7 / 6", "Up, down, open"},
    {"4 / 8", "Previous / next page"},
    {"B V N = . , /", "Punctuation bare"},
    {"HOLD ESC", "Exit app"},
}};

constexpr std::array<HelpRow, 5> kMenuRows = {{
    {"F / X", "Up / down"},
    {"6", "Select"},
    {"4 / 8", "Back / home"},
    {"SYM + KEY", "Type that letter"},
    {"ESC", "Back"},
}};

constexpr std::array<HelpRow, 5> kTypeRows = {{
    {"B V N R", ". , / - bare"},
    {"SYM + KEY", "Type the letter"},
    {"ENTER", "Run"},
    {"ESC", "Back"},
    {"F X Z C", "Move cursor"},
}};

} // namespace

HelpPopup::HelpPopup(lv_obj_t* parent,
                     viewmodel::BaseViewModel& view_model,
                     app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

void HelpPopup::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(core_obj_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_50, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(core_obj_, backdrop_clicked_cb, LV_EVENT_CLICKED, this);

    panel_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, 292, 146);
    lv_obj_center(panel_);
    lv_obj_set_style_radius(panel_, 6, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_shadow_width(panel_, 14, 0);
    lv_obj_set_style_shadow_opa(panel_, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(panel_, 0, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);

    title_ = lv_label_create(panel_);
    lv_label_set_text(title_, "Keyboard help");
    auto* title_font = assets_.load_standard_font(15);
    lv_obj_set_style_text_font(title_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 12, 9);

    page_badge_ = lv_label_create(panel_);
    auto* badge_font = assets_.load_standard_font(11);
    lv_obj_set_style_text_font(page_badge_, badge_font ? badge_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(page_badge_, 3, 0);
    lv_obj_set_style_pad_hor(page_badge_, 6, 0);
    lv_obj_set_style_pad_ver(page_badge_, 2, 0);
    lv_obj_align(page_badge_, LV_ALIGN_TOP_RIGHT, -10, 7);

    auto* rows = lv_obj_create(panel_);
    lv_obj_remove_style_all(rows);
    lv_obj_set_size(rows, 268, 88);
    lv_obj_align(rows, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rows, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rows, 1, 0);
    lv_obj_clear_flag(rows, LV_OBJ_FLAG_SCROLLABLE);

    auto* key_font = assets_.load_standard_font(11);
    auto* action_font = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    for (auto& widgets : rows_) {
        widgets.row = lv_obj_create(rows);
        lv_obj_remove_style_all(widgets.row);
        lv_obj_set_size(widgets.row, LV_PCT(100), 16);
        lv_obj_clear_flag(widgets.row, LV_OBJ_FLAG_SCROLLABLE);

        widgets.key = lv_label_create(widgets.row);
        lv_obj_set_width(widgets.key, 96);
        lv_obj_set_style_text_font(widgets.key, key_font ? key_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(widgets.key, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(widgets.key, LV_ALIGN_LEFT_MID, 0, 0);

        widgets.action = lv_label_create(widgets.row);
        lv_obj_set_width(widgets.action, 162);
        lv_obj_set_style_text_font(widgets.action,
                                   action_font ? action_font : &lv_font_montserrat_12,
                                   0);
        lv_obj_align(widgets.action, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    footer_ = lv_label_create(panel_);
    lv_label_set_text(footer_, "HELP / ESC  Close   PRSC  Screenshot");
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_CLIP);
    auto* footer_font = assets_.load_standard_font(10);
    lv_obj_set_style_text_font(footer_, footer_font ? footer_font : &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(footer_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(footer_, LV_SIZE_CONTENT, 12);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 12, -7);

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(),
                                theme_observer_cb,
                                core_obj_,
                                this);
    apply_theme();
    hide();
}

void HelpPopup::show(model::AppPage page) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }
    update_rows(page);
    apply_theme();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool HelpPopup::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::update_rows(model::AppPage page) {
    const bool home = page == model::AppPage::ChannelGrid;
    const bool typing = page == model::AppPage::CodecWork || page == model::AppPage::EngCalcWork ||
                        page == model::AppPage::ComboWork || page == model::AppPage::TotpAdd ||
                        page == model::AppPage::ColorHex || page == model::AppPage::PdaEdit ||
                        page == model::AppPage::ToolPrompt || page == model::AppPage::QrCustom ||
                        page == model::AppPage::BarcodeWork;
    lv_label_set_text(page_badge_, home ? "HOME" : (typing ? "TYPE" : "MENU"));
    const auto row_count = home ? kGridRows.size() : (typing ? kTypeRows.size() : kMenuRows.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool row_visible = i < row_count;
        if (!row_visible) {
            lv_obj_add_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
        const auto& row = home ? kGridRows[i] : (typing ? kTypeRows[i] : kMenuRows[i]);
        lv_label_set_text(rows_[i].key, row.key);
        lv_label_set_text(rows_[i].action, row.action);
    }
}

void HelpPopup::apply_theme() {
    if (!panel_) {
        return;
    }
    const auto colors = view::palette(view_model_.is_dark_mode());
    lv_obj_set_style_bg_color(panel_, colors.surface, 0);
    lv_obj_set_style_border_color(panel_, colors.border, 0);
    lv_obj_set_style_shadow_color(panel_, lv_color_black(), 0);
    lv_obj_set_style_text_color(title_, colors.text, 0);
    lv_obj_set_style_bg_color(page_badge_, colors.primary, 0);
    lv_obj_set_style_bg_opa(page_badge_, LV_OPA_20, 0);
    lv_obj_set_style_text_color(page_badge_, colors.primary, 0);
    lv_obj_set_style_text_color(footer_, colors.text_disabled, 0);
    for (auto& row : rows_) {
        lv_obj_set_style_text_color(row.key, colors.primary, 0);
        lv_obj_set_style_text_color(row.action, colors.text, 0);
    }
}

void HelpPopup::backdrop_clicked_cb(lv_event_t* event) {
    auto* popup = static_cast<HelpPopup*>(lv_event_get_user_data(event));
    if (popup && lv_event_get_target(event) == popup->core_obj_) {
        popup->hide();
    }
}

void HelpPopup::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* popup = static_cast<HelpPopup*>(lv_observer_get_user_data(observer));
    if (popup) {
        popup->apply_theme();
    }
}

} // namespace view::widgets

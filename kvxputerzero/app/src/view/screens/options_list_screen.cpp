#include "options_list_screen.h"

#include "asset_manager.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

OptionsListScreen::OptionsListScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     std::string title,
                                     std::vector<MenuEntry> entries)
    : KvxScreen(view_model, assets), title_(std::move(title)), entries_(std::move(entries)) {
    init();
}

void OptionsListScreen::build() {
    make_header(content(), title_.c_str());

    list_ = lv_obj_create(content());
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 2, 0);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);

    auto* font = assets().load_standard_font(14);
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, view::kScreenWidth - 8, 20);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, entries_[i].label.c_str());
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
        rows_.push_back(row);
    }
    refresh();
}

void OptionsListScreen::refresh() {
    if (entries_.empty()) return;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = static_cast<int>(entries_.size()) - 1;

    for (size_t i = 0; i < rows_.size(); ++i) {
        const bool on = static_cast<int>(i) == selected_;
        lv_obj_set_style_bg_color(rows_[i], on ? pri_color() : lv_color_hex(0x1A1A22), 0);
        auto* label = lv_obj_get_child(rows_[i], 0);
        if (label) {
            lv_obj_set_style_text_color(label, on ? lv_color_hex(0xFFFFFF) : sec_color(), 0);
        }
    }
    if (!rows_.empty()) {
        lv_obj_scroll_to_view(rows_[static_cast<size_t>(selected_)], LV_ANIM_OFF);
    }
}

void OptionsListScreen::move_selection(int delta) {
    if (entries_.empty()) return;
    const int count = static_cast<int>(entries_.size());
    selected_ = (selected_ + delta + count) % count;
    refresh();
}

void OptionsListScreen::activate() {
    if (entries_.empty()) return;
    if (entries_[static_cast<size_t>(selected_)].action) {
        entries_[static_cast<size_t>(selected_)].action();
    }
}

bool OptionsListScreen::handle_key(uint32_t key) {
    if (entries_.empty()) return KvxScreen::handle_key(key);
    if (key == LV_KEY_DOWN) {
        move_selection(1);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == ' ') {
        activate();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> OptionsListScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {view::ICON_CHECK, [this]() { activate(); }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

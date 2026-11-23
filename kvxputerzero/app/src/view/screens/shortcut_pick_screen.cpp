#include "shortcut_pick_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "menu_icons.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>

namespace screen {
namespace {

constexpr int kRowH = 18;
constexpr int kRowGap = 2;
constexpr int kInputY = 2;

} // namespace

ShortcutPickScreen::ShortcutPickScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

ShortcutPickScreen::~ShortcutPickScreen() {
    platform::clear_screen_key_release_listener(on_release, this);
}

void ShortcutPickScreen::on_release(uint32_t, const char*, void* user_data) {
    auto* self = static_cast<ShortcutPickScreen*>(user_data);
    if (self) self->ready_ = true;
}

void ShortcutPickScreen::build() {
    platform::set_screen_key_release_listener(on_release, this);

    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;

    input_ = view::make_line_input(content(), tok, font, view::kScreenWidth - 8);
    lv_textarea_set_placeholder_text(input_, "Type to filter");
    lv_textarea_set_text(input_, "");
    lv_obj_set_height(input_, 18);
    lv_obj_align(input_, LV_ALIGN_TOP_MID, 0, kInputY);
    lv_obj_update_layout(input_);

    const int list_y = lv_obj_get_y(input_) + lv_obj_get_height(input_) + 2;
    const int avail = view::kContentHeight - list_y;
    const int stride = kRowH + kRowGap;
    visible_ = stride > 0 ? avail / stride : 1;
    if (visible_ > 5) visible_ = 5;
    if (visible_ < 1) visible_ = 1;

    list_ = lv_obj_create(content());
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, view::kScreenWidth, visible_ * stride);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, list_y);
    lv_obj_set_style_pad_row(list_, kRowGap, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    rebuild();
}

void ShortcutPickScreen::rebuild() {
    if (!list_) return;
    while (lv_obj_get_child_count(list_) > 0) {
        lv_obj_delete(lv_obj_get_child(list_, 0));
    }
    shown_.clear();
    if (query_.empty()) {
        for (const auto& target : model::launch_targets()) shown_.push_back(&target);
    } else {
        shown_ = model::search_launch_targets(query_);
    }
    if (selected_ >= static_cast<int>(shown_.size())) selected_ = 0;
    if (selected_ < 0) selected_ = 0;

    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* small = assets().load_standard_font(10);
    if (!small) small = &lv_font_montserrat_12;
    const lv_font_t* icon_font = assets().load_font("Phosphor-Fill.ttf", 12);

    if (shown_.empty()) {
        auto* note = lv_label_create(list_);
        lv_label_set_text(note, "No matches");
        lv_obj_set_style_text_font(note, font, 0);
        lv_obj_set_style_text_color(note, tok.ink_dim, 0);
        lv_obj_set_style_pad_left(note, 8, 0);
        return;
    }

    int start = 0;
    if (static_cast<int>(shown_.size()) > visible_) {
        start = selected_ - visible_ + 1;
        if (start < 0) start = 0;
        const int max_start = static_cast<int>(shown_.size()) - visible_;
        if (start > max_start) start = max_start;
    }
    const int end = std::min(start + visible_, static_cast<int>(shown_.size()));
    for (int i = start; i < end; ++i) {
        const auto* t = shown_[static_cast<size_t>(i)];
        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, view::kScreenWidth - 12, kRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        const bool on = i == selected_;
        lv_obj_set_style_bg_opa(row, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, on ? tok.surface_sel : tok.bg, 0);

        view::add_target_icon(row, icon_font, tok, *t);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, t->label.c_str());
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, on ? tok.ink : tok.ink_dim, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(label, 1);

        if (!t->group.empty()) {
            auto* group = lv_label_create(row);
            lv_label_set_text(group, t->group.c_str());
            lv_obj_set_style_text_font(group, small, 0);
            lv_obj_set_style_text_color(group, tok.ink_dim, 0);
        }
    }
}

void ShortcutPickScreen::move_selection(int delta) {
    if (shown_.empty()) return;
    const int count = static_cast<int>(shown_.size());
    selected_ = (selected_ + delta + count) % count;
    rebuild();
}

void ShortcutPickScreen::activate() {
    if (!ready_ || shown_.empty()) return;
    if (selected_ < 0 || selected_ >= static_cast<int>(shown_.size())) return;
    view_model().choose_shortcut_target(shown_[static_cast<size_t>(selected_)]->id);
}

bool ShortcutPickScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (key == LV_KEY_DOWN) {
        move_selection(1);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == '6') {
        // The key that opened the picker is still down. Swallow it.
        if (!ready_) return true;
        activate();
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == 8) {
        if (!query_.empty()) query_.pop_back();
        if (input_) lv_textarea_set_text(input_, query_.c_str());
        selected_ = 0;
        rebuild();
        return true;
    }
    if (key == '4' || key == '5' || key == '7' || key == '8') return false;
    if (key >= 32 && key <= 126) {
        query_.push_back(static_cast<char>(key));
        if (input_) lv_textarea_set_text(input_, query_.c_str());
        selected_ = 0;
        rebuild();
        return true;
    }
    return KvxScreen::handle_key(key);
}

const char* ShortcutPickScreen::typing_hint() const {
    return "Type · 6 choose · Esc back";
}

std::array<view::widgets::ActionSlot, 5> ShortcutPickScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {view::ICON_CHECK, [this]() { activate(); }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

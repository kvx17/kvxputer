#include "options_list_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "menu_icons.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

OptionsListScreen::OptionsListScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     std::string title,
                                     std::vector<MenuEntry> entries)
    : KvxScreen(view_model, assets), title_(std::move(title)), entries_(std::move(entries)) {
    cursor_key_ = cursor_key();
    // Confirmations ("Delete?", "Reboot?") always open on the first row.
    if (cursor_key_.empty() || title_.empty() || title_.back() == '?') selected_ = 0;
    else selected_ = view_model.menu_cursor(cursor_key_);
    init();
}

OptionsListScreen::~OptionsListScreen() {
    remember_selection();
}

std::string OptionsListScreen::cursor_key() {
    // Captured when the menu is built. The page may already have changed by the
    // time this screen is destroyed, so the key is stored and reused.
    const auto page = view_model().current_page();
    if (page == model::AppPage::ToolRun || page == model::AppPage::ToolPrompt) {
        return "tool:" + std::to_string(static_cast<int>(view_model().active_tool_id())) + ":" + title_;
    }
    return "page:" + std::to_string(static_cast<int>(page)) + ":" + title_;
}

void OptionsListScreen::remember_selection() {
    if (cursor_key_.empty() || title_.empty() || title_.back() == '?') return;
    view_model().set_menu_cursor(cursor_key_, selected_);
}

void OptionsListScreen::build() {
    make_header(content(), title_.c_str());

    const auto tok = tokens();
    list_ = view::make_list(content(), 18);

    auto* font = assets().load_standard_font(14);
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 14);
    const char* fallback = view::channel_icon(view_model().current_channel().id);
    for (size_t i = 0; i < entries_.size(); ++i) {
        const char* icon = entries_[i].icon ? entries_[i].icon : fallback;
        auto* row = view::make_list_row(list_,
                                        entries_[i].label.c_str(),
                                        icon,
                                        font ? font : &lv_font_montserrat_14,
                                        icon_font,
                                        tok,
                                        entries_[i].swatch);
        rows_.push_back(row);
    }
    refresh();
}

void OptionsListScreen::refresh() {
    if (entries_.empty()) return;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = static_cast<int>(entries_.size()) - 1;

    const auto tok = tokens();
    for (size_t i = 0; i < rows_.size(); ++i) {
        view::apply_row_state(rows_[i], static_cast<int>(i) == selected_, tok);
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

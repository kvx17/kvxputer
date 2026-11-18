#include "result_list_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

ResultListScreen::ResultListScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets,
                                   std::string title, std::vector<std::string> lines,
                                   std::function<void()> on_save)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      lines_(std::move(lines)),
      on_save_(std::move(on_save)) {
    init();
}

void ResultListScreen::build() {
    make_header(content(), title_.c_str());
    list_ = view::make_list(content(), 18);

    const auto tok = tokens();
    auto* font = assets().load_standard_font(12);
    if (lines_.empty()) lines_.push_back("(empty)");
    for (const auto& line : lines_) {
        auto* row = view::make_list_row(list_, line.c_str(), nullptr,
                                        font ? font : &lv_font_montserrat_12, nullptr, tok);
        // Compact result rows.
        lv_obj_set_height(row, 16);
        rows_.push_back(row);
    }
    refresh();
}

void ResultListScreen::refresh() {
    if (rows_.empty()) return;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = static_cast<int>(rows_.size()) - 1;
    const auto tok = tokens();
    for (size_t i = 0; i < rows_.size(); ++i) {
        view::apply_row_state(rows_[i], static_cast<int>(i) == selected_, tok);
    }
    lv_obj_scroll_to_view(rows_[static_cast<size_t>(selected_)], LV_ANIM_OFF);
}

void ResultListScreen::move_selection(int delta) {
    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());
    selected_ = (selected_ + delta + count) % count;
    refresh();
}

bool ResultListScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_DOWN) {
        move_selection(1);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1);
        return true;
    }
    if ((key == 's' || key == 'S') && on_save_) {
        on_save_();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> ResultListScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {on_save_ ? view::ICON_CHECK : "", on_save_ ? std::function<void()>([this]() { on_save_(); })
                                                    : std::function<void()>()},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

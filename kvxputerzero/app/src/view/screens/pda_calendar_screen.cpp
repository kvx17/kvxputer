#include "pda_calendar_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>

namespace screen {
namespace {

const char* kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                             "July",    "August",   "September", "October", "November", "December"};

} // namespace

PdaCalendarScreen::PdaCalendarScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    year_ = view_model.model().cal_year();
    month_ = view_model.model().cal_month();
    day_ = view_model.model().cal_day();
    init();
}

void PdaCalendarScreen::build() {
    header_label_ = nullptr;
    make_header(content(), "Calendar");
    // Replace header text dynamically via a second label overlay — keep simple: store month in footer.

    grid_ = lv_obj_create(content());
    lv_obj_remove_style_all(grid_);
    lv_obj_set_size(grid_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(grid_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

    const auto tok = tokens();
    header_label_ = lv_label_create(grid_);
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(header_label_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header_label_, tok.ink, 0);
    lv_obj_align(header_label_, LV_ALIGN_TOP_MID, 0, 0);

    const char* wd[] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int i = 0; i < 7; ++i) {
        auto* l = lv_label_create(grid_);
        lv_label_set_text(l, wd[i]);
        lv_obj_set_style_text_font(l, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, tok.ink_dim, 0);
        // positions set in redraw
    }

    for (int d = 1; d <= 31; ++d) {
        auto* cell = lv_obj_create(grid_);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_radius(cell, 3, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        auto* lab = lv_label_create(cell);
        lv_obj_set_style_text_font(lab, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_center(lab);
        cells_[d] = cell;
    }
    redraw();
}

void PdaCalendarScreen::redraw() {
    view_model().model().set_cal_date(year_, month_, day_);

    std::vector<tools::pda::Event> events;
    std::string error;
    tools::pda::load_events(events, error);
    tools::pda::event_counts_for_month(events, year_, month_, event_count_);

    if (header_label_) {
        lv_label_set_text_fmt(header_label_, "%s %d", kMonthNames[month_ - 1], year_);
    }

    constexpr int margin = 4;
    const int grid_w = view::kScreenWidth - 2 * margin;
    const int cw = grid_w / 7;
    const int wd_y = 14;
    const int grid_top = wd_y + 12;
    const int rows = 6;
    const int footer_room = 14;
    const int ch = (view::kContentHeight - 18 - grid_top - footer_room) / rows;

    // weekday labels are children 1..7
    for (int i = 0; i < 7; ++i) {
        auto* l = lv_obj_get_child(grid_, 1 + i);
        if (l) lv_obj_set_pos(l, margin + i * cw + cw / 2 - 4, wd_y);
    }

    const int first_dow = tools::pda::day_of_week(year_, month_, 1);
    const int dim = tools::pda::days_in_month(year_, month_);
    const auto now = tools::pda::local_now();
    const auto tok = tokens();

    if (header_label_) {
        lv_obj_set_style_text_color(header_label_, tok.ink, 0);
    }

    for (int d = 1; d <= 31; ++d) {
        auto* cell = cells_[d];
        if (!cell) continue;
        if (d > dim) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
        const int cell_index = first_dow + d - 1;
        const int r = cell_index / 7;
        const int c = cell_index % 7;
        lv_obj_set_size(cell, cw - 2, ch - 2);
        lv_obj_set_pos(cell, margin + c * cw + 1, grid_top + r * ch + 1);

        const bool is_today =
            (now.tm_year + 1900 == year_ && now.tm_mon + 1 == month_ && now.tm_mday == d);
        const bool is_cursor = (d == day_);
        if (is_cursor) {
            lv_obj_set_style_bg_color(cell, tok.accent, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, tok.accent, 0);
        } else if (is_today) {
            lv_obj_set_style_bg_color(cell, tok.bg, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, tok.accent, 0);
        } else {
            lv_obj_set_style_bg_color(cell, tok.bg, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_border_color(cell, tok.bg, 0);
        }
        auto* lab = lv_obj_get_child(cell, 0);
        if (lab) {
            lv_label_set_text_fmt(lab, "%d%s", d, event_count_[d] > 0 ? "." : "");
            lv_obj_set_style_text_color(lab, is_cursor ? tok.bg : tok.ink, 0);
        }
    }
}

void PdaCalendarScreen::open_day() {
    view_model().model().set_cal_date(year_, month_, day_);
    view_model().open_pda_day();
}

bool PdaCalendarScreen::handle_key(uint32_t key) {
    // Zero arrows: F/Z/X/C. Month: L/M. Year: -/+ (bare R/T) or Sym+R/T.
    if (key == LV_KEY_LEFT || key == 'a' || key == 'A') {
        tools::pda::add_days(year_, month_, day_, -1);
        redraw();
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'd' || key == 'D') {
        tools::pda::add_days(year_, month_, day_, 1);
        redraw();
        return true;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W') {
        tools::pda::add_days(year_, month_, day_, -7);
        redraw();
        return true;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S') {
        tools::pda::add_days(year_, month_, day_, 7);
        redraw();
        return true;
    }
    if (key == LV_KEY_PREV || key == '[' || key == '-') {
        tools::pda::add_months(year_, month_, day_, -1);
        redraw();
        return true;
    }
    if (key == LV_KEY_NEXT || key == ']' || key == '=') {
        tools::pda::add_months(year_, month_, day_, 1);
        redraw();
        return true;
    }
    if (key == '-' || key == 'r' || key == 'R') {
        tools::pda::add_months(year_, month_, day_, -12);
        redraw();
        return true;
    }
    if (key == '+' || key == 't' || key == 'T') {
        tools::pda::add_months(year_, month_, day_, 12);
        redraw();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ' || key == '\n') {
        open_day();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaCalendarScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() {
             tools::pda::add_days(year_, month_, day_, -1);
             redraw();
         }},
        {view::ICON_CHECK, [this]() { open_day(); }},
        {view::ICON_ARROW_RIGHT, [this]() {
             tools::pda::add_days(year_, month_, day_, 1);
             redraw();
         }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

// --- Day screen ---

PdaDayScreen::PdaDayScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    date_ = tools::pda::format_date(view_model.model().cal_year(), view_model.model().cal_month(),
                                    view_model.model().cal_day());
    init();
}

void PdaDayScreen::build() {
    char title[32];
    std::snprintf(title, sizeof(title), "%s", date_.c_str());
    make_header(content(), title);
    list_ = view::make_list(content(), 18);
    reload();
}

void PdaDayScreen::reload() {
    rows_.clear();
    lv_obj_clean(list_);
    all_events_.clear();
    day_events_.clear();
    std::string error;
    tools::pda::load_events(all_events_, error);
    tools::pda::events_for_day(all_events_, date_, day_events_);

    const auto tok = tokens();
    auto* font = assets().load_standard_font(14);
    auto add_row = [&](const char* text) {
        auto* row = view::make_list_row(list_, text, nullptr, font ? font : &lv_font_montserrat_14,
                                        nullptr, tok);
        rows_.push_back(row);
    };

    add_row("Add Event");
    for (const auto& e : day_events_) {
        std::string line = e.time + "  " + e.title;
        if (line.size() > 36) line = line.substr(0, 36) + "...";
        add_row(line.c_str());
    }
    selected_ = 0;
    refresh();
}

void PdaDayScreen::refresh() {
    if (rows_.empty()) return;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = static_cast<int>(rows_.size()) - 1;
    const auto tok = tokens();
    for (size_t i = 0; i < rows_.size(); ++i) {
        view::apply_row_state(rows_[i], static_cast<int>(i) == selected_, tok);
    }
}

void PdaDayScreen::add_event() {
    auto& model = view_model().model();
    model.set_pda_path("__calendar__");
    const auto now = tools::pda::local_now();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d%02d", now.tm_hour, now.tm_min);
    model.set_work_input(buf);
    model.set_work_output("");
    view_model().open_pda_edit(model::PdaEditMode::PromptTime, false);
}

void PdaDayScreen::delete_selected() {
    if (selected_ <= 0) return;
    const size_t ev_i = static_cast<size_t>(selected_ - 1);
    if (ev_i >= day_events_.size()) return;
    const size_t line_idx = day_events_[ev_i].line_index;
    if (line_idx < all_events_.size()) {
        // Rebuild from all_events by line_index matching — events store original indices
        std::vector<tools::pda::Event> kept;
        for (size_t i = 0; i < all_events_.size(); ++i) {
            if (i != line_idx) kept.push_back(all_events_[i]);
        }
        std::string error;
        tools::pda::save_events(kept, error);
    }
    reload();
}

bool PdaDayScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_DOWN) {
        if (!rows_.empty()) {
            selected_ = (selected_ + 1) % static_cast<int>(rows_.size());
            refresh();
        }
        return true;
    }
    if (key == LV_KEY_UP) {
        if (!rows_.empty()) {
            selected_ = (selected_ - 1 + static_cast<int>(rows_.size())) % static_cast<int>(rows_.size());
            refresh();
        }
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ' || key == '\n') {
        if (selected_ == 0) add_event();
        else delete_selected(); // Enter on event opens delete confirm via immediate delete (Cardputer submenu Delete)
        return true;
    }
    // Bare X is Down on Zero; Del or Fn+X ('x') deletes. Bare N is End — use Fn+N or row 0.
    if (key == LV_KEY_DEL || key == 'x' || key == 'X') {
        delete_selected();
        return true;
    }
    if (key == 'n' || key == 'N') {
        add_event();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaDayScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() {
             if (!rows_.empty()) {
                 selected_ = (selected_ - 1 + static_cast<int>(rows_.size())) % static_cast<int>(rows_.size());
                 refresh();
             }
         }},
        {view::ICON_CHECK, [this]() {
             if (selected_ == 0) add_event();
             else delete_selected();
         }},
        {view::ICON_CARET_DOWN, [this]() {
             if (!rows_.empty()) {
                 selected_ = (selected_ + 1) % static_cast<int>(rows_.size());
                 refresh();
             }
         }},
        {view::ICON_PLUS, [this]() { add_event(); }},
    }};
}

} // namespace screen

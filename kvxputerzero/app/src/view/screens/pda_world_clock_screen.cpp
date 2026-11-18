#include "pda_world_clock_screen.h"

#include "asset_manager.h"
#include "device_status.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>
#include <cstring>

namespace screen {

PdaWorldClockScreen::PdaWorldClockScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    face_ = view_model.model().wc_face();
    city_ = view_model.model().wc_city();
    if (city_ < 0) city_ = 0;
    if (city_ >= tools::pda::city_count()) city_ = 0;
    init();
}

PdaWorldClockScreen::~PdaWorldClockScreen() {
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
    view_model().model().set_wc_face(face_);
    view_model().model().set_wc_city(city_);
    view_model().persist_pda_config();
}

void PdaWorldClockScreen::build() {
    make_header(content(), "WorldClock");
    body_ = lv_obj_create(content());
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);

    tick_ = lv_timer_create(on_tick, 1000, this);
    redraw(true);
}

void PdaWorldClockScreen::on_tick(lv_timer_t* timer) {
    auto* self = static_cast<PdaWorldClockScreen*>(lv_timer_get_user_data(timer));
    if (self) self->redraw(false);
}

void PdaWorldClockScreen::cycle_face(int delta) {
    face_ = (face_ + delta + 3) % 3;
    last_key_[0] = '\0';
    redraw(true);
}

void PdaWorldClockScreen::move_city(int delta) {
    const int n = tools::pda::city_count();
    city_ = (city_ + delta + n) % n;
    last_key_[0] = '\0';
    redraw(true);
}

void PdaWorldClockScreen::confirm_set_tz() {
    if (!confirm_tz_) {
        confirm_tz_ = true;
        last_key_[0] = '\0';
        redraw(true);
        return;
    }
    const auto& c = tools::pda::cities()[city_];
    view_model().model().set_tmz(c.utc_offset);
    view_model().persist_pda_config();
    confirm_tz_ = false;
    last_key_[0] = '\0';
    redraw(true);
}

void PdaWorldClockScreen::draw_cities(bool /*full*/) {
    lv_obj_clean(body_);
    auto* font = assets().load_standard_font(12);
    auto* font14 = assets().load_standard_font(14);
    const bool clock24 = view_model().model().clock24hr();
    const auto now = tools::pda::local_now();
    const auto& selected = tools::pda::cities()[city_];

    auto* local = lv_label_create(body_);
    lv_obj_set_style_text_font(local, font14 ? font14 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(local, pri_color(), 0);
    lv_label_set_text_fmt(local, "Local  %s%s", tools::pda::format_hm(now, clock24).c_str(),
                          tools::pda::clock_looks_set() ? "" : "  (unset)");
    lv_obj_align(local, LV_ALIGN_TOP_LEFT, 4, 2);

    auto* tz = lv_label_create(body_);
    lv_obj_set_style_text_font(tz, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tz, sec_color(), 0);
    lv_label_set_text_fmt(tz, "TZ UTC%+.1f%s", view_model().model().tmz(),
                          view_model().model().dst() ? " DST" : "");
    lv_obj_align(tz, LV_ALIGN_TOP_LEFT, 4, 18);

    if (confirm_tz_) {
        auto* ask = lv_label_create(body_);
        lv_obj_set_style_text_font(ask, font14 ? font14 : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ask, sec_color(), 0);
        lv_label_set_text_fmt(ask, "Set TZ from %s? Enter=Yes Esc=No", selected.name);
        lv_obj_align(ask, LV_ALIGN_CENTER, 0, 10);
        return;
    }

    constexpr int max_rows = 5;
    int start = city_;
    if (start > tools::pda::city_count() - max_rows) start = tools::pda::city_count() - max_rows;
    if (start < 0) start = 0;
    if (city_ < start) start = city_;
    if (city_ >= start + max_rows) start = city_ - max_rows + 1;

    for (int i = 0; i < max_rows && start + i < tools::pda::city_count(); ++i) {
        const int idx = start + i;
        const auto& c = tools::pda::cities()[idx];
        const auto t = tools::pda::city_tm(c.utc_offset);
        char name[8];
        std::snprintf(name, sizeof(name), "%.6s", c.name);
        std::string line = std::string(name) + " " + tools::pda::format_hm(t, clock24) + " " +
                           tools::pda::format_offset(c.utc_offset);

        auto* row = lv_obj_create(body_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, view::kScreenWidth - 8, 16);
        lv_obj_set_pos(row, 4, 36 + i * 16);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        const bool hi = (idx == city_);
        lv_obj_set_style_bg_color(row, hi ? sec_color() : bg_color(), 0);
        auto* lab = lv_label_create(row);
        lv_label_set_text(lab, line.c_str());
        lv_obj_set_style_text_font(lab, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lab, hi ? bg_color() : pri_color(), 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 2, 0);
    }

    auto* foot = lv_label_create(body_);
    lv_obj_set_style_text_font(foot, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(foot, sec_color(), 0);
    lv_label_set_text(foot, "OK set TZ  [] face  H 12/24");
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void PdaWorldClockScreen::draw_charge() {
    lv_obj_clean(body_);
    auto* font = assets().load_standard_font(12);
    auto* font28 = assets().load_standard_font(28);
    const bool clock24 = view_model().model().clock24hr();
    const auto& c = tools::pda::cities()[city_];
    const auto t = tools::pda::city_tm(c.utc_offset);

    char date_buf[24];
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

    auto status = platform::read_device_status();
    int bat = status.battery_percent;
    if (!status.battery_present || bat < 0) bat = 50;
    if (bat < 1) bat = 1;
    if (bat > 100) bat = 100;

    auto* date = lv_label_create(body_);
    lv_obj_set_style_text_font(date, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(date, pri_color(), 0);
    lv_label_set_text(date, date_buf);
    lv_obj_align(date, LV_ALIGN_TOP_MID, 0, 2);

    auto* time_l = lv_label_create(body_);
    lv_obj_set_style_text_font(time_l, font28 ? font28 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_l, pri_color(), 0);
    lv_label_set_text(time_l, tools::pda::format_hms(t, clock24).c_str());
    lv_obj_align(time_l, LV_ALIGN_TOP_LEFT, 8, 18);

    auto* pct = lv_label_create(body_);
    lv_obj_set_style_text_font(pct, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct, pri_color(), 0);
    lv_label_set_text_fmt(pct, "%d%%", bat);
    lv_obj_align(pct, LV_ALIGN_TOP_RIGHT, -6, 22);

    // Mini month calendar
    const int year = t.tm_year + 1900;
    const int month = t.tm_mon + 1;
    const int first = tools::pda::day_of_week(year, month, 1);
    const int dim = tools::pda::days_in_month(year, month);
    constexpr int cal_y = 52;
    constexpr int cw = 40;
    constexpr int ch = 10;
    auto* f10 = assets().load_standard_font(10);
    for (int d = 1; d <= dim; ++d) {
        const int idx = first + d - 1;
        const int r = idx / 7;
        const int col = idx % 7;
        auto* lab = lv_label_create(body_);
        lv_label_set_text_fmt(lab, "%d", d);
        lv_obj_set_style_text_font(lab, f10 ? f10 : &lv_font_montserrat_14, 0);
        const bool today = (d == t.tm_mday);
        lv_obj_set_style_text_color(lab, today ? sec_color() : pri_color(), 0);
        lv_obj_set_pos(lab, 8 + col * (cw / 2), cal_y + r * ch);
    }

    // Charge bar
    auto* bar_bg = lv_obj_create(body_);
    lv_obj_remove_style_all(bar_bg);
    lv_obj_set_size(bar_bg, view::kScreenWidth - 16, 10);
    lv_obj_align(bar_bg, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_radius(bar_bg, 3, 0);
    lv_obj_set_style_border_width(bar_bg, 1, 0);
    lv_obj_set_style_border_color(bar_bg, pri_color(), 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_TRANSP, 0);

    auto* bar_fill = lv_obj_create(bar_bg);
    lv_obj_remove_style_all(bar_fill);
    const int fill_w = (view::kScreenWidth - 20) * bat / 100;
    lv_obj_set_size(bar_fill, fill_w > 0 ? fill_w : 1, 6);
    lv_obj_set_pos(bar_fill, 2, 2);
    lv_obj_set_style_bg_color(bar_fill, pri_color(), 0);
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);

    auto* foot = lv_label_create(body_);
    lv_obj_set_style_text_font(foot, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(foot, sec_color(), 0);
    lv_label_set_text_fmt(foot, "%s  [] face", c.name);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void PdaWorldClockScreen::draw_digital() {
    lv_obj_clean(body_);
    auto* font28 = assets().load_standard_font(28);
    auto* font = assets().load_standard_font(12);
    const bool clock24 = view_model().model().clock24hr();
    const auto& c = tools::pda::cities()[city_];
    const auto t = tools::pda::city_tm(c.utc_offset);

    auto* border = lv_obj_create(body_);
    lv_obj_remove_style_all(border);
    lv_obj_set_size(border, view::kScreenWidth - 16, view::kContentHeight - 40);
    lv_obj_align(border, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_border_width(border, 1, 0);
    lv_obj_set_style_border_color(border, pri_color(), 0);
    lv_obj_set_style_bg_opa(border, LV_OPA_TRANSP, 0);

    auto* time_l = lv_label_create(body_);
    lv_obj_set_style_text_font(time_l, font28 ? font28 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_l, pri_color(), 0);
    lv_label_set_text(time_l, tools::pda::format_hms(t, clock24).c_str());
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -4);

    auto* foot = lv_label_create(body_);
    lv_obj_set_style_text_font(foot, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(foot, sec_color(), 0);
    lv_label_set_text_fmt(foot, "%s  [] face", c.name);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void PdaWorldClockScreen::redraw(bool full) {
    const auto now = tools::pda::local_now();
    char key[48];
    std::snprintf(key, sizeof(key), "%d-%d-%02d%02d%02d-%d", face_, city_, now.tm_hour, now.tm_min,
                  (face_ == 0) ? 0 : now.tm_sec, full ? 1 : 0);
    if (!full && std::strcmp(key, last_key_) == 0) return;
    std::strncpy(last_key_, key, sizeof(last_key_) - 1);

    // Update header status with city name via recreate is heavy; faces draw their own chrome.
    if (face_ == 0) draw_cities(full);
    else if (face_ == 1) draw_charge();
    else draw_digital();
}

bool PdaWorldClockScreen::handle_key(uint32_t key) {
    if (confirm_tz_) {
        if (key == LV_KEY_ENTER || key == ' ' || key == '\n' || key == 'y' || key == 'Y') {
            confirm_set_tz();
            return true;
        }
        if (key == LV_KEY_ESC || key == 'n' || key == 'N') {
            confirm_tz_ = false;
            last_key_[0] = '\0';
            redraw(true);
            return true;
        }
        return true;
    }
    if (key == '[') {
        cycle_face(-1);
        return true;
    }
    if (key == ']') {
        cycle_face(1);
        return true;
    }
    if (key == LV_KEY_UP || key == 'w' || key == 'W') {
        move_city(-1);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S') {
        move_city(1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        confirm_set_tz();
        return true;
    }
    if (key == 'h' || key == 'H') {
        view_model().model().set_clock24hr(!view_model().model().clock24hr());
        view_model().persist_pda_config();
        last_key_[0] = '\0';
        redraw(true);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaWorldClockScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move_city(-1); }},
        {view::ICON_CHECK, [this]() { confirm_set_tz(); }},
        {view::ICON_CARET_DOWN, [this]() { move_city(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

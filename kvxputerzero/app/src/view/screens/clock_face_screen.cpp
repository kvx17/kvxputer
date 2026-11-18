#include "clock_face_screen.h"

#include "asset_manager.h"
#include "backlight.h"
#include "device_status.h"
#include "kvx_style.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace screen {
namespace {

constexpr uint32_t kFaceColors[] = {
    0x9800C0, // purple
    0x00FC00, // green
    0xF8A000, // orange
    0x00FCF8, // cyan
    0xFFFFFF, // white
    0xF80000, // red
    0xF8FC00, // yellow
    0xF890F8, // pink
};
constexpr int kFaceColorCount = static_cast<int>(sizeof(kFaceColors) / sizeof(kFaceColors[0]));

} // namespace

ClockFaceScreen::ClockFaceScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets,
                                 bool charge_mode)
    : KvxScreen(view_model, assets), charge_mode_(charge_mode), charge_style_(charge_mode) {
    if (charge_mode_) {
        if (platform::read_brightness_percent(saved_brightness_)) {
            saved_brightness_valid_ = true;
            current_brightness_ = saved_brightness_;
        } else {
            current_brightness_ = 100;
        }
    }
    init();
}

ClockFaceScreen::~ClockFaceScreen() {
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
    restore_brightness();
}

void ClockFaceScreen::restore_brightness() {
    if (!charge_mode_ || !saved_brightness_valid_) return;
    platform::write_brightness_percent(saved_brightness_);
    saved_brightness_valid_ = false;
}

void ClockFaceScreen::build() {
    body_ = lv_obj_create(content());
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, view::kScreenWidth, view::kContentHeight);
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);

    tick_ = lv_timer_create(on_tick, 500, this);
    redraw(true);
}

void ClockFaceScreen::on_tick(lv_timer_t* timer) {
    auto* self = static_cast<ClockFaceScreen*>(lv_timer_get_user_data(timer));
    if (self) self->redraw(false);
}

lv_color_t ClockFaceScreen::face_color() const {
    if (charge_mode_) {
        return lv_color_hex(kFaceColors[color_index_ % kFaceColorCount]);
    }
    return tokens().main;
}

void ClockFaceScreen::format_date(const std::tm& t, char* out, size_t out_len) const {
    static const char* kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int wday = t.tm_wday;
    int mon = t.tm_mon;
    if (wday < 0 || wday > 6) wday = 0;
    if (mon < 0 || mon > 11) mon = 0;
    int mday = t.tm_mday;
    if (mday < 1) mday = 1;
    if (mday > 31) mday = 31;
    std::snprintf(out, out_len, "%s %d %s %d", kDow[wday], mday, kMon[mon], t.tm_year + 1900);
}

int ClockFaceScreen::resolve_battery_percent(bool& known) const {
    const auto status = platform::read_device_status();
    if (status.battery_percent >= 0) {
        known = true;
        return std::clamp(status.battery_percent, 0, 100);
    }
#if USE_DESKTOP
    // Desktop has no power-supply sysfs; show a fixed sample, never 0% for unknown.
    known = true;
    return 76;
#else
    known = false;
    return -1;
#endif
}

void ClockFaceScreen::redraw(bool full) {
    const auto now = tools::pda::local_now();
    bool known = false;
    const int bat = resolve_battery_percent(known);
    char key[72];
    std::snprintf(key, sizeof(key), "%d-%d-%02d%02d%02d-%d-%d-%d", charge_mode_ ? 1 : 0,
                  charge_style_ ? 1 : 0, now.tm_hour, now.tm_min, now.tm_sec, color_index_, bat,
                  full ? 1 : 0);
    if (!full && std::strcmp(key, last_key_) == 0) return;
    std::strncpy(last_key_, key, sizeof(last_key_) - 1);
    last_key_[sizeof(last_key_) - 1] = '\0';

    if (charge_mode_ || charge_style_) {
        draw_charge_style();
    } else {
        draw_digital();
    }
}

void ClockFaceScreen::draw_digital() {
    lv_obj_clean(body_);
    const auto tok = tokens();
    const auto color = face_color();
    auto* font28 = assets().load_standard_font(28);
    auto* font14 = assets().load_standard_font(14);
    auto* font12 = assets().load_standard_font(12);
    const bool clock24 = view_model().model().clock24hr();
    const auto now = tools::pda::local_now();

    auto* border = lv_obj_create(body_);
    lv_obj_remove_style_all(border);
    lv_obj_set_size(border, view::kScreenWidth - 16, view::kContentHeight - 8);
    lv_obj_align(border, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(border, 8, 0);
    lv_obj_set_style_border_width(border, 1, 0);
    lv_obj_set_style_border_color(border, color, 0);
    lv_obj_set_style_bg_opa(border, LV_OPA_TRANSP, 0);

    auto* time_l = lv_label_create(body_);
    lv_obj_set_style_text_font(time_l, font28 ? font28 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_l, color, 0);
    lv_label_set_text(time_l, tools::pda::format_hms(now, clock24).c_str());
    lv_obj_align(time_l, LV_ALIGN_CENTER, 0, -10);

    char date_buf[32];
    format_date(now, date_buf, sizeof(date_buf));
    auto* date_l = lv_label_create(body_);
    lv_obj_set_style_text_font(date_l, font14 ? font14 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(date_l, tok.ink_dim, 0);
    lv_label_set_text(date_l, date_buf);
    lv_obj_align(date_l, LV_ALIGN_CENTER, 0, 18);

    bool known = false;
    const int bat = resolve_battery_percent(known);
    auto* pct_l = lv_label_create(body_);
    lv_obj_set_style_text_font(pct_l, font12 ? font12 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct_l, color, 0);
    if (known) {
        lv_label_set_text_fmt(pct_l, "%d%%", bat);
    } else {
        lv_label_set_text(pct_l, "");
    }
    lv_obj_align(pct_l, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void ClockFaceScreen::draw_charge_style() {
    lv_obj_clean(body_);
    const auto tok = tokens();
    const auto color = face_color();
    auto* font28 = assets().load_standard_font(28);
    auto* font12 = assets().load_standard_font(12);
    auto* font10 = assets().load_standard_font(10);
    const bool clock24 = view_model().model().clock24hr();
    const auto now = tools::pda::local_now();

    bool known = false;
    int bat = resolve_battery_percent(known);
    if (known) {
        if (bat < 1) bat = 1;
        if (bat > 100) bat = 100;
    }

    char date_buf[32];
    format_date(now, date_buf, sizeof(date_buf));

    auto* date_l = lv_label_create(body_);
    lv_obj_set_style_text_font(date_l, font12 ? font12 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(date_l, color, 0);
    lv_label_set_text(date_l, date_buf);
    lv_obj_align(date_l, LV_ALIGN_TOP_MID, 0, 2);

    auto* time_l = lv_label_create(body_);
    lv_obj_set_style_text_font(time_l, font28 ? font28 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_l, color, 0);
    lv_label_set_text(time_l, tools::pda::format_hm(now, clock24).c_str());
    lv_obj_align(time_l, LV_ALIGN_TOP_LEFT, 8, 16);

    auto* pct_l = lv_label_create(body_);
    lv_obj_set_style_text_font(pct_l, font12 ? font12 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pct_l, color, 0);
    if (known) {
        lv_label_set_text_fmt(pct_l, "%d%%", bat);
    } else {
        lv_label_set_text(pct_l, "");
    }
    lv_obj_align(pct_l, LV_ALIGN_TOP_RIGHT, -6, 22);

    constexpr int kBarH = 10;
    constexpr int bar_y_pad = 2;
    const int bar_y = view::kContentHeight - kBarH - bar_y_pad;

    const int year = now.tm_year + 1900;
    const int month = now.tm_mon + 1;
    const int first = tools::pda::day_of_week(year, month, 1);
    const int dim = tools::pda::days_in_month(year, month);
    // Pull calendar up so day cells clear the font line-height (was clipping mid-glyph).
    constexpr int cal_top = 40;
    constexpr int margin = 4;
    const int cal_h = bar_y - cal_top - 2;
    const int cell_w = (view::kScreenWidth - 2 * margin) / 7;
    const int header_h = 9;
    const lv_font_t* day_font = font10 ? font10 : &lv_font_montserrat_12;
    const int line_h = lv_font_get_line_height(day_font);
    const int min_cell = line_h + 1;
    const int weeks_needed = (first + dim + 6) / 7;
    int rows = std::min(6, std::max(weeks_needed, 4));
    int cell_h = rows > 0 ? (cal_h - header_h) / rows : 0;
    while (rows > 4 && cell_h < min_cell) {
        --rows;
        cell_h = (cal_h - header_h) / rows;
    }

    if (cell_w >= 8 && cell_h >= min_cell) {
        static const char* wd[] = {"S", "M", "T", "W", "T", "F", "S"};
        for (int c = 0; c < 7; ++c) {
            auto* hdr = lv_label_create(body_);
            lv_label_set_text(hdr, wd[c]);
            lv_obj_set_style_text_font(hdr, day_font, 0);
            lv_obj_set_style_text_color(hdr, color, 0);
            lv_obj_set_pos(hdr, margin + c * cell_w + cell_w / 2 - 3, cal_top);
        }
        for (int d = 1; d <= dim; ++d) {
            const int idx = first + d - 1;
            const int r = idx / 7;
            const int c = idx % 7;
            if (r >= rows) break;
            const int cx = margin + c * cell_w;
            const int cy = cal_top + header_h + r * cell_h;
            const bool today = (d == now.tm_mday);
            if (today) {
                auto* hi = lv_obj_create(body_);
                lv_obj_remove_style_all(hi);
                lv_obj_set_size(hi, std::max(cell_w - 2, 6), std::max(cell_h - 1, min_cell));
                lv_obj_set_pos(hi, cx + 1, cy);
                lv_obj_set_style_radius(hi, 2, 0);
                lv_obj_set_style_bg_color(hi, color, 0);
                lv_obj_set_style_bg_opa(hi, LV_OPA_COVER, 0);
                lv_obj_clear_flag(hi, LV_OBJ_FLAG_SCROLLABLE);
            }
            auto* lab = lv_label_create(body_);
            lv_label_set_text_fmt(lab, "%d", d);
            lv_obj_set_style_text_font(lab, day_font, 0);
            lv_obj_set_style_text_color(lab, today ? tok.bg : color, 0);
            lv_obj_set_size(lab, cell_w - 2, cell_h);
            lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
            // Vertically center within the cell using the real font metrics.
            const int y_off = std::max(0, (cell_h - line_h) / 2);
            lv_obj_set_pos(lab, cx + 1, cy + y_off);
        }
    }

    auto* bar_bg = lv_obj_create(body_);
    lv_obj_remove_style_all(bar_bg);
    lv_obj_set_size(bar_bg, view::kScreenWidth - 16, kBarH);
    lv_obj_set_pos(bar_bg, 8, bar_y);
    lv_obj_set_style_radius(bar_bg, 3, 0);
    lv_obj_set_style_border_width(bar_bg, 1, 0);
    lv_obj_set_style_border_color(bar_bg, color, 0);
    lv_obj_set_style_bg_color(bar_bg, tok.bg, 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);

    if (known) {
        auto* bar_fill = lv_obj_create(bar_bg);
        lv_obj_remove_style_all(bar_fill);
        const int inner_w = view::kScreenWidth - 20;
        const int fill_w = inner_w * bat / 100;
        lv_obj_set_size(bar_fill, fill_w > 0 ? fill_w : 1, kBarH - 4);
        lv_obj_set_pos(bar_fill, 2, 2);
        lv_obj_set_style_bg_color(bar_fill, color, 0);
        lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar_fill, 2, 0);
    }
}

void ClockFaceScreen::toggle_face() {
    if (charge_mode_) return;
    charge_style_ = !charge_style_;
    last_key_[0] = '\0';
    redraw(true);
}

void ClockFaceScreen::cycle_color(int delta) {
    if (!charge_mode_) return;
    color_index_ = (color_index_ + delta) % kFaceColorCount;
    if (color_index_ < 0) color_index_ += kFaceColorCount;
    last_key_[0] = '\0';
    redraw(true);
}

void ClockFaceScreen::step_brightness(int delta) {
    if (!charge_mode_) return;
    int32_t next = current_brightness_ + delta;
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    current_brightness_ = next;
    platform::write_brightness_percent(current_brightness_);
}

bool ClockFaceScreen::handle_key(uint32_t key) {
    if (charge_mode_) {
        if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
            cycle_color(-1);
            return true;
        }
        if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
            cycle_color(1);
            return true;
        }
        if (key == LV_KEY_UP || key == 'f' || key == 'F') {
            step_brightness(10);
            return true;
        }
        if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
            step_brightness(-10);
            return true;
        }
        if (key == LV_KEY_ESC) {
            restore_brightness();
            view_model().go_back();
            return true;
        }
        return KvxScreen::handle_key(key);
    }

    if (key == LV_KEY_LEFT || key == 'z' || key == 'Z' || key == LV_KEY_RIGHT || key == 'c' ||
        key == 'C' || key == '[' || key == ']') {
        toggle_face();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        view_model().show_page(model::AppPage::ClockSubMenu);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> ClockFaceScreen::nav_actions() {
    if (charge_mode_) {
        return {{
            {view::ICON_ARROW_BACK, [this]() {
                 restore_brightness();
                 view_model().go_back();
             }},
            {view::ICON_MINUS, [this]() { step_brightness(-10); }},
            {view::ICON_MENU_MAIN_CLR, [this]() { cycle_color(1); }},
            {view::ICON_PLUS, [this]() { step_brightness(10); }},
            {view::ICON_HOUSE, [this]() {
                 restore_brightness();
                 view_model().go_home();
             }},
        }};
    }
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_SQUARE_ARROW_LEFT, [this]() { toggle_face(); }},
        {view::ICON_CHECK, [this]() { view_model().show_page(model::AppPage::ClockSubMenu); }},
        {view::ICON_SQUARE_ARROW_RIGHT, [this]() { toggle_face(); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

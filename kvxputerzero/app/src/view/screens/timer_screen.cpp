#include "timer_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

TimerScreen::TimerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    remaining_ = view_model.timer_seconds();
    preset_ = remaining_;
    init();
}

TimerScreen::~TimerScreen() {
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
}

void TimerScreen::build() {
    make_header(content(), "Timer");

    auto* font = assets().load_standard_font(28);
    label_ = lv_label_create(content());
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label_, tokens().ink, 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, -18);
    refresh_label();

    auto* hint = lv_label_create(content());
    auto* hint_font = assets().load_standard_font(11);
    lv_label_set_text(hint, "Enter start/stop   +/- 10s");
    lv_obj_set_style_text_font(hint, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, tokens().ink_dim, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    tick_ = lv_timer_create(on_tick, 1000, this);
    lv_timer_pause(tick_);
}

void TimerScreen::refresh_label() {
    const int m = remaining_ / 60;
    const int s = remaining_ % 60;
    lv_label_set_text_fmt(label_, "%02d:%02d", m, s);
}

void TimerScreen::on_tick(lv_timer_t* timer) {
    auto* self = static_cast<TimerScreen*>(lv_timer_get_user_data(timer));
    if (!self || !self->running_) return;
    if (self->remaining_ > 0) {
        self->remaining_--;
        self->refresh_label();
    }
    if (self->remaining_ <= 0) {
        self->running_ = false;
        lv_timer_pause(self->tick_);
        lv_obj_set_style_text_color(self->label_, self->tokens().main, 0);
    }
}

void TimerScreen::toggle_running() {
    running_ = !running_;
    if (running_) {
        lv_timer_resume(tick_);
        lv_obj_set_style_text_color(label_, tokens().accent, 0);
    } else {
        lv_timer_pause(tick_);
        lv_obj_set_style_text_color(label_, tokens().ink, 0);
    }
}

void TimerScreen::adjust(int delta) {
    remaining_ += delta;
    if (remaining_ < 1) remaining_ = 1;
    view_model().set_timer_seconds(remaining_);
    refresh_label();
}

void TimerScreen::reset_timer() {
    running_ = false;
    if (tick_) lv_timer_pause(tick_);
    remaining_ = preset_;
    view_model().set_timer_seconds(remaining_);
    refresh_label();
    lv_obj_set_style_text_color(label_, tokens().ink, 0);
}

bool TimerScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ENTER || key == ' ') {
        toggle_running();
        return true;
    }
    if (key == LV_KEY_RIGHT || key == '+' || key == '=') {
        adjust(10);
        return true;
    }
    if (key == LV_KEY_LEFT || key == '-') {
        adjust(-10);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> TimerScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { adjust(-10); }},
        {view::ICON_PLAY, [this]() { toggle_running(); }},
        {view::ICON_PLUS, [this]() { adjust(10); }},
        {view::ICON_STOP, [this]() { reset_timer(); }},
    }};
}

} // namespace screen

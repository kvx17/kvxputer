#include "totp_show_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <ctime>

namespace screen {

TotpShowScreen::TotpShowScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    std::string error;
    tools::totp::load_accounts(accounts_, error);
    init();
}

TotpShowScreen::~TotpShowScreen() {
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
}

void TotpShowScreen::build() {
    make_header(content(), "TOTP");
    const auto tok = tokens();

    name_ = lv_label_create(content());
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(name_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(name_, tok.ink_dim, 0);
    lv_obj_set_width(name_, view::kScreenWidth - 8);
    lv_obj_align(name_, LV_ALIGN_TOP_LEFT, 4, 22);

    code_ = lv_label_create(content());
    auto* code_font = assets().load_standard_font(28);
    lv_obj_set_style_text_font(code_, code_font ? code_font : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(code_, tok.accent, 0);
    lv_obj_align(code_, LV_ALIGN_CENTER, 0, -4);

    meta_ = lv_label_create(content());
    lv_obj_set_style_text_font(meta_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(meta_, tok.ink_dim, 0);
    lv_obj_align(meta_, LV_ALIGN_BOTTOM_MID, 0, -4);

    tick_ = lv_timer_create(on_tick, 500, this);
    refresh();
}

void TotpShowScreen::on_tick(lv_timer_t* timer) {
    auto* self = static_cast<TotpShowScreen*>(lv_timer_get_user_data(timer));
    if (self) self->refresh();
}

void TotpShowScreen::move_account(int delta) {
    if (accounts_.empty()) return;
    int idx = view_model().totp_index() + delta;
    const int n = static_cast<int>(accounts_.size());
    idx = (idx % n + n) % n;
    view_model().set_totp_index(idx);
    refresh();
}

void TotpShowScreen::refresh() {
    const auto tok = tokens();
    if (accounts_.empty()) {
        lv_label_set_text(name_, "No accounts");
        lv_label_set_text(code_, "------");
        lv_obj_set_style_text_color(code_, tok.ink_dim, 0);
        lv_label_set_text(meta_, "Add one from the TOTP menu");
        return;
    }
    int idx = view_model().totp_index();
    if (idx >= static_cast<int>(accounts_.size())) {
        idx = 0;
        view_model().set_totp_index(0);
    }
    const auto& acc = accounts_[static_cast<size_t>(idx)];
    std::string code;
    int remaining = 0;
    std::string error;
    const time_t now = std::time(nullptr);
    lv_label_set_text_fmt(name_, "%s  (%d/%d)", acc.name.c_str(), idx + 1,
                          static_cast<int>(accounts_.size()));
    if (!tools::totp::code_at(acc, now, code, remaining, error)) {
        lv_label_set_text(code_, "error");
        lv_obj_set_style_text_color(code_, tok.main, 0);
        lv_label_set_text(meta_, error.c_str());
        return;
    }
    lv_label_set_text(code_, code.c_str());
    lv_obj_set_style_text_color(code_, remaining <= 5 ? tok.main : tok.accent, 0);
    lv_label_set_text_fmt(meta_, "%ds remaining   Z/C account", remaining);
}

bool TotpShowScreen::handle_key(uint32_t key) {
    // Zero: Z/C (left/right). Adv used ,/. as arrows — do not bind those chars.
    if (key == LV_KEY_LEFT) {
        move_account(-1);
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        move_account(1);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> TotpShowScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() { move_account(-1); }},
        {},
        {view::ICON_ARROW_RIGHT, [this]() { move_account(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

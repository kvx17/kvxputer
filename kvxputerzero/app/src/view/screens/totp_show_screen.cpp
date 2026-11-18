#include "totp_show_screen.h"

#include "asset_manager.h"
#include "theme.h"

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
    body_ = lv_label_create(content());
    auto* font = assets().load_standard_font(14);
    lv_obj_set_style_text_font(body_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body_, sec_color(), 0);
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body_, view::kScreenWidth - 8);
    lv_obj_align(body_, LV_ALIGN_TOP_LEFT, 4, 20);
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
    if (accounts_.empty()) {
        lv_label_set_text(body_, "No accounts.\nAdd one first.");
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
    if (!tools::totp::code_at(acc, now, code, remaining, error)) {
        lv_label_set_text_fmt(body_, "%s\n%s", acc.name.c_str(), error.c_str());
        return;
    }
    lv_label_set_text_fmt(body_,
                          "%s  (%d/%d)\n\n%s\n\n%ds  ,/. account",
                          acc.name.c_str(),
                          idx + 1,
                          static_cast<int>(accounts_.size()),
                          code.c_str(),
                          remaining);
}

bool TotpShowScreen::handle_key(uint32_t key) {
    if (key == ',' || key == LV_KEY_LEFT) {
        move_account(-1);
        return true;
    }
    if (key == '.' || key == LV_KEY_RIGHT) {
        move_account(1);
        return true;
    }
    return KvxScreen::handle_key(key);
}

} // namespace screen

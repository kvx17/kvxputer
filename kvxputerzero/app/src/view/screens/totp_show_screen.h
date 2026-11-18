#pragma once

#include "kvx_screen.h"
#include "totp_lib.h"

#include <vector>

namespace screen {

class TotpShowScreen final : public KvxScreen {
public:
    TotpShowScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TotpShowScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void on_tick(lv_timer_t* timer);
    void refresh();
    void move_account(int delta);

    std::vector<tools::totp::Account> accounts_;
    lv_obj_t* name_{nullptr};
    lv_obj_t* code_{nullptr};
    lv_obj_t* meta_{nullptr};
    lv_timer_t* tick_{nullptr};
};

} // namespace screen

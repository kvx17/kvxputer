#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>

namespace screen {

class PdaWorldClockScreen final : public KvxScreen {
public:
    PdaWorldClockScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~PdaWorldClockScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void on_tick(lv_timer_t* timer);
    void redraw(bool full);
    void draw_cities(bool full);
    void draw_charge();
    void draw_digital();
    void cycle_face(int delta);
    void move_city(int delta);
    void confirm_set_tz();

    lv_obj_t* body_{nullptr};
    lv_timer_t* tick_{nullptr};
    int face_ = 0;
    int city_ = 0;
    bool confirm_tz_ = false;
    char last_key_[48]{};
};

} // namespace screen

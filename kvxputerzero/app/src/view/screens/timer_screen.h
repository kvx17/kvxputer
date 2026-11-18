#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>

namespace screen {

class TimerScreen final : public KvxScreen {
public:
    TimerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TimerScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void on_tick(lv_timer_t* timer);
    void refresh_label();
    void toggle_running();
    void adjust(int delta);
    void reset_timer();

    lv_obj_t* label_{nullptr};
    lv_timer_t* tick_{nullptr};
    int remaining_ = 0;
    int preset_ = 60;
    bool running_ = false;
};

} // namespace screen

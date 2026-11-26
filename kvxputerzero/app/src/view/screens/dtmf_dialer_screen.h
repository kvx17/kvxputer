#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace screen {

// Phone pad. A matching key plays that tone immediately: 1 plays 1, A plays A.
class DtmfDialerScreen final : public KvxScreen {
public:
    DtmfDialerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~DtmfDialerScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;
    std::array<view::widgets::HintIcon, 2> hint_icons() override;

private:
    static void tick_cb(lv_timer_t* timer);

    void press_index(int index);
    void backspace();
    void refresh_dial();
    void set_lit(int index);
    void enqueue(int index);
    void show_pending_error();
    void player_main();

    lv_obj_t* dial_{nullptr};
    std::array<lv_obj_t*, 16> boxes_{};
    std::array<lv_obj_t*, 16> labels_{};
    std::string dialed_;
    int lit_{-1};
    uint32_t lit_at_{0};
    lv_timer_t* timer_{nullptr};

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<int> queue_;
    std::string error_;
    bool stop_{false};
    std::thread player_;
};

} // namespace screen

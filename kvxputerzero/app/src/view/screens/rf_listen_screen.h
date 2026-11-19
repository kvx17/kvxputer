#pragma once

#include "cc1101.h"
#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class RfListenScreen final : public KvxScreen {
public:
    RfListenScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RfListenScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void start_worker();
    void stop_worker();
    void on_timer();
    void refresh_labels();
    void retune(int delta_steps);
    void toggle_favourite();
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    bool cap_ok_{false};
    bool demo_{false};
    std::string mod_hint_{"?"};

    int rssi_dbm_{-120};
    std::vector<int> mini_rssi_;
    std::vector<uint32_t> mini_freqs_;

    lv_obj_t* info_{nullptr};
    lv_obj_t* bar_bg_{nullptr};
    lv_obj_t* bar_fg_{nullptr};
    lv_obj_t* mini_{nullptr};
    lv_draw_buf_t* mini_buf_{nullptr};
    int mini_stride_{0};
    lv_timer_t* timer_{nullptr};

    std::atomic<bool> cancel_{false};
    std::atomic<bool> ready_{false};
    std::thread worker_;
    std::mutex mu_;
};

} // namespace screen

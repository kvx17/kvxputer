#pragma once

#include "cc1101.h"
#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <cstdint>
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
    void draw_mini();
    void update_bar();
    void retune(int delta_steps);
    void toggle_favourite();
    void geiger_tick();
    bool is_favourite() const;
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    bool cap_ok_{false};
    bool demo_{false};
    bool geiger_on_{false};
    bool help_on_{false};
    std::string activity_{"quiet"};

    int rssi_dbm_{-120};
    int peak_local_{-120};
    int64_t last_geiger_ms_{0};
    std::vector<int> mini_rssi_;
    std::vector<uint32_t> mini_freqs_;

    lv_obj_t* tuned_lbl_{nullptr};
    lv_obj_t* signal_lbl_{nullptr};
    lv_obj_t* footer_lbl_{nullptr};
    lv_obj_t* bar_bg_{nullptr};
    lv_obj_t* bar_fg_{nullptr};
    lv_obj_t* bar_scale_{nullptr};
    lv_obj_t* mini_{nullptr};
    lv_draw_buf_t* mini_buf_{nullptr};
    int mini_stride_{0};
    int mini_w_{312};
    int mini_h_{56};
    lv_timer_t* timer_{nullptr};

    std::atomic<bool> cancel_{false};
    std::atomic<bool> ready_{false};
    std::thread worker_;
    std::mutex mu_;
};

} // namespace screen

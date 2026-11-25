#pragma once

#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class RfRecordScreen final : public KvxScreen {
public:
    RfRecordScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RfRecordScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Phase { Idle, Armed, Capturing, Preview };
    void refresh();
    void arm_or_capture();
    void stop_capture();
    void save_file();
    void trim_start();
    void trim_end();
    void draw_scope();
    void clear_canvas(uint16_t color);
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    Phase phase_{Phase::Idle};
    std::vector<int> timings_;
    int trim_lo_{0};
    int trim_hi_{0};
    int peak_rssi_{-120};
    int live_rssi_{-120};
    int arm_left_s_{0};
    bool rssi_archive_{false};
    bool help_on_{false};
    std::string status_;
    std::string path_saved_;

    // Rolling RSSI history for live scope (Idle / Armed / Capturing).
    std::deque<int> rssi_hist_;
    static constexpr size_t kHistMax = 160;

    lv_obj_t* label_{nullptr};
    lv_obj_t* footer_lbl_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{312};
    int plot_h_{80};
    int plot_top_y_{56};
    lv_timer_t* timer_{nullptr};

    void start_idle_rssi();
    void stop_idle_rssi();

    std::atomic<bool> cancel_{false};
    std::atomic<bool> idle_cancel_{false};
    std::thread worker_;
    std::thread idle_rssi_;
};

} // namespace screen

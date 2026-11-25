#pragma once

#include "kvx_screen.h"
#include "wifi_monitor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace screen {

// Channel airtime bars + alert when load stays high with almost no beacons.
class JamDetectScreen final : public KvxScreen {
public:
    JamDetectScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~JamDetectScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void start_worker();
    void stop_worker();
    void on_timer();
    void draw_bars();
    void update_footer();
    void adjust_threshold(int delta);

    static void timer_cb(lv_timer_t* t);

    platform::WifiAnalyzerState state_{};
    std::string status_msg_;
    std::string alert_msg_;
    uint8_t threshold_{70}; // percent load

    lv_obj_t* footer_lbl_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{320};
    int plot_h_{110};

    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> data_ready_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex data_mu_;
};

} // namespace screen

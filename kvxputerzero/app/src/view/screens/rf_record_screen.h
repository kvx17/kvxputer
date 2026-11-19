#pragma once

#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
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
    void draw_preview();
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    Phase phase_{Phase::Idle};
    std::vector<int> timings_;
    int trim_lo_{0};
    int trim_hi_{0};
    int peak_rssi_{-120};
    std::string status_;
    std::string path_saved_;

    lv_obj_t* label_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    lv_timer_t* timer_{nullptr};

    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

} // namespace screen

#pragma once

#include "imu.h"
#include "kvx_screen.h"

#include <deque>
#include <string>

namespace screen {

// Acceleration per axis, a magnitude trace, shake detection and an impact log.
// (pocket-zero-tools "Motion".) Kickstarter-only notice when no IMU.
class MotionScreen final : public KvxScreen {
public:
    MotionScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~MotionScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void timer_cb(lv_timer_t* t);
    void on_timer();
    void draw();

    platform::Imu imu_;
    bool imu_ok_{false};

    lv_obj_t* notice_{nullptr};
    lv_obj_t* chip_{nullptr};
    lv_obj_t* mag_lbl_{nullptr};
    lv_obj_t* axis_lbl_[3]{nullptr, nullptr, nullptr};
    lv_obj_t* axis_bar_[3]{nullptr, nullptr, nullptr};
    lv_obj_t* imp_cap_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    lv_timer_t* timer_{nullptr};
    int stride_{0};
    int w_{0};
    int h_{0};

    std::deque<float> trace_;
    std::string impacts_;
    bool above_{false};
    float peak_cur_{0.f};
    int impact_n_{0};
    float threshold_{1.8f};
};

} // namespace screen

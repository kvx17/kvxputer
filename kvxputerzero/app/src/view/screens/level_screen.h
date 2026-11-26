#pragma once

#include "imu.h"
#include "instrument.h"
#include "kvx_screen.h"

namespace screen {

// Bubble level + angle finder + tape compass, off the BMI270/BMM150, as three
// Left/Right sections. (pocket-zero-tools "Level".) Kickstarter-only notice when
// the sensors are absent.
class LevelScreen final : public KvxScreen {
public:
    LevelScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~LevelScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void timer_cb(lv_timer_t* t);
    void on_timer();
    void draw();
    void zero_or_calibrate();
    void sync_chrome();

    platform::Imu imu_;
    bool imu_ok_{false};

    lv_obj_t* notice_{nullptr};
    lv_obj_t* chip_{nullptr};
    lv_obj_t* big_{nullptr};
    lv_obj_t* cap_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    view::widgets::SectionTabs tabs_;
    lv_timer_t* timer_{nullptr};
    int stride_{0};
    int w_{0};
    int h_{0};

    bool hold_{false};
    float pitch0_{0.f};
    float roll0_{0.f};
    float pitch_{0.f};
    float roll_{0.f};
    float heading_deg_{-1.f};
    view::widgets::Smooth bubx_, buby_, horizon_, rose_;
    uint32_t last_ms_{0};
    bool flat_{true};              // orientation: resting on a table vs standing
    uint32_t manual_until_{0};     // suppress auto section-switch after a manual Left/Right

    bool mag_cal_{false};
    float mx_min_{1e9f}, mx_max_{-1e9f}, my_min_{1e9f}, my_max_{-1e9f};
};

} // namespace screen

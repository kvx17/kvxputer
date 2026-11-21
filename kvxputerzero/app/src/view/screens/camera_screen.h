#pragma once

#include "camera_session.h"
#include "kvx_screen.h"

#include <array>
#include <string>

namespace screen {

class CameraScreen final : public KvxScreen {
public:
    CameraScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~CameraScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class GuideMode { Off, Grid, Diamond, Lines, All };

    void on_timer();
    void draw_preview();
    void update_hud();
    void rebuild_guides();
    void cycle_guide();
    void adjust_brightness(float delta);
    void take_photo();
    void toggle_record();
    void stop_session();
    void build_help();
    void set_help(bool open);

    static void timer_cb(lv_timer_t* t);

    platform::CameraSession session_;
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{320};
    int plot_h_{134};

    lv_obj_t* hud_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* filter_lbl_{nullptr};
    lv_obj_t* rec_lbl_{nullptr};
    lv_obj_t* guide_layer_{nullptr};

    lv_timer_t* timer_{nullptr};
    GuideMode guide_{GuideMode::Off};
    std::string last_msg_;
    bool chrome_on_{true};
    bool help_open_{false};
    lv_obj_t* help_{nullptr};
    lv_obj_t* help_list_{nullptr};
};

} // namespace screen

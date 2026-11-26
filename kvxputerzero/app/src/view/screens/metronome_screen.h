#pragma once

#include "kvx_screen.h"

namespace screen {

// Tempo with a canvas-drawn pendulum that swings smoothly and clicks either as
// it crosses the middle or at the sides. (pocket-zero-tools "Metronome".)
class MetronomeScreen final : public KvxScreen {
public:
    MetronomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~MetronomeScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class ClickAt { Middle, Sides };

    static void anim_cb(lv_timer_t* t);
    void on_anim();
    void draw(float angle_rad);
    void set_bpm(int bpm);
    void toggle_run();
    void toggle_click_mode();
    void refresh();

    lv_obj_t* bpm_lbl_{nullptr};
    lv_obj_t* beat_cap_{nullptr};
    lv_obj_t* chip_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    lv_timer_t* anim_{nullptr};
    int stride_{0};
    int cw_{0};
    int ch_{0};

    int bpm_{120};
    bool running_{false};
    ClickAt click_at_{ClickAt::Middle};
    double phase_{0.0};        // beats; integer = pendulum at an extreme
    long beat_count_{0};
    uint32_t last_ms_{0};
};

} // namespace screen

#pragma once

#include "kvx_screen.h"

namespace screen {

// Panel as a lamp at full backlight: white / amber / red, with a clear color
// name and a three-swatch selector. Restores prior backlight on exit.
// (pocket-zero-tools "Flashlight".)
class FlashlightScreen final : public KvxScreen {
public:
    FlashlightScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~FlashlightScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void hide_cb(lv_timer_t* t);
    void cycle_color(int delta);
    void apply_color();
    void show_ui();
    void hide_ui();

    lv_obj_t* lamp_{nullptr};
    lv_obj_t* name_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* swatch_[3]{nullptr, nullptr, nullptr};
    lv_timer_t* hide_timer_{nullptr};
    int color_{0};          // 0 white, 1 amber, 2 red
    int32_t prev_brightness_{-1};
    bool brightness_saved_{false};
};

} // namespace screen

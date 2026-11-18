#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>
#include <cstdint>
#include <ctime>

namespace screen {

class ClockFaceScreen final : public KvxScreen {
public:
    ClockFaceScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, bool charge_mode);
    ~ClockFaceScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void on_tick(lv_timer_t* timer);

    void redraw(bool full);
    void draw_digital();
    void draw_charge_style();
    void toggle_face();
    void cycle_color(int delta);
    void step_brightness(int delta);
    void restore_brightness();
    int resolve_battery_percent(bool& known) const;
    lv_color_t face_color() const;
    void format_date(const std::tm& t, char* out, size_t out_len) const;

    bool charge_mode_ = false;
    bool charge_style_ = false;
    int color_index_ = 0;
    int32_t current_brightness_ = 100;
    int32_t saved_brightness_ = -1;
    bool saved_brightness_valid_ = false;
    lv_obj_t* body_{nullptr};
    lv_timer_t* tick_{nullptr};
    char last_key_[64]{};
};

} // namespace screen

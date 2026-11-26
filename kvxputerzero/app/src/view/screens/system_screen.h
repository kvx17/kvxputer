#pragma once

#include "calc_help_modal.h"
#include "kvx_screen.h"

namespace screen {

// Battery, network, memory and disk at a glance with bars; brightness and volume
// adjusted in place. (pocket-zero-tools "System".)
class SystemScreen final : public KvxScreen {
public:
    SystemScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~SystemScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void timer_cb(lv_timer_t* t);
    void refresh();
    void adjust_brightness(int delta);
    void adjust_volume(int delta);
    void toggle_mute();
    void set_bar(lv_obj_t* fill, int pct);
    void open_help();

    view::widgets::CalcHelpModal help_;

    lv_obj_t* chip_{nullptr};
    lv_obj_t* net_val_{nullptr};
    lv_obj_t* batt_val_{nullptr};
    lv_obj_t* batt_bar_{nullptr};
    lv_obj_t* mem_val_{nullptr};
    lv_obj_t* mem_bar_{nullptr};
    lv_obj_t* disk_val_{nullptr};
    lv_obj_t* disk_bar_{nullptr};
    lv_obj_t* bright_val_{nullptr};
    lv_obj_t* bright_bar_{nullptr};
    lv_obj_t* vol_val_{nullptr};
    lv_obj_t* vol_bar_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* timer_{nullptr};

    int brightness_{-1};
    int volume_{-1};
    bool muted_{false};
};

} // namespace screen

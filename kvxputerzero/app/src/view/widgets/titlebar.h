/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

namespace app {
class AssetManager;
}

namespace view::widgets {

class TitleBar : public BaseWidgets {
public:
    TitleBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TitleBar() override;

    void build() override;

private:
    void refresh_time();
    void refresh_device_status();
    static void refresh_timer_cb(lv_timer_t* timer);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* time_label_{nullptr};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* ethernet_label_{nullptr};
    lv_obj_t* battery_icon_label_{nullptr};
    lv_obj_t* battery_percent_label_{nullptr};
    lv_timer_t* refresh_timer_{nullptr};
    uint32_t refresh_tick_{0};
};

} // namespace view::widgets

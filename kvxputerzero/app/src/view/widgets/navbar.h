/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "icon_button.h"

#include <array>
#include <memory>

namespace app {
class AssetManager;
}

namespace view::widgets {

class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    void create_icon_buttons();
    void create_shortcut_hints();
    void update_icon_buttons();

    static void toggle_theme_cb(lv_event_t* event);
    static void toggle_page_cb(lv_event_t* event);
    static void toggle_bold_text_cb(lv_event_t* event);
    static void increment_counter_cb(lv_event_t* event);
    static void decrement_counter_cb(lv_event_t* event);
    static void show_info_cb(lv_event_t* event);
    static void request_quit_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<std::unique_ptr<IconButton>, 5> icon_buttons_;
    std::array<lv_obj_t*, 3> shortcut_key_labels_{};
    lv_font_t* icon_font_{nullptr};
    lv_obj_t* shortcut_bar_{nullptr};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
};

} // namespace view::widgets

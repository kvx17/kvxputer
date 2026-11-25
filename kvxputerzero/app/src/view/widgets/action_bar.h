#pragma once

#include "kvx_style.h"
#include "lvgl.h"

#include <array>
#include <functional>

namespace app {
class AssetManager;
}

namespace view::widgets {

struct ActionSlot {
    const char* icon{nullptr};
    std::function<void()> on_press;
};

// Optional click-only icons drawn on the hint bar. They do not register as
// keys 4–8 nav buttons, so digit entry still works on calculator screens.
struct HintIcon {
    const char* icon{nullptr};
    std::function<void()> on_press;
};

// Bottom row aligned with Cardputer Zero keys 4 5 6 7 8.
class ActionBar {
public:
    ActionBar(lv_obj_t* parent,
              app::AssetManager& assets,
              const view::KvxTokens& tokens,
              std::array<ActionSlot, 5> slots,
              const char* hint,
              std::array<HintIcon, 2> hint_icons = {});
    ~ActionBar();

    ActionBar(const ActionBar&) = delete;
    ActionBar& operator=(const ActionBar&) = delete;

    lv_obj_t* root() const { return root_; }

private:
    static void on_click(lv_event_t* event);
    static void on_hint_icon_click(lv_event_t* event);

    lv_obj_t* root_{nullptr};
    std::array<lv_obj_t*, 5> buttons_{};
    std::array<std::function<void()>, 5> actions_;
    std::array<std::function<void()>, 2> hint_actions_;
};

} // namespace view::widgets

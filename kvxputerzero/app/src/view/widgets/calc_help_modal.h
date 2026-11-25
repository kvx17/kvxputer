#pragma once

#include "kvx_style.h"
#include "lvgl.h"

#include <string>

namespace app {
class AssetManager;
}

namespace view::widgets {

// Shared calculator help / favorites overlay. Styled like HelpPopup but with a
// scrolling body so per-op instructions fit the 320×170 panel.
class CalcHelpModal {
public:
    CalcHelpModal() = default;
    ~CalcHelpModal();

    CalcHelpModal(const CalcHelpModal&) = delete;
    CalcHelpModal& operator=(const CalcHelpModal&) = delete;

    void build(lv_obj_t* parent, app::AssetManager& assets, const view::KvxTokens& tokens);
    void show(const char* title, const std::string& body);
    void hide();
    bool visible() const;
    // True if the key was consumed (modal open). Esc / Tab hide.
    bool handle_key(uint32_t key);

private:
    static void backdrop_clicked(lv_event_t* event);

    lv_obj_t* root_{nullptr};
    lv_obj_t* panel_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* body_{nullptr};
    lv_obj_t* footer_{nullptr};
};

} // namespace view::widgets

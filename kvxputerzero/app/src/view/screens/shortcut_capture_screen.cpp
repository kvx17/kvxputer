#include "shortcut_capture_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "launch_catalog.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

ShortcutCaptureScreen::ShortcutCaptureScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void ShortcutCaptureScreen::set_status(const char* text) {
    if (status_) lv_label_set_text(status_, text ? text : "");
}

void ShortcutCaptureScreen::build() {
    const bool hold = view_model().model().shortcut_bind_hold();
    const auto* target = model::find_launch_target(view_model().model().shortcut_bind_target());
    const std::string title = hold ? "Hold shortcut" : "Tap shortcut";
    make_header(content(), title.c_str());

    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;

    auto* which = lv_label_create(content());
    const std::string name = target ? target->label : std::string("Pick an app");
    const std::string line = std::string(hold ? "Hold 3s opens " : "Tap opens ") + name;
    lv_label_set_text(which, line.c_str());
    lv_obj_set_width(which, view::kScreenWidth - 12);
    lv_label_set_long_mode(which, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(which, font, 0);
    lv_obj_set_style_text_color(which, tok.ink, 0);
    lv_obj_align(which, LV_ALIGN_TOP_LEFT, 6, 24);

    auto* hint = lv_label_create(content());
    lv_label_set_text(hint, "Press a letter. 4-8, F X Z C, Ok, Esc, Space and Tab stay reserved.");
    lv_obj_set_width(hint, view::kScreenWidth - 12);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hint, font, 0);
    lv_obj_set_style_text_color(hint, tok.ink_dim, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 6, 46);

    status_ = lv_label_create(content());
    lv_label_set_text(status_, "Waiting for a key");
    lv_obj_set_width(status_, view::kScreenWidth - 12);
    lv_obj_set_style_text_font(status_, font, 0);
    lv_obj_set_style_text_color(status_, tok.accent, 0);
    lv_obj_align(status_, LV_ALIGN_TOP_LEFT, 6, 92);
}

bool ShortcutCaptureScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (view_model().model().shortcut_bind_target().empty()) {
        set_status("No app selected");
        return true;
    }
    if (!platform::shortcut_key_allowed(key)) {
        set_status("That key is reserved");
        return true;
    }
    view_model().commit_shortcut(key);
    return true;
}

std::array<view::widgets::ActionSlot, 5> ShortcutCaptureScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {},
        {},
        {},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

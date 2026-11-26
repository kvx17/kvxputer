#include "keys_screen.h"

#include "asset_manager.h"
#include "instrument.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>

namespace screen {
namespace {

std::string key_name(uint32_t key) {
    switch (key) {
        case LV_KEY_UP: return "UP";
        case LV_KEY_DOWN: return "DOWN";
        case LV_KEY_LEFT: return "LEFT";
        case LV_KEY_RIGHT: return "RIGHT";
        case LV_KEY_ENTER: return "ENTER";
        case LV_KEY_ESC: return "ESC";
        case LV_KEY_DEL: return "DEL";
        case LV_KEY_BACKSPACE: return "BKSP";
        case LV_KEY_HOME: return "HOME";
        case LV_KEY_END: return "END";
        case LV_KEY_NEXT: return "NEXT";
        case LV_KEY_PREV: return "PREV";
        case ' ': return "SPACE";
        default: break;
    }
    if (key >= 33 && key <= 126) {
        return std::string(1, static_cast<char>(key));
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(key));
    return buf;
}

} // namespace

KeysScreen::KeysScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void KeysScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Keys");

    auto* font = assets().load_standard_font(12);
    auto* big_font = assets().load_standard_font(40);

    big_ = view::widgets::make_readout(root, big_font, tok);
    lv_obj_align(big_, LV_ALIGN_CENTER, 0, -22);

    code_ = view::widgets::make_caption(root, font, tok);
    lv_obj_align(code_, LV_ALIGN_CENTER, 0, 14);

    recent_ = view::widgets::make_caption(root, font, tok);
    lv_obj_set_width(recent_, view::kScreenWidth - 12);
    lv_obj_set_style_text_align(recent_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(recent_, LV_ALIGN_CENTER, 0, 40);

    hint_ = view::widgets::make_hint_bar(root, "Press any key   Esc x2 to exit", font, tok);

    refresh();
}

void KeysScreen::refresh() {
    if (big_) lv_label_set_text(big_, have_key_ ? last_name_.c_str() : "--");
    if (code_) {
        if (have_key_)
            lv_label_set_text_fmt(code_, "code %u   0x%02X", static_cast<unsigned>(last_key_),
                                  static_cast<unsigned>(last_key_));
        else
            lv_label_set_text(code_, "waiting for a key");
    }
    if (recent_) lv_label_set_text_fmt(recent_, "%s", recent_text_.c_str());
}

bool KeysScreen::handle_key(uint32_t key) {
    // First Esc shows the key; a second consecutive Esc leaves.
    if (key == LV_KEY_ESC) {
        if (esc_count_ >= 1) {
            return KvxScreen::handle_key(key);
        }
        ++esc_count_;
    } else {
        esc_count_ = 0;
    }

    last_key_ = key;
    last_name_ = key_name(key);
    have_key_ = true;
    recent_text_ = last_name_ + "   " + recent_text_;
    if (recent_text_.size() > 120) recent_text_.resize(120);
    refresh();
    return true;
}

std::array<view::widgets::ActionSlot, 5> KeysScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {nullptr, nullptr},
        {nullptr, nullptr},
        {nullptr, nullptr},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

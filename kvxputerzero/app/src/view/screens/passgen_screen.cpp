#include "passgen_screen.h"

#include "asset_manager.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

PassgenScreen::PassgenScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void PassgenScreen::build() {
    make_header(content(), "Passgen");
    body_ = lv_label_create(content());
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(body_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body_, view::kScreenWidth - 8);
    lv_obj_align(body_, LV_ALIGN_TOP_LEFT, 4, 20);
    regenerate();
}

void PassgenScreen::refresh() {
    std::string text;
    text += "Len " + std::to_string(opt_.length) + "  Cnt " + std::to_string(opt_.count) + "\n";
    text += opt_.upper ? "[U] " : " U  ";
    text += opt_.lower ? "[L] " : " L  ";
    text += opt_.digits ? "[D] " : " D  ";
    text += opt_.symbols ? "[C]\n" : " C\n";
    text += ";/. len  , cnt  G gen  S save\n";
    if (passwords_.empty()) {
        text += "Press G to generate";
    } else {
        for (size_t i = 0; i < passwords_.size(); ++i) {
            text += std::to_string(i + 1) + ") " + passwords_[i] + "\n";
        }
    }
    if (!status_.empty()) {
        text += status_;
    }
    lv_label_set_text(body_, text.c_str());
}

void PassgenScreen::regenerate() {
    passwords_ = tools::passgen::generate(opt_);
    status_.clear();
    refresh();
}

void PassgenScreen::save() {
    if (passwords_.empty()) {
        status_ = "nothing to save";
        refresh();
        return;
    }
    std::string path;
    std::string error;
    if (tools::passgen::append_to_file(passwords_, path, error)) {
        status_ = "saved " + path;
    } else {
        status_ = error;
    }
    refresh();
}

bool PassgenScreen::handle_key(uint32_t key) {
    if (key == ';' || key == LV_KEY_LEFT) {
        if (opt_.length > 4) --opt_.length;
        regenerate();
        return true;
    }
    if (key == '.' || key == LV_KEY_RIGHT) {
        if (opt_.length < 64) ++opt_.length;
        regenerate();
        return true;
    }
    if (key == ',') {
        if (opt_.count < 8) ++opt_.count;
        else opt_.count = 1;
        regenerate();
        return true;
    }
    if (key == 'g' || key == 'G' || key == LV_KEY_ENTER) {
        regenerate();
        return true;
    }
    if (key == 's' || key == 'S') {
        save();
        return true;
    }
    if (key == 'u' || key == 'U') {
        opt_.upper = !opt_.upper;
        regenerate();
        return true;
    }
    if (key == 'l' || key == 'L') {
        opt_.lower = !opt_.lower;
        regenerate();
        return true;
    }
    if (key == 'd' || key == 'D') {
        opt_.digits = !opt_.digits;
        regenerate();
        return true;
    }
    if (key == 'c' || key == 'C') {
        opt_.symbols = !opt_.symbols;
        regenerate();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PassgenScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() {
             if (opt_.length > 4) --opt_.length;
             regenerate();
         }},
        {view::ICON_PLAY, [this]() { regenerate(); }},
        {view::ICON_PLUS, [this]() {
             if (opt_.length < 64) ++opt_.length;
             regenerate();
         }},
        {},
    }};
}

} // namespace screen

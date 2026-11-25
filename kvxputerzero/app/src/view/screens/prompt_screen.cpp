#include "prompt_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"

namespace screen {

PromptScreen::PromptScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                           std::string hint, std::string initial, ConfirmFn on_confirm, bool secret)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      hint_(std::move(hint)),
      initial_(std::move(initial)),
      on_confirm_(std::move(on_confirm)),
      secret_(secret) {
    init();
}

void PromptScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    const lv_font_t* use_font = font ? font : &lv_font_montserrat_12;
    auto* hint = lv_label_create(content());
    lv_label_set_text(hint, hint_.c_str());
    lv_obj_set_style_text_color(hint, tok.ink, 0);
    lv_obj_set_style_text_font(hint, use_font, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, view::kScreenWidth - 8);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 4, 22);

    input_ta_ = view::make_line_input(content(), tok, use_font, view::kScreenWidth - 16);
    if (secret_) {
        lv_textarea_set_password_mode(input_ta_, true);
        lv_textarea_set_password_show_time(input_ta_, 0);
        lv_textarea_set_placeholder_text(input_ta_, "type password");
        lv_obj_set_style_border_color(input_ta_, tok.accent, LV_PART_MAIN);
        lv_obj_set_style_border_width(input_ta_, 2, LV_PART_MAIN);
    }
    lv_textarea_set_text(input_ta_, initial_.c_str());
    lv_obj_align_to(input_ta_, hint, LV_ALIGN_OUT_BOTTOM_LEFT, 0, secret_ ? 10 : 6);
    // Typing is handled in handle_key (no LVGL keypad group — avoids stale focus after leave).
}

void PromptScreen::confirm() {
    const char* text = lv_textarea_get_text(input_ta_);
    std::string value = text ? text : "";
    if (!secret_) view_model().set_work_input(value);
    if (on_confirm_) on_confirm_(value);
}

const char* PromptScreen::typing_hint() const {
    return secret_ ? "Password · Enter confirm · Esc back" : "Type · Enter confirm · Esc back";
}

bool PromptScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        confirm();
        return true;
    }
    if (!input_ta_) return KvxScreen::handle_key(key);
    if (key == LV_KEY_BACKSPACE || key == 8) {
        lv_textarea_delete_char(input_ta_);
        return true;
    }
    if (key == LV_KEY_DEL || key == 127) {
        lv_textarea_delete_char_forward(input_ta_);
        return true;
    }
    if (key == LV_KEY_LEFT) {
        lv_textarea_cursor_left(input_ta_);
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        lv_textarea_cursor_right(input_ta_);
        return true;
    }
    if (key >= 32 && key <= 126) {
        lv_textarea_add_char(input_ta_, key);
        return true;
    }
    return KvxScreen::handle_key(key);
}

} // namespace screen

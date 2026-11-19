#include "work_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"

namespace screen {

WorkScreen::WorkScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       std::string title,
                       std::string hint,
                       RunFn run)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      hint_(std::move(hint)),
      run_(std::move(run)) {
    init();
}

void WorkScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();

    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;

    auto* hint = lv_label_create(content());
    lv_label_set_text(hint, hint_.c_str());
    lv_obj_set_style_text_color(hint, tok.ink_dim, 0);
    lv_obj_set_style_text_font(hint, font, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(hint, view::kScreenWidth - 8);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 4, 20);

    input_ta_ = view::make_line_input(content(), tok, font, view::kScreenWidth - 8);
    lv_textarea_set_placeholder_text(input_ta_, "input");
    lv_textarea_set_text(input_ta_, view_model().work_input().c_str());
    lv_obj_align(input_ta_, LV_ALIGN_TOP_MID, 0, 36);

    const int input_h = lv_obj_get_height(input_ta_);
    const int out_y = 36 + input_h + 4;
    auto* output_box = lv_obj_create(content());
    lv_obj_remove_style_all(output_box);
    lv_obj_set_size(output_box, view::kScreenWidth - 8, view::kContentHeight - out_y);
    lv_obj_align(output_box, LV_ALIGN_TOP_LEFT, 4, out_y);
    lv_obj_add_flag(output_box, LV_OBJ_FLAG_SCROLLABLE);

    output_label_ = lv_label_create(output_box);
    lv_label_set_text(output_label_, view_model().work_output().empty() ? "(result)"
                                                                        : view_model().work_output().c_str());
    lv_obj_set_style_text_color(output_label_, tok.ink, 0);
    lv_obj_set_style_text_font(output_label_, font, 0);
    lv_label_set_long_mode(output_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(output_label_, view::kScreenWidth - 12);
    // Typing is handled in handle_key (no LVGL keypad group — avoids stale focus after leave).
}

void WorkScreen::run_now() {
    const char* text = lv_textarea_get_text(input_ta_);
    std::string input = text ? text : "";
    view_model().set_work_input(input);
    bool ok = true;
    std::string out = run_ ? run_(input, ok) : "";
    if (!ok && out.empty()) out = "error";
    view_model().set_work_output(out);
    lv_label_set_text(output_label_, out.c_str());
}

const char* WorkScreen::typing_hint() const {
    return "Type · Enter confirm · Esc back";
}

bool WorkScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        run_now();
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

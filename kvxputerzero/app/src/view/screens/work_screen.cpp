#include "work_screen.h"

#include "asset_manager.h"
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

    auto* font = assets().load_standard_font(12);
    auto* hint = lv_label_create(content());
    lv_label_set_text(hint, hint_.c_str());
    lv_obj_set_style_text_color(hint, sec_color(), 0);
    lv_obj_set_style_text_font(hint, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_width(hint, view::kScreenWidth - 8);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 4, 20);

    input_ta_ = lv_textarea_create(content());
    lv_obj_set_size(input_ta_, view::kScreenWidth - 8, 28);
    lv_obj_align(input_ta_, LV_ALIGN_TOP_MID, 0, 36);
    lv_textarea_set_one_line(input_ta_, true);
    lv_textarea_set_placeholder_text(input_ta_, "Type input, Enter to run");
    lv_textarea_set_text(input_ta_, view_model().work_input().c_str());
    lv_obj_set_style_bg_color(input_ta_, lv_color_hex(0x1A1A22), 0);
    lv_obj_set_style_text_color(input_ta_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(input_ta_, pri_color(), 0);
    lv_obj_set_style_border_width(input_ta_, 1, 0);

    output_label_ = lv_label_create(content());
    lv_label_set_text(output_label_, view_model().work_output().empty() ? "(result)"
                                                                        : view_model().work_output().c_str());
    lv_obj_set_style_text_color(output_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(output_label_, font ? font : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(output_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(output_label_, view::kScreenWidth - 8);
    lv_obj_set_height(output_label_, view::kContentHeight - 68);
    lv_obj_align(output_label_, LV_ALIGN_TOP_LEFT, 4, 68);

    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, input_ta_);
    if (lv_indev_t* indev = lv_indev_get_next(nullptr); indev) {
        // Prefer keyboard indev if present.
        for (lv_indev_t* d = lv_indev_get_next(nullptr); d; d = lv_indev_get_next(d)) {
            if (lv_indev_get_type(d) == LV_INDEV_TYPE_KEYPAD) {
                indev = d;
                break;
            }
        }
        lv_indev_set_group(indev, g);
    }
    lv_group_focus_obj(input_ta_);
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
    return "Fn+key types    Enter runs    Esc back";
}

bool WorkScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        run_now();
        return true;
    }
    return KvxScreen::handle_key(key);
}

} // namespace screen

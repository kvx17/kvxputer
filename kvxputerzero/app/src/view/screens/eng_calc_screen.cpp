#include "eng_calc_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"

namespace screen {
namespace {

constexpr int kLineScroll = 12;

} // namespace

EngCalcScreen::EngCalcScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             tools::engcalc::Op op)
    : KvxScreen(view_model, assets), op_(op) {
    init();
}

void EngCalcScreen::build() {
    make_header(content(), tools::engcalc::op_label(op_));
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;

    auto* example = lv_label_create(content());
    lv_label_set_text(example, tools::engcalc::input_example(op_));
    lv_obj_set_pos(example, 4, 20);
    lv_obj_set_width(example, view::kScreenWidth - 8);
    lv_label_set_long_mode(example, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(example, font10, 0);
    lv_obj_set_style_text_color(example, tok.ink_dim, 0);

    // Space shown as '_' on the chip; inserts a real space.
    keys_.build(content(), tok, font10, 34, view::kScreenWidth - 8, "./-+:, ",
                [this](char ch) { insert_char(ch); });

    input_ta_ = view::make_line_input(content(), tok, font, view::kScreenWidth - 8);
    lv_textarea_set_placeholder_text(input_ta_, "input");
    lv_textarea_set_text(input_ta_, view_model().work_input().c_str());
    lv_obj_align(input_ta_, LV_ALIGN_TOP_MID, 0, 54);

    constexpr int kOutTop = 76;
    constexpr int kHintReserve = 12;
    output_box_ = lv_obj_create(content());
    lv_obj_remove_style_all(output_box_);
    lv_obj_set_size(output_box_, view::kScreenWidth - 8, view::kContentHeight - kOutTop - kHintReserve);
    lv_obj_align(output_box_, LV_ALIGN_TOP_LEFT, 4, kOutTop);
    lv_obj_add_flag(output_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(output_box_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(output_box_, LV_SCROLLBAR_MODE_AUTO);

    output_label_ = lv_label_create(output_box_);
    const char* initial = view_model().work_output().empty()
                              ? "Type a value, Enter runs.\nF/X/Z/C move · ↑ ops inserts ./"
                              : view_model().work_output().c_str();
    lv_label_set_text(output_label_, initial);
    lv_obj_set_style_text_color(output_label_, tok.ink, 0);
    lv_obj_set_style_text_font(output_label_, font, 0);
    lv_label_set_long_mode(output_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(output_label_, view::kScreenWidth - 12);
    // SIZE_CONTENT so the box can scroll past the first visible rows.
    lv_obj_set_height(output_label_, LV_SIZE_CONTENT);

    scroll_hint_ = lv_label_create(content());
    lv_label_set_text(scroll_hint_, "");
    lv_obj_set_style_text_font(scroll_hint_, font10, 0);
    lv_obj_set_style_text_color(scroll_hint_, tok.ink_dim, 0);
    lv_obj_align(scroll_hint_, LV_ALIGN_BOTTOM_RIGHT, -4, -1);
    update_scroll_hint();
}

uint32_t EngCalcScreen::map_nav_letter(uint32_t key) {
    // Bare F/X/Z/C = Up/Down/Left/Right (no Fn) on eng-calc screens.
    switch (key) {
        case 'f':
        case 'F':
            return LV_KEY_UP;
        case 'x':
        case 'X':
            return LV_KEY_DOWN;
        case 'z':
        case 'Z':
            return LV_KEY_LEFT;
        case 'c':
        case 'C':
            return LV_KEY_RIGHT;
        default:
            return key;
    }
}

void EngCalcScreen::insert_char(char ch) {
    if (!input_ta_) return;
    result_focus_ = false;
    lv_textarea_add_char(input_ta_, static_cast<uint32_t>(static_cast<unsigned char>(ch)));
}

bool EngCalcScreen::type_key(uint32_t key) {
    if (!input_ta_) return false;
    if (key == LV_KEY_BACKSPACE || key == 8) {
        result_focus_ = false;
        lv_textarea_delete_char(input_ta_);
        return true;
    }
    if (key == LV_KEY_DEL || key == 127) {
        result_focus_ = false;
        lv_textarea_delete_char_forward(input_ta_);
        return true;
    }
    // Left/Right move the caret only while editing (not browsing results / ops).
    if (!result_focus_ && !keys_.focused()) {
        if (key == LV_KEY_LEFT) {
            lv_textarea_cursor_left(input_ta_);
            return true;
        }
        if (key == LV_KEY_RIGHT) {
            lv_textarea_cursor_right(input_ta_);
            return true;
        }
    }
    if (key >= 32 && key <= 126) {
        result_focus_ = false;
        lv_textarea_add_char(input_ta_, key);
        return true;
    }
    return false;
}

void EngCalcScreen::scroll_result(int delta_px) {
    if (!output_box_) return;
    result_focus_ = true;
    keys_.set_focused(false);
    const int32_t y = lv_obj_get_scroll_y(output_box_);
    lv_obj_scroll_to_y(output_box_, y + delta_px, LV_ANIM_OFF);
    update_scroll_hint();
}

void EngCalcScreen::update_scroll_hint() {
    if (!output_box_ || !scroll_hint_) return;
    const int32_t scroll_y = lv_obj_get_scroll_y(output_box_);
    const int32_t bottom = lv_obj_get_scroll_bottom(output_box_);
    if (bottom <= 2 && scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "");
        return;
    }
    if (bottom <= 2) {
        lv_label_set_text(scroll_hint_, "^ X/F");
    } else if (scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "v X/F");
    } else {
        lv_label_set_text(scroll_hint_, "^v X/F");
    }
}

void EngCalcScreen::run_now() {
    const char* text = lv_textarea_get_text(input_ta_);
    std::string input = text ? text : "";
    view_model().set_work_input(input);
    bool ok = true;
    std::string out = tools::engcalc::run(op_, input, ok);
    if (!ok && out.empty()) out = "error";
    view_model().set_work_output(out);
    lv_label_set_text(output_label_, out.c_str());
    // Re-assert wrap + content height after text change so scrolling works.
    lv_obj_set_width(output_label_, view::kScreenWidth - 12);
    lv_obj_set_height(output_label_, LV_SIZE_CONTENT);
    if (output_box_) {
        lv_obj_scroll_to_y(output_box_, 0, LV_ANIM_OFF);
    }
    result_focus_ = true;
    keys_.set_focused(false);
    update_scroll_hint();
}

const char* EngCalcScreen::typing_hint() const {
    return "Enter run · F/X/Z/C move · Esc";
}

std::array<view::widgets::ActionSlot, 5> EngCalcScreen::nav_actions() {
    return {};
}

bool EngCalcScreen::handle_key(uint32_t key) {
    key = map_nav_letter(key);

    if (key == LV_KEY_ESC) {
        if (keys_.focused()) {
            keys_.set_focused(false);
            return true;
        }
        if (result_focus_) {
            result_focus_ = false;
            return true;
        }
        view_model().go_back();
        return true;
    }

    // Result browsing: F/X (Up/Down) scroll the output after Enter.
    if (result_focus_ && output_box_ && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
        if (key == LV_KEY_UP && lv_obj_get_scroll_y(output_box_) <= 0) {
            result_focus_ = false;
            keys_.set_focused(true); // Up from top of results → ops strip
            return true;
        }
        scroll_result(key == LV_KEY_DOWN ? kLineScroll : -kLineScroll);
        return true;
    }

    // Down from editing jumps into the result list (if there is overflow).
    if (!keys_.focused() && !result_focus_ && key == LV_KEY_DOWN && output_box_ &&
        lv_obj_get_scroll_bottom(output_box_) > 2) {
        scroll_result(kLineScroll);
        return true;
    }

    if (keys_.handle_key(key)) {
        result_focus_ = false;
        return true;
    }

    if (key == LV_KEY_ENTER) {
        run_now();
        return true;
    }

    if (type_key(key)) return true;
    return KvxScreen::handle_key(key);
}

} // namespace screen

#include "calculator_screen.h"

#include "asset_manager.h"
#include "eng_calc_help.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <string>

namespace screen {

CalculatorScreen::CalculatorScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    tools::calc::load_prefs(state_);
    const auto sci = tools::calcpersist::load_scientific();
    if (!sci.history.empty()) state_.history = sci.history;
    init();
}

void CalculatorScreen::build() {
    make_header(content(), "Calculator");
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;

    example_label_ = lv_label_create(content());
    lv_label_set_text(example_label_, "e.g.  sin(30)+sqrt(2)   ans*pi");
    lv_obj_set_pos(example_label_, 4, 20);
    lv_obj_set_width(example_label_, view::kScreenWidth - 8);
    lv_label_set_long_mode(example_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(example_label_, font10, 0);
    lv_obj_set_style_text_color(example_label_, tok.ink_dim, 0);

    keys_.build(
        content(), tok, font10, 34, view::kScreenWidth - 8, "./-+*(),",
        [this](char ch) { insert_char(ch); }, state_.deg_mode ? "DEG" : "RAD",
        [this]() { toggle_mode(); });

    input_ta_ = view::make_line_input(content(), tok, font, view::kScreenWidth - 8);
    lv_textarea_set_placeholder_text(input_ta_, "expression");
    lv_textarea_set_max_length(input_ta_, 96);
    const auto sci = tools::calcpersist::load_scientific();
    // Do not reuse eng-calc work_input (e.g. a MAC) as the expression.
    lv_textarea_set_text(input_ta_, sci.last_expr.c_str());
    lv_obj_align(input_ta_, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_border_color(input_ta_, tok.accent, 0);
    lv_obj_set_style_border_width(input_ta_, 2, 0);
    lv_obj_set_style_bg_color(input_ta_, tok.surface_sel, 0);

    history_label_ = lv_label_create(content());
    lv_obj_set_pos(history_label_, 4, 76);
    lv_obj_set_size(history_label_, view::kScreenWidth - 8, view::kContentHeight - 78);
    lv_label_set_long_mode(history_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(history_label_, font10, 0);
    lv_obj_set_style_text_color(history_label_, tok.ink_dim, 0);

    help_.build(root(), assets(), tok);
    refresh_mode();
    refresh_history();
}

void CalculatorScreen::refresh_history() {
    if (!history_label_) return;
    if (state_.history.empty()) {
        // Keep the area empty so the first viewport is not a tip wall.
        lv_label_set_text(history_label_, "");
        return;
    }
    std::string text;
    const int n = static_cast<int>(state_.history.size());
    for (int i = 0; i < n && i < 5; ++i) {
        if (i) text += "\n";
        text += state_.history[static_cast<size_t>(i)];
    }
    lv_label_set_text(history_label_, text.c_str());
}

void CalculatorScreen::refresh_mode() {
    keys_.set_mode_label(state_.deg_mode ? "DEG" : "RAD");
}

void CalculatorScreen::toggle_mode() {
    state_.deg_mode = !state_.deg_mode;
    tools::calc::save_prefs(state_);
    refresh_mode();
}

void CalculatorScreen::insert_char(char ch) {
    if (!input_ta_) return;
    lv_textarea_add_char(input_ta_, static_cast<uint32_t>(static_cast<unsigned char>(ch)));
}

bool CalculatorScreen::type_key(uint32_t key) {
    if (!input_ta_) return false;
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
    return false;
}

void CalculatorScreen::persist_state() {
    tools::calcpersist::SciState sci;
    if (input_ta_) {
        const char* t = lv_textarea_get_text(input_ta_);
        sci.last_expr = t ? t : "";
    }
    sci.history = state_.history;
    tools::calcpersist::save_scientific(sci);
}

void CalculatorScreen::evaluate_now() {
    if (!input_ta_) return;
    const char* text = lv_textarea_get_text(input_ta_);
    const std::string expr = text ? text : "";
    if (expr.empty()) {
        if (history_label_) lv_label_set_text(history_label_, "type an expression | Tab help");
        return;
    }
    view_model().set_work_input(expr);
    double result = 0;
    std::string error;
    if (!tools::calc::evaluate(expr, state_, result, error)) {
        if (history_label_) {
            lv_label_set_text(history_label_,
                              error == "empty" ? "type an expression | Tab help"
                                               : "syntax / math error");
        }
        return;
    }
    const std::string formatted = tools::calc::format_result(result);
    lv_textarea_set_text(input_ta_, formatted.c_str());
    view_model().set_work_input(formatted);
    refresh_history();
    persist_state();
}

void CalculatorScreen::open_help() {
    if (help_.visible()) {
        help_.hide();
        return;
    }
    help_.show("Calculator", tools::engcalc::scientific_help_text());
}

const char* CalculatorScreen::typing_hint() const {
    return "Tab help | Enter run | up ops | Esc";
}

std::array<view::widgets::ActionSlot, 5> CalculatorScreen::nav_actions() {
    return {};
}

std::array<view::widgets::HintIcon, 2> CalculatorScreen::hint_icons() {
    return {{
        {view::ICON_INFO, [this]() { open_help(); }},
        {},
    }};
}

bool CalculatorScreen::handle_key(uint32_t key) {
    if (help_.visible()) return help_.handle_key(key);
    if (key == platform::kKeyTab) {
        open_help();
        return true;
    }

    if (key == LV_KEY_ESC) {
        keys_.set_focused(false);
        persist_state();
        view_model().go_back();
        return true;
    }

    if (keys_.handle_key(key)) {
        return true;
    }

    if (key == LV_KEY_ENTER) {
        evaluate_now();
        return true;
    }

    if (type_key(key)) return true;
    return KvxScreen::handle_key(key);
}

} // namespace screen

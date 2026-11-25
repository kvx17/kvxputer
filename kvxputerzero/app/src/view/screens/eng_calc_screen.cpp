#include "eng_calc_screen.h"

#include "asset_manager.h"
#include "eng_calc_help.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>

namespace screen {
namespace {

constexpr int kLineScroll = 12;
constexpr int kFieldH = 18;

lv_obj_t* make_small_ta(lv_obj_t* parent, const view::KvxTokens& tok, const lv_font_t* font, int w) {
    auto* ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, kFieldH);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 96);
    lv_obj_set_style_pad_all(ta, 2, 0);
    lv_obj_set_style_radius(ta, 3, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, tok.rule, 0);
    lv_obj_set_style_bg_color(ta, tok.surface, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(ta, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ta, tok.ink, 0);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
    return ta;
}

} // namespace

EngCalcScreen::EngCalcScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             tools::engcalc::Op op)
    : KvxScreen(view_model, assets), op_(op) {
    persist_ = tools::calcpersist::load_op(op_);
    init();
}

void EngCalcScreen::build() {
    make_header(content(), tools::engcalc::op_label(op_));
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;

    const auto& sch = tools::engcalc::schema_for(op_);
    std::string initial = persist_.last_line;
    if (initial.empty()) initial = view_model().work_input();
    tools::engcalc::parse_line_into(op_, initial, mode_index_, values_);

    int y = 20;
    if (sch.modes && sch.mode_count > 0) {
        mode_label_ = lv_label_create(content());
        lv_obj_set_pos(mode_label_, 4, y);
        lv_obj_set_width(mode_label_, view::kScreenWidth - 8);
        lv_obj_set_style_text_font(mode_label_, font10, 0);
        lv_obj_set_style_text_color(mode_label_, tok.ink_dim, 0);
        y += 12;
    }

    fields_row_ = lv_obj_create(content());
    lv_obj_remove_style_all(fields_row_);
    lv_obj_set_pos(fields_row_, 2, y);
    lv_obj_set_width(fields_row_, view::kScreenWidth - 4);
    lv_obj_set_height(fields_row_, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fields_row_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(fields_row_, 4, 0);
    lv_obj_set_style_pad_row(fields_row_, 2, 0);
    lv_obj_clear_flag(fields_row_, LV_OBJ_FLAG_SCROLLABLE);

    if (sch.kind == tools::engcalc::FormKind::Paste) {
        paste_ta_ = view::make_line_input(content(), tok, font, view::kScreenWidth - 8);
        lv_textarea_set_placeholder_text(paste_ta_, "paste / type");
        if (!values_.empty()) lv_textarea_set_text(paste_ta_, values_[0].c_str());
        lv_obj_align(paste_ta_, LV_ALIGN_TOP_MID, 0, y);
        fields_bottom_y_ = y + 20;
    } else {
        rebuild_fields();
    }

    const auto& sch_glyphs = tools::engcalc::schema_for(op_);
    const bool has_glyphs = sch_glyphs.glyphs && sch_glyphs.glyphs[0] != '\0';
    if (has_glyphs) {
        keys_.build(content(), tok, font10, fields_bottom_y_ + 2, view::kScreenWidth - 8, sch_glyphs.glyphs,
                    [this](char ch) { insert_char(ch); });
        keys_built_ = true;
    } else {
        keys_built_ = false;
    }

    // Result sits right under fields, or under the glyph strip when present.
    const int out_top = fields_bottom_y_ + (keys_built_ ? 22 : 4);
    output_box_ = lv_obj_create(content());
    lv_obj_remove_style_all(output_box_);
    lv_obj_set_size(output_box_, view::kScreenWidth - 8, std::max(24, view::kContentHeight - out_top - 2));
    lv_obj_align(output_box_, LV_ALIGN_TOP_LEFT, 4, out_top);
    lv_obj_add_flag(output_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(output_box_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(output_box_, LV_SCROLLBAR_MODE_AUTO);

    output_label_ = lv_label_create(output_box_);
    const char* initial_out =
        view_model().work_output().empty() ? "" : view_model().work_output().c_str();
    lv_label_set_text(output_label_, initial_out);
    lv_obj_set_style_text_color(output_label_, tok.ink, 0);
    lv_obj_set_style_text_font(output_label_, font, 0);
    lv_label_set_long_mode(output_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(output_label_, view::kScreenWidth - 12);
    lv_obj_set_height(output_label_, LV_SIZE_CONTENT);

    scroll_hint_ = lv_label_create(content());
    lv_label_set_text(scroll_hint_, "");
    lv_obj_set_style_text_font(scroll_hint_, font10, 0);
    lv_obj_set_style_text_color(scroll_hint_, tok.ink_dim, 0);
    lv_obj_align(scroll_hint_, LV_ALIGN_BOTTOM_RIGHT, -4, -1);

    help_.build(root(), assets(), tok);
    favorites_.build(root(), assets(), tok);
    // Always land on the first value field so typing works immediately.
    focus_field(0);
    update_scroll_hint();
}

void EngCalcScreen::layout_below_fields() {
    if (keys_built_) keys_.set_y(fields_bottom_y_ + 2);
    const int out_top = fields_bottom_y_ + (keys_built_ ? 22 : 4);
    if (output_box_) {
        lv_obj_set_y(output_box_, out_top);
        lv_obj_set_height(output_box_, std::max(24, view::kContentHeight - out_top - 2));
    }
}

void EngCalcScreen::rebuild_fields() {
    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;
    const auto& sch = tools::engcalc::schema_for(op_);

    if (mode_label_ && sch.modes && sch.mode_count > 0) {
        const int mi = std::max(0, std::min(mode_index_, sch.mode_count - 1));
        std::string t = "mode: ";
        t += sch.modes[mi].label;
        t += "  (Z/C)";
        lv_label_set_text(mode_label_, t.c_str());
    }

    if (!fields_row_ || sch.kind == tools::engcalc::FormKind::Paste) {
        fields_bottom_y_ = paste_ta_ ? 40 : 20;
        return;
    }
    lv_obj_clean(fields_row_);
    field_tas_.clear();

    int n = 0;
    const tools::engcalc::FieldDef* fields = tools::engcalc::active_fields(op_, mode_index_, n);
    if (sch.kind == tools::engcalc::FormKind::List) {
        if (static_cast<int>(values_.size()) < sch.list_min) {
            values_.resize(static_cast<size_t>(sch.list_min));
        }
        n = static_cast<int>(values_.size());
        fields = sch.fields;
    } else if (static_cast<int>(values_.size()) < n) {
        values_.resize(static_cast<size_t>(n));
    }

    const int cols = (n <= 2) ? std::max(1, n) : 2;
    const int cell_w = (view::kScreenWidth - 12) / cols;
    for (int i = 0; i < n; ++i) {
        auto* cell = lv_obj_create(fields_row_);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, cell_w - 2, kFieldH);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        auto* lab = lv_label_create(cell);
        const char* name = (fields && i < (sch.kind == tools::engcalc::FormKind::List
                                               ? sch.field_count
                                               : n))
                               ? fields[sch.kind == tools::engcalc::FormKind::List ? 0 : i].label
                               : "v";
        if (sch.kind == tools::engcalc::FormKind::List && fields) name = fields[0].label;
        lv_label_set_text(lab, name);
        lv_obj_set_style_text_font(lab, font10, 0);
        lv_obj_set_style_text_color(lab, tok.ink_dim, 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

        auto* ta = make_small_ta(cell, tok, font10, cell_w - 40);
        lv_obj_align(ta, LV_ALIGN_RIGHT_MID, 0, 0);
        if (static_cast<size_t>(i) < values_.size()) {
            lv_textarea_set_text(ta, values_[static_cast<size_t>(i)].c_str());
        }
        field_tas_.push_back(ta);
    }

    lv_obj_update_layout(fields_row_);
    const int row_h = std::max(kFieldH, static_cast<int>(lv_obj_get_height(fields_row_)));
    fields_bottom_y_ = static_cast<int>(lv_obj_get_y(fields_row_)) + row_h;
    layout_below_fields();
}

void EngCalcScreen::focus_field(int index) {
    focus_ = index;
    result_focus_ = false;
    keys_.set_focused(false);
    const auto tok = tokens();
    for (size_t i = 0; i < field_tas_.size(); ++i) {
        const bool on = static_cast<int>(i) == focus_;
        lv_obj_set_style_border_color(field_tas_[i], on ? tok.accent : tok.rule, 0);
        lv_obj_set_style_border_width(field_tas_[i], on ? 2 : 1, 0);
        lv_obj_set_style_bg_color(field_tas_[i], on ? tok.surface_sel : tok.surface, 0);
    }
    if (paste_ta_) {
        const bool on = focus_ == 0;
        lv_obj_set_style_border_color(paste_ta_, on ? tok.accent : tok.rule, 0);
        lv_obj_set_style_border_width(paste_ta_, on ? 2 : 1, 0);
        lv_obj_set_style_bg_color(paste_ta_, on ? tok.surface_sel : tok.surface, 0);
    }
    if (mode_label_) {
        lv_obj_set_style_text_color(mode_label_, focus_ < 0 ? tok.accent : tok.ink_dim, 0);
    }
}

void EngCalcScreen::cycle_mode(int delta) {
    const auto& sch = tools::engcalc::schema_for(op_);
    if (!sch.modes || sch.mode_count <= 0) return;
    sync_values_from_ui();
    mode_index_ = (mode_index_ + delta + sch.mode_count) % sch.mode_count;
    const int n = sch.modes[mode_index_].field_count;
    values_.assign(static_cast<size_t>(n), "");
    rebuild_fields();
    focus_field(0);
}

void EngCalcScreen::sync_values_from_ui() {
    const auto& sch = tools::engcalc::schema_for(op_);
    if (sch.kind == tools::engcalc::FormKind::Paste && paste_ta_) {
        const char* t = lv_textarea_get_text(paste_ta_);
        values_.assign(1, t ? t : "");
        return;
    }
    values_.resize(field_tas_.size());
    for (size_t i = 0; i < field_tas_.size(); ++i) {
        const char* t = lv_textarea_get_text(field_tas_[i]);
        values_[i] = t ? t : "";
    }
}

void EngCalcScreen::apply_line(const std::string& line) {
    tools::engcalc::parse_line_into(op_, line, mode_index_, values_);
    const auto& sch = tools::engcalc::schema_for(op_);
    if (sch.kind == tools::engcalc::FormKind::Paste && paste_ta_) {
        lv_textarea_set_text(paste_ta_, values_.empty() ? "" : values_[0].c_str());
        if (mode_label_) rebuild_fields();
    } else {
        rebuild_fields();
    }
    focus_field(0);
}

lv_obj_t* EngCalcScreen::active_ta() const {
    if (paste_ta_ && focus_ == 0) return paste_ta_;
    if (focus_ >= 0 && focus_ < static_cast<int>(field_tas_.size())) {
        return field_tas_[static_cast<size_t>(focus_)];
    }
    return nullptr;
}

tools::engcalc::AutoFormat EngCalcScreen::active_format() const {
    if (focus_ < 0) return tools::engcalc::AutoFormat::None;
    const auto& sch = tools::engcalc::schema_for(op_);
    if (sch.kind == tools::engcalc::FormKind::Paste) return tools::engcalc::AutoFormat::None;
    int n = 0;
    const tools::engcalc::FieldDef* fields = tools::engcalc::active_fields(op_, mode_index_, n);
    if (sch.kind == tools::engcalc::FormKind::List) {
        return sch.fields && sch.field_count > 0 ? sch.fields[0].format : tools::engcalc::AutoFormat::None;
    }
    if (!fields || focus_ >= n) return tools::engcalc::AutoFormat::None;
    return fields[focus_].format;
}

void EngCalcScreen::reformat_active_field() {
    lv_obj_t* ta = active_ta();
    if (!ta) return;
    const auto fmt = active_format();
    if (fmt == tools::engcalc::AutoFormat::None) return;
    const char* text = lv_textarea_get_text(ta);
    std::string raw = text ? text : "";
    int caret = 0;
    const std::string formatted = tools::engcalc::apply_auto_format(fmt, raw, caret);
    if (formatted == raw) return;
    lv_textarea_set_text(ta, formatted.c_str());
    lv_textarea_set_cursor_pos(ta, static_cast<uint32_t>(caret));
}

void EngCalcScreen::insert_char(char ch) {
    result_focus_ = false;
    lv_obj_t* ta = active_ta();
    if (!ta) return;
    lv_textarea_add_char(ta, static_cast<uint32_t>(static_cast<unsigned char>(ch)));
    reformat_active_field();
}

bool EngCalcScreen::type_key(uint32_t key) {
    lv_obj_t* ta = active_ta();
    if (!ta) return false;
    if (key == LV_KEY_BACKSPACE || key == 8) {
        result_focus_ = false;
        lv_textarea_delete_char(ta);
        reformat_active_field();
        return true;
    }
    if (key == LV_KEY_DEL || key == 127) {
        result_focus_ = false;
        lv_textarea_delete_char_forward(ta);
        reformat_active_field();
        return true;
    }
    if (!result_focus_ && !keys_.focused()) {
        if (key == LV_KEY_LEFT) {
            lv_textarea_cursor_left(ta);
            return true;
        }
        if (key == LV_KEY_RIGHT) {
            lv_textarea_cursor_right(ta);
            return true;
        }
    }
    if (key >= 32 && key <= 126) {
        result_focus_ = false;
        // Skip separators the formatter always inserts; keep '.' for short IP/geo octets.
        const auto fmt = active_format();
        if (fmt == tools::engcalc::AutoFormat::Mac && (key == ':' || key == '.')) return true;
        if (fmt == tools::engcalc::AutoFormat::IsoDate && key == '-') return true;
        if (fmt == tools::engcalc::AutoFormat::IsoTime && key == ':') return true;
        lv_textarea_add_char(ta, key);
        reformat_active_field();
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
    if (bottom <= 2) lv_label_set_text(scroll_hint_, "^ X/F");
    else if (scroll_y <= 2) lv_label_set_text(scroll_hint_, "v X/F");
    else lv_label_set_text(scroll_hint_, "^v X/F");
}

void EngCalcScreen::run_now() {
    sync_values_from_ui();
    std::string input = tools::engcalc::build_line(op_, mode_index_, values_);
    bool all_blank = true;
    for (const auto& v : values_) {
        if (!v.empty() && v != "-") {
            all_blank = false;
            break;
        }
    }
    if (input.empty() || all_blank) {
        lv_label_set_text(output_label_, "fill a field | Tab help");
        result_focus_ = false;
        return;
    }
    view_model().set_work_input(input);
    bool ok = true;
    std::string out = tools::engcalc::run(op_, input, ok);
    if (!ok && out.empty()) out = "error";
    view_model().set_work_output(out);
    lv_label_set_text(output_label_, out.c_str());
    lv_obj_set_width(output_label_, view::kScreenWidth - 12);
    lv_obj_set_height(output_label_, LV_SIZE_CONTENT);
    if (output_box_) lv_obj_scroll_to_y(output_box_, 0, LV_ANIM_OFF);
    result_focus_ = true;
    keys_.set_focused(false);
    update_scroll_hint();

    persist_.last_line = input;
    tools::calcpersist::save_op(op_, persist_);
}

void EngCalcScreen::open_help() {
    favorites_.hide();
    if (help_.visible()) {
        help_.hide();
        return;
    }
    help_.show(tools::engcalc::op_label(op_), tools::engcalc::help_text(op_));
}

void EngCalcScreen::open_favorites() {
    help_.hide();
    if (favorites_.visible()) {
        favorites_.hide();
        return;
    }
    std::string body;
    if (persist_.favorites.empty()) {
        body = "No favorites yet.\n"
               "Run a calculation, then tap the list icon to save.\n"
               "Tap again and press 1-8 to recall.";
    } else {
        body = "Tap number to recall · Sym+S saves current\n";
        for (size_t i = 0; i < persist_.favorites.size() && i < 8; ++i) {
            body += std::to_string(i + 1);
            body += ") ";
            body += persist_.favorites[i].name;
            body += "\n";
        }
    }
    favorites_.show("Favorites", body);
}

void EngCalcScreen::add_current_favorite() {
    sync_values_from_ui();
    const std::string line = tools::engcalc::build_line(op_, mode_index_, values_);
    tools::calcpersist::add_favorite(persist_, line);
    tools::calcpersist::save_op(op_, persist_);
    open_favorites();
}

uint32_t EngCalcScreen::map_nav_letter(uint32_t key) {
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

const char* EngCalcScreen::typing_hint() const {
    return "Tab help | Enter run | F/X fields | Esc";
}

std::array<view::widgets::ActionSlot, 5> EngCalcScreen::nav_actions() {
    return {};
}

std::array<view::widgets::HintIcon, 2> EngCalcScreen::hint_icons() {
    return {{
        {view::ICON_INFO, [this]() { open_help(); }},
        {view::ICON_PDA_TODO, [this]() { open_favorites(); }},
    }};
}

bool EngCalcScreen::handle_key(uint32_t key) {
    if (help_.visible()) return help_.handle_key(key);
    if (favorites_.visible()) {
        if (key == LV_KEY_ESC || key == platform::kKeyTab) {
            favorites_.hide();
            return true;
        }
        if (key >= '1' && key <= '8') {
            const size_t i = static_cast<size_t>(key - '1');
            if (i < persist_.favorites.size()) {
                apply_line(persist_.favorites[i].line);
                favorites_.hide();
                run_now();
            }
            return true;
        }
        if (key == 's' || key == 'S') {
            add_current_favorite();
            return true;
        }
        return favorites_.handle_key(key);
    }

    if (key == platform::kKeyTab) {
        open_help();
        return true;
    }

    // One Esc always leaves the calculator (help already handled above).
    if (key == LV_KEY_ESC) {
        keys_.set_focused(false);
        result_focus_ = false;
        view_model().go_back();
        return true;
    }

    const bool editing_value = !keys_.focused() && !result_focus_ && focus_ >= 0;
    if (editing_value) {
        if (key == 'f' || key == 'F') {
            const auto& sch = tools::engcalc::schema_for(op_);
            const bool has_mode = sch.modes && sch.mode_count > 0;
            focus_field(std::max(has_mode ? -1 : 0, focus_ - 1));
            return true;
        }
        if (key == 'x' || key == 'X') {
            const int n = paste_ta_ ? 1 : static_cast<int>(field_tas_.size());
            if (focus_ + 1 < n) focus_field(focus_ + 1);
            else if (output_box_ && lv_obj_get_scroll_bottom(output_box_) > 2) {
                scroll_result(kLineScroll);
            }
            return true;
        }
    } else {
        key = map_nav_letter(key);
    }

    const auto& sch = tools::engcalc::schema_for(op_);

    if (result_focus_ && output_box_ && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
        if (key == LV_KEY_UP && lv_obj_get_scroll_y(output_box_) <= 0) {
            result_focus_ = false;
            keys_.set_focused(true);
            return true;
        }
        scroll_result(key == LV_KEY_DOWN ? kLineScroll : -kLineScroll);
        return true;
    }

    if (!keys_.focused() && !result_focus_) {
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            if (sch.modes && sch.mode_count > 0 && focus_ < 0) {
                cycle_mode(key == LV_KEY_RIGHT ? 1 : -1);
                return true;
            }
        }
        if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            const int n = sch.kind == tools::engcalc::FormKind::Paste
                              ? 1
                              : static_cast<int>(field_tas_.size());
            const bool has_mode = sch.modes && sch.mode_count > 0;
            int max_f = n - 1;
            int min_f = has_mode ? -1 : 0;
            if (key == LV_KEY_UP) {
                if (focus_ > min_f) focus_field(focus_ - 1);
                else if (has_mode) focus_field(-1);
                else keys_.set_focused(true);
            } else {
                if (focus_ < max_f) focus_field(focus_ + 1);
                else if (output_box_ && lv_obj_get_scroll_bottom(output_box_) > 2) {
                    scroll_result(kLineScroll);
                }
            }
            return true;
        }
        if (sch.kind == tools::engcalc::FormKind::List) {
            if (key == '+' || key == '=') {
                sync_values_from_ui();
                if (static_cast<int>(values_.size()) < sch.list_max) {
                    values_.emplace_back();
                    rebuild_fields();
                    focus_field(static_cast<int>(values_.size()) - 1);
                }
                return true;
            }
            if ((key == '-' || key == '_') && focus_ >= 0) {
                // Only remove a row when the active field is empty; otherwise type '-'.
                sync_values_from_ui();
                if (focus_ < static_cast<int>(values_.size()) && values_[static_cast<size_t>(focus_)].empty() &&
                    static_cast<int>(values_.size()) > sch.list_min) {
                    values_.pop_back();
                    rebuild_fields();
                    focus_field(static_cast<int>(values_.size()) - 1);
                    return true;
                }
            }
        }
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

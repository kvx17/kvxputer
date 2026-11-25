#include "calc_key_bar.h"

#include "theme.h"

namespace screen {
namespace {

constexpr int kChipH = 16;
constexpr int kChipMinW = 22;

} // namespace

void CalcKeyBar::build(lv_obj_t* parent,
                       const view::KvxTokens& tokens,
                       const lv_font_t* font,
                       int y,
                       int width,
                       const char* glyphs,
                       InsertFn on_insert,
                       const char* mode_label,
                       ModeFn on_mode) {
    tokens_ = tokens;
    on_insert_ = std::move(on_insert);
    on_mode_ = std::move(on_mode);
    has_mode_ = mode_label != nullptr && mode_label[0] != '\0';
    count_ = 0;
    if (glyphs) {
        for (const char* p = glyphs; *p && count_ < kMaxKeys; ++p) {
            glyphs_[count_++] = *p;
        }
    }

    row_ = lv_obj_create(parent);
    lv_obj_remove_style_all(row_);
    lv_obj_set_size(row_, width, kChipH + 2);
    lv_obj_set_pos(row_, 4, y);
    lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_, 3, 0);
    lv_obj_clear_flag(row_, LV_OBJ_FLAG_SCROLLABLE);

    auto make_chip = [&](const char* text, void* user) -> lv_obj_t* {
        auto* btn = lv_obj_create(row_);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, kChipH);
        lv_obj_set_style_min_width(btn, kChipMinW, 0);
        lv_obj_set_style_pad_hor(btn, 5, 0);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(btn, user);

        auto* lab = lv_label_create(btn);
        lv_label_set_text(lab, text);
        lv_obj_set_style_text_font(lab, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_center(lab);

        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* self = static_cast<CalcKeyBar*>(lv_event_get_user_data(e));
                auto* tgt = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                if (!self || !tgt) return;
                const auto idx = reinterpret_cast<intptr_t>(lv_obj_get_user_data(tgt));
                self->selected_ = static_cast<int>(idx);
                self->activate_selected();
                self->refresh_styles();
            },
            LV_EVENT_CLICKED,
            this);
        return btn;
    };

    int index = 0;
    if (has_mode_) {
        mode_btn_ = make_chip(mode_label, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        mode_label_ = lv_obj_get_child(mode_btn_, 0);
        ++index;
    }

    for (size_t i = 0; i < count_; ++i) {
        char label[2] = {glyphs_[i], '\0'};
        if (glyphs_[i] == ' ') label[0] = '_'; // visible stand-in for space
        btns_[i] = make_chip(label, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        ++index;
    }

    selected_ = 0;
    focused_ = false;
    refresh_styles();
}

void CalcKeyBar::set_mode_label(const char* label) {
    if (mode_label_ && label) {
        lv_label_set_text(mode_label_, label);
    }
}

void CalcKeyBar::set_focused(bool focused) {
    focused_ = focused;
    refresh_styles();
}

void CalcKeyBar::set_y(int y) {
    if (row_) lv_obj_set_y(row_, y);
}

void CalcKeyBar::refresh_styles() {
    const int total = static_cast<int>(count_) + (has_mode_ ? 1 : 0);
    auto style_one = [&](lv_obj_t* btn, lv_obj_t* lab, int idx) {
        if (!btn) return;
        const bool sel = focused_ && idx == selected_;
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, sel ? tokens_.accent : tokens_.surface, 0);
        lv_obj_set_style_border_color(btn, sel ? tokens_.accent : tokens_.rule, 0);
        if (lab) {
            lv_obj_set_style_text_color(lab, sel ? tokens_.bg : tokens_.ink, 0);
        }
    };

    int idx = 0;
    if (has_mode_) {
        style_one(mode_btn_, mode_label_, idx++);
    }
    for (size_t i = 0; i < count_; ++i) {
        auto* lab = btns_[i] ? lv_obj_get_child(btns_[i], 0) : nullptr;
        style_one(btns_[i], lab, idx++);
    }
    (void)total;
}

void CalcKeyBar::activate_selected() {
    if (has_mode_ && selected_ == 0) {
        if (on_mode_) on_mode_();
        return;
    }
    const int op_index = selected_ - (has_mode_ ? 1 : 0);
    if (op_index < 0 || static_cast<size_t>(op_index) >= count_) return;
    if (on_insert_) on_insert_(glyphs_[static_cast<size_t>(op_index)]);
}

bool CalcKeyBar::handle_key(uint32_t key) {
    const int total = static_cast<int>(count_) + (has_mode_ ? 1 : 0);
    if (total <= 0) return false;

    if (key == LV_KEY_UP) {
        set_focused(true);
        return true;
    }
    if (!focused_) return false;

    if (key == LV_KEY_DOWN || key == LV_KEY_ESC) {
        set_focused(false);
        return key == LV_KEY_DOWN; // Esc still bubbles for leave
    }
    // Left/Right only — screens that want bare Z/C remap those to arrows first.
    if (key == LV_KEY_LEFT) {
        selected_ = (selected_ - 1 + total) % total;
        refresh_styles();
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        selected_ = (selected_ + 1) % total;
        refresh_styles();
        return true;
    }
    if (key == LV_KEY_ENTER) {
        activate_selected();
        return true;
    }
    return false;
}

} // namespace screen

#include "instrument.h"

#include "theme.h"
#include "ui_const.h"

namespace view::widgets {

lv_obj_t* make_status_chip(lv_obj_t* body, const char* text, view::StatusPillKind kind,
                           const lv_font_t* font, const view::KvxTokens& tokens) {
    lv_obj_t* pill = view::make_status_pill(body, text, kind, font, tokens);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -4, 22);
    return pill;
}

lv_obj_t* make_hint_bar(lv_obj_t* body, const char* text, const lv_font_t* font,
                        const view::KvxTokens& tokens) {
    lv_obj_t* hint = lv_label_create(body);
    lv_obj_set_style_text_font(hint, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, tokens.ink_dim, 0);
    lv_label_set_text(hint, text ? text : "");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    return hint;
}

lv_obj_t* make_readout(lv_obj_t* parent, const lv_font_t* big_font, const view::KvxTokens& tokens) {
    lv_obj_t* r = lv_label_create(parent);
    lv_obj_set_style_text_font(r, big_font ? big_font : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(r, tokens.accent, 0);
    lv_label_set_text(r, "");
    return r;
}

lv_obj_t* make_caption(lv_obj_t* parent, const lv_font_t* font, const view::KvxTokens& tokens) {
    lv_obj_t* c = lv_label_create(parent);
    lv_obj_set_style_text_font(c, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(c, tokens.ink_dim, 0);
    lv_label_set_text(c, "");
    return c;
}

// --- SectionTabs -------------------------------------------------------------

namespace {
void underline_x_cb(void* var, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t*>(var), v); }
}

void SectionTabs::build(lv_obj_t* body, int y, const std::vector<std::string>& names,
                        const lv_font_t* font, const view::KvxTokens& tokens) {
    tokens_ = tokens;
    labels_.clear();
    const int n = static_cast<int>(names.size());
    if (n <= 0) return;
    slot_w_ = view::kScreenWidth / n;
    underline_w_ = slot_w_ * 6 / 10;

    for (int i = 0; i < n; ++i) {
        lv_obj_t* lbl = lv_label_create(body);
        lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
        lv_label_set_text(lbl, names[i].c_str());
        lv_obj_set_width(lbl, slot_w_);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(lbl, i * slot_w_, y);
        labels_.push_back(lbl);
    }

    underline_ = lv_obj_create(body);
    lv_obj_remove_style_all(underline_);
    lv_obj_set_size(underline_, underline_w_, 2);
    lv_obj_set_style_radius(underline_, 1, 0);
    lv_obj_set_style_bg_color(underline_, tokens_.accent, 0);
    lv_obj_set_style_bg_opa(underline_, LV_OPA_COVER, 0);
    lv_obj_set_y(underline_, y + 16);

    set_active(0, false);
}

void SectionTabs::set_active(int idx, bool animate) {
    if (idx < 0 || idx >= count()) return;
    active_ = idx;
    for (int i = 0; i < count(); ++i) {
        lv_obj_set_style_text_color(labels_[i], i == idx ? tokens_.accent : tokens_.ink_dim, 0);
    }
    if (!underline_) return;
    const int target_x = idx * slot_w_ + (slot_w_ - underline_w_) / 2;
    if (!animate) {
        lv_obj_set_x(underline_, target_x);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, underline_);
    lv_anim_set_exec_cb(&a, underline_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(underline_), target_x);
    lv_anim_set_time(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

} // namespace view::widgets

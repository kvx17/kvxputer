#include "calc_help_modal.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"

namespace view::widgets {

CalcHelpModal::~CalcHelpModal() {
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void CalcHelpModal::build(lv_obj_t* parent, app::AssetManager& assets, const view::KvxTokens& tokens) {
    if (root_ || !parent) return;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_50, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root_, backdrop_clicked, LV_EVENT_CLICKED, this);

    panel_ = lv_obj_create(root_);
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, 292, 146);
    lv_obj_center(panel_);
    lv_obj_set_style_radius(panel_, 6, 0);
    lv_obj_set_style_bg_color(panel_, tokens.surface, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);
    lv_obj_set_style_border_color(panel_, tokens.rule, 0);
    lv_obj_set_style_shadow_width(panel_, 14, 0);
    lv_obj_set_style_shadow_opa(panel_, LV_OPA_20, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);

    title_ = lv_label_create(panel_);
    auto* title_font = assets.load_standard_font(14);
    lv_obj_set_style_text_font(title_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_, tokens.ink, 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 10, 6);
    lv_obj_set_width(title_, 270);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);

    body_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, 272, 100);
    lv_obj_align(body_, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);

    auto* text = lv_label_create(body_);
    auto* body_font = assets.load_standard_font(11);
    lv_obj_set_style_text_font(text, body_font ? body_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(text, tokens.ink, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, 264);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_user_data(body_, text);

    footer_ = lv_label_create(panel_);
    lv_label_set_text(footer_, "Tab/Esc close · F/X scroll");
    auto* footer_font = assets.load_standard_font(10);
    lv_obj_set_style_text_font(footer_, footer_font ? footer_font : &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(footer_, tokens.ink_dim, 0);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 10, -4);

    hide();
}

void CalcHelpModal::show(const char* title, const std::string& body) {
    if (!root_) return;
    lv_label_set_text(title_, title ? title : "Help");
    auto* text = static_cast<lv_obj_t*>(lv_obj_get_user_data(body_));
    if (text) {
        lv_label_set_text(text, body.c_str());
        lv_obj_set_height(text, LV_SIZE_CONTENT);
        lv_obj_scroll_to_y(body_, 0, LV_ANIM_OFF);
    }
    lv_obj_move_foreground(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void CalcHelpModal::hide() {
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool CalcHelpModal::visible() const {
    return root_ && lv_obj_is_valid(root_) && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

bool CalcHelpModal::handle_key(uint32_t key) {
    if (!visible()) return false;
    if (key == LV_KEY_ESC || key == platform::kKeyTab) {
        hide();
        return true;
    }
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        if (body_) lv_obj_scroll_by(body_, 0, 18, LV_ANIM_OFF);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        if (body_) lv_obj_scroll_by(body_, 0, -18, LV_ANIM_OFF);
        return true;
    }
    return true; // consume everything else while open
}

void CalcHelpModal::backdrop_clicked(lv_event_t* event) {
    auto* self = static_cast<CalcHelpModal*>(lv_event_get_user_data(event));
    if (self && lv_event_get_target(event) == self->root_) {
        self->hide();
    }
}

} // namespace view::widgets

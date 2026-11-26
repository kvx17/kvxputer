#include "flashlight_screen.h"

#include "asset_manager.h"
#include "backlight.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

struct LampColor {
    const char* name;
    uint8_t r, g, b;
};
constexpr LampColor kColors[] = {
    {"WHITE", 255, 255, 255},
    {"AMBER", 255, 176, 40},
    {"RED", 255, 40, 40},
};
constexpr int kColorCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));

constexpr int kSwatch = 20;
constexpr int kSwatchGap = 14;

} // namespace

FlashlightScreen::FlashlightScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    int32_t cur = -1;
    if (platform::read_brightness_percent(cur)) {
        prev_brightness_ = cur;
        brightness_saved_ = true;
    }
    init();
}

FlashlightScreen::~FlashlightScreen() {
    if (hide_timer_) lv_timer_delete(hide_timer_);
    if (brightness_saved_) {
        (void)platform::write_brightness_percent(prev_brightness_);
    }
}

void FlashlightScreen::build() {
    auto* root = content();

    lamp_ = lv_obj_create(root);
    lv_obj_remove_flag(lamp_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(lamp_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(lamp_, 0, 0);
    lv_obj_set_style_border_width(lamp_, 0, 0);
    lv_obj_set_style_radius(lamp_, 0, 0);
    lv_obj_set_style_pad_all(lamp_, 0, 0);

    name_ = lv_label_create(lamp_);
    auto* big = assets().load_standard_font(40);
    lv_obj_set_style_text_font(name_, big ? big : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(name_, lv_color_make(20, 20, 20), 0);
    lv_obj_align(name_, LV_ALIGN_CENTER, 0, -16);

    // Three swatch dots, centered as a row below the name.
    const int total = kColorCount * kSwatch + (kColorCount - 1) * kSwatchGap;
    const int start_x = (view::kScreenWidth - total) / 2;
    for (int i = 0; i < kColorCount; ++i) {
        swatch_[i] = lv_obj_create(lamp_);
        lv_obj_remove_flag(swatch_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(swatch_[i], kSwatch, kSwatch);
        lv_obj_set_style_radius(swatch_[i], kSwatch / 2, 0);
        lv_obj_set_style_bg_color(swatch_[i], lv_color_make(kColors[i].r, kColors[i].g, kColors[i].b), 0);
        lv_obj_set_style_bg_opa(swatch_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(swatch_[i], lv_color_make(20, 20, 20), 0);
        lv_obj_align(swatch_[i], LV_ALIGN_CENTER,
                     start_x + i * (kSwatch + kSwatchGap) + kSwatch / 2 - view::kScreenWidth / 2, 28);
    }

    hint_ = lv_label_create(lamp_);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(hint_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_, lv_color_make(40, 40, 40), 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(hint_, "F/X change color   Esc exit");

    apply_color();
    (void)platform::write_brightness_percent(100);

    // Fade the chrome away after a few seconds so the panel is a pure lamp.
    hide_timer_ = lv_timer_create(hide_cb, 3000, this);
}

void FlashlightScreen::hide_cb(lv_timer_t* t) {
    auto* self = static_cast<FlashlightScreen*>(lv_timer_get_user_data(t));
    if (self) self->hide_ui();
}

void FlashlightScreen::show_ui() {
    if (name_) lv_obj_remove_flag(name_, LV_OBJ_FLAG_HIDDEN);
    if (hint_) lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    for (auto* s : swatch_)
        if (s) lv_obj_remove_flag(s, LV_OBJ_FLAG_HIDDEN);
    if (hide_timer_) {
        lv_timer_reset(hide_timer_);
        lv_timer_resume(hide_timer_);
    }
}

void FlashlightScreen::hide_ui() {
    if (name_) lv_obj_add_flag(name_, LV_OBJ_FLAG_HIDDEN);
    if (hint_) lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    for (auto* s : swatch_)
        if (s) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    if (hide_timer_) lv_timer_pause(hide_timer_);
}

void FlashlightScreen::apply_color() {
    if (!lamp_) return;
    const auto& c = kColors[color_];
    lv_obj_set_style_bg_color(lamp_, lv_color_make(c.r, c.g, c.b), 0);
    lv_obj_set_style_bg_opa(lamp_, LV_OPA_COVER, 0);
    if (name_) lv_label_set_text(name_, c.name);
    for (int i = 0; i < kColorCount; ++i) {
        if (!swatch_[i]) continue;
        lv_obj_set_style_border_width(swatch_[i], i == color_ ? 3 : 0, 0);
    }
}

void FlashlightScreen::cycle_color(int delta) {
    color_ = (color_ + delta + kColorCount) % kColorCount;
    apply_color();
    show_ui();  // reveal chrome again and restart the fade-out countdown
}

bool FlashlightScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        return KvxScreen::handle_key(key);
    }
    if (key == LV_KEY_LEFT || key == 'f' || key == 'F') {
        cycle_color(-1);
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'x' || key == 'X' || key == LV_KEY_ENTER || key == ' ') {
        cycle_color(1);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> FlashlightScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() { cycle_color(-1); }},
        {view::ICON_CHECK, [this]() { cycle_color(1); }},
        {view::ICON_ARROW_RIGHT, [this]() { cycle_color(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

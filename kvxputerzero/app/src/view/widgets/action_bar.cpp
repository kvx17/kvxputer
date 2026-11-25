#include "action_bar.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"

#include <cstdint>

namespace view::widgets {
namespace {

constexpr std::array<const char*, 5> kKeyLabels = {"4", "5", "6", "7", "8"};

} // namespace

ActionBar::ActionBar(lv_obj_t* parent,
                     app::AssetManager& assets,
                     const view::KvxTokens& tokens,
                     std::array<ActionSlot, 5> slots,
                     const char* hint,
                     std::array<HintIcon, 2> hint_icons) {
    bool any_action = false;
    for (const auto& slot : slots) {
        if (slot.on_press) {
            any_action = true;
            break;
        }
    }

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kActionBarHeight);
    lv_obj_align(root_, LV_ALIGN_BOTTOM_MID, 0, 0);
    view::style_bar(root_, tokens);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* rule = lv_obj_create(root_);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, view::kScreenWidth, 1);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(rule, tokens.accent, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    if (!any_action) {
        auto* label = lv_label_create(root_);
        lv_label_set_text(label, hint && hint[0] ? hint : "Fn + key types");
        auto* font = assets.load_standard_font(11);
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, tokens.ink, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_set_width(label, view::kScreenWidth - 56);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);

        const lv_font_t* icon_font = assets.load_font("Phosphor-Fill.ttf", 14);
        int x = view::kScreenWidth - 6;
        for (int i = 1; i >= 0; --i) {
            if (!hint_icons[static_cast<size_t>(i)].on_press) continue;
            hint_actions_[static_cast<size_t>(i)] = std::move(hint_icons[static_cast<size_t>(i)].on_press);
            auto* btn = lv_obj_create(root_);
            lv_obj_remove_style_all(btn);
            lv_obj_set_size(btn, 22, view::kActionBarHeight - 2);
            x -= 24;
            lv_obj_set_pos(btn, x, 1);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(btn, on_hint_icon_click, LV_EVENT_CLICKED, this);
            auto* icon = lv_label_create(btn);
            const char* glyph = hint_icons[static_cast<size_t>(i)].icon;
            lv_label_set_text(icon, glyph && glyph[0] ? glyph : "?");
            lv_obj_set_style_text_font(icon, icon_font ? icon_font : &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(icon, tokens.accent, 0);
            lv_obj_center(icon);
        }
        return;
    }

    auto* row = lv_obj_create(root_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, view::kScreenWidth, view::kActionBarHeight - 1);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t* icon_font = assets.load_font("Phosphor-Fill.ttf", 14);
    const lv_font_t* key_font = assets.load_standard_font(10);
    if (!key_font) {
        key_font = &lv_font_montserrat_12;
    }

    for (size_t i = 0; i < slots.size(); ++i) {
        auto* button = lv_obj_create(row);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 54, view::kActionBarHeight - 2);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(button, 2, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(i));
        buttons_[i] = button;

        auto* key = lv_label_create(button);
        lv_label_set_text(key, kKeyLabels[i]);
        lv_obj_set_style_text_font(key, key_font, 0);
        lv_obj_set_style_text_color(key, tokens.accent, 0);

        if (slots[i].icon && slots[i].icon[0] != '\0') {
            auto* icon = lv_label_create(button);
            lv_label_set_text(icon, slots[i].icon);
            lv_obj_set_style_text_font(icon, icon_font ? icon_font : &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(icon, tokens.ink, 0);
        }

        if (slots[i].on_press) {
            actions_[i] = std::move(slots[i].on_press);
            lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, this);
            platform::register_nav_button(i, button);
        } else {
            lv_obj_set_style_opa(button, LV_OPA_40, 0);
        }
    }
}

ActionBar::~ActionBar() {
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i]) {
            platform::unregister_nav_button(i, buttons_[i]);
        }
    }
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void ActionBar::on_click(lv_event_t* event) {
    auto* bar = static_cast<ActionBar*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!bar || !target) {
        return;
    }
    const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (index < bar->actions_.size() && bar->actions_[index]) {
        bar->actions_[index]();
    }
}

void ActionBar::on_hint_icon_click(lv_event_t* event) {
    auto* bar = static_cast<ActionBar*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!bar || !target) return;
    const auto index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (index >= 0 && static_cast<size_t>(index) < bar->hint_actions_.size() &&
        bar->hint_actions_[static_cast<size_t>(index)]) {
        bar->hint_actions_[static_cast<size_t>(index)]();
    }
}

} // namespace view::widgets

#include "kvx_style.h"

#include "theme.h"
#include "ui_const.h"

namespace view {
namespace {

int luminance(lv_color_t c) {
    // Approximate Rec. 601 luma on 8-bit channels.
    return (static_cast<int>(c.red) * 299 + static_cast<int>(c.green) * 587 +
            static_cast<int>(c.blue) * 114) /
           1000;
}

lv_obj_t* row_child_by_role(lv_obj_t* row, uintptr_t role) {
    const uint32_t n = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < n; ++i) {
        auto* child = lv_obj_get_child(row, i);
        if (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(child)) == role) {
            return child;
        }
    }
    return nullptr;
}

constexpr uintptr_t kRoleIcon   = 1;
constexpr uintptr_t kRoleSwatch = 2;
constexpr uintptr_t kRoleLabel  = 3;

} // namespace

KvxTokens make_tokens(uint32_t main_rgb, uint32_t accent_rgb, uint32_t bg_rgb) {
    KvxTokens t{};
    t.main = lv_color_hex(main_rgb);
    t.accent = lv_color_hex(accent_rgb);
    t.bg = lv_color_hex(bg_rgb);
    t.light_bg = luminance(t.bg) > 140;

    if (t.light_bg) {
        t.ink = lv_color_mix(t.main, lv_color_hex(0x000000), 70);
        t.icon_idle = t.main;
    } else {
        t.ink = lv_color_hex(0xF0F0F0);
        t.icon_idle = lv_color_mix(lv_color_hex(0xFFFFFF), t.main, 150);
    }

    t.ink_dim = lv_color_mix(t.ink, t.bg, 150);
    t.bar = lv_color_mix(t.main, t.bg, 26);
    t.surface = lv_color_mix(t.main, t.bg, 44);
    t.surface_sel = lv_color_mix(t.main, t.bg, 130);
    t.rule = lv_color_mix(t.main, t.bg, 90);

    const int accent_delta = luminance(t.accent) - luminance(t.surface_sel);
    t.icon_sel = (accent_delta > 70 || accent_delta < -70) ? t.accent : t.ink;
    return t;
}

void style_bar(lv_obj_t* obj, const KvxTokens& tokens) {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, tokens.bar, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void style_tile(lv_obj_t* obj, int radius) {
    if (!obj) return;
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void apply_tile_state(lv_obj_t* tile,
                      lv_obj_t* icon,
                      lv_obj_t* label,
                      lv_obj_t* badge,
                      const TileState& state,
                      const KvxTokens& tokens) {
    if (!tile) return;

    const bool selected = state.selected;
    lv_obj_set_style_bg_color(tile, selected ? tokens.surface_sel : tokens.surface, 0);
    if (selected) {
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, tokens.accent, 0);
    } else {
        lv_obj_set_style_border_width(tile, 0, 0);
    }

    const lv_opa_t content_opa =
        (state.kind == TileKind::ComingSoon)
            ? (selected ? static_cast<lv_opa_t>(LV_OPA_70) : static_cast<lv_opa_t>(LV_OPA_40 + 5))
            : LV_OPA_COVER;

    if (icon) {
        lv_obj_set_style_text_color(icon, selected ? tokens.icon_sel : tokens.icon_idle, 0);
        lv_obj_set_style_opa(icon, content_opa, 0);
    }
    if (label) {
        lv_obj_set_style_text_color(label, selected ? tokens.ink : tokens.ink_dim, 0);
        lv_obj_set_style_opa(label, content_opa, 0);
    }
    if (badge) {
        if (state.kind == TileKind::ComingSoon) {
            lv_label_set_text(badge, ICON_BADGE_SOON);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(badge, tokens.ink_dim, 0);
            lv_obj_set_style_opa(badge, LV_OPA_COVER, 0);
        } else if (state.kind == TileKind::UseStock) {
            lv_label_set_text(badge, ICON_BADGE_STOCK);
            lv_obj_clear_flag(badge, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(badge, tokens.ink_dim, 0);
            lv_obj_set_style_opa(badge, LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_obj_t* make_list(lv_obj_t* parent, int top) {
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kScreenWidth, kContentHeight - top);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_pad_top(list, 4, 0);
    lv_obj_set_style_pad_bottom(list, 4, 0);
    lv_obj_set_style_pad_left(list, 6, 0);
    lv_obj_set_style_pad_right(list, 6, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

lv_obj_t* make_list_row(lv_obj_t* list,
                        const char* text,
                        const char* icon,
                        const lv_font_t* font,
                        const lv_font_t* icon_font,
                        const KvxTokens& tokens,
                        uint32_t swatch_rgb) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kScreenWidth - 12, 22);
    lv_obj_set_style_radius(row, 5, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (icon && icon[0] != '\0') {
        auto* icon_label = lv_label_create(row);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_user_data(icon_label, reinterpret_cast<void*>(kRoleIcon));
        if (icon_font) {
            lv_obj_set_style_text_font(icon_label, icon_font, 0);
        }
        lv_obj_set_style_text_color(icon_label, tokens.icon_idle, 0);
    }

    if (swatch_rgb != 0) {
        auto* swatch = lv_label_create(row);
        lv_label_set_text(swatch, ICON_DOT);
        lv_obj_set_user_data(swatch, reinterpret_cast<void*>(kRoleSwatch));
        if (icon_font) {
            lv_obj_set_style_text_font(swatch, icon_font, 0);
        }
        lv_obj_set_style_text_color(swatch, lv_color_hex(swatch_rgb), 0);
    }

    auto* label = lv_label_create(row);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_user_data(label, reinterpret_cast<void*>(kRoleLabel));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(label, 1);
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    } else {
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    }
    lv_obj_set_style_text_color(label, tokens.ink_dim, 0);

    apply_row_state(row, false, tokens);
    return row;
}

void apply_row_state(lv_obj_t* row, bool selected, const KvxTokens& tokens) {
    if (!row) return;
    lv_obj_set_style_bg_color(row, selected ? tokens.surface_sel : tokens.surface, 0);
    if (selected) {
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, tokens.accent, 0);
    } else {
        lv_obj_set_style_border_width(row, 0, 0);
    }

    if (auto* icon = row_child_by_role(row, kRoleIcon)) {
        lv_obj_set_style_text_color(icon, selected ? tokens.icon_sel : tokens.icon_idle, 0);
    }
    if (auto* label = row_child_by_role(row, kRoleLabel)) {
        lv_obj_set_style_text_color(label, selected ? tokens.ink : tokens.ink_dim, 0);
    }
}

void apply_log_row_state(lv_obj_t* row, bool selected, const KvxTokens& tokens) {
    if (!row) return;
    // Log cursor: thin left rule + brighter ink — not a full menu pill bar.
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    if (selected) {
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, tokens.accent, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 6, 0);
    } else {
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
    }
    if (auto* label = row_child_by_role(row, kRoleLabel)) {
        lv_obj_set_style_text_color(label, selected ? tokens.ink : tokens.ink_dim, 0);
    } else if (lv_obj_get_child_count(row) > 0) {
        if (auto* child = lv_obj_get_child(row, 0)) {
            lv_obj_set_style_text_color(child, selected ? tokens.ink : tokens.ink_dim, 0);
        }
    }
}

lv_obj_t* make_log_row(lv_obj_t* list, const lv_font_t* font, const KvxTokens& tokens) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kScreenWidth - 12, 14);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(row);
    lv_label_set_text(label, "");
    lv_obj_set_user_data(label, reinterpret_cast<void*>(kRoleLabel));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, kScreenWidth - 22);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, tokens.ink_dim, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

lv_obj_t* make_panel(lv_obj_t* parent, int top, const KvxTokens& tokens) {
    auto* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, kScreenWidth - 8, kContentHeight - top - 2);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_style_radius(panel, 5, 0);
    lv_obj_set_style_bg_color(panel, tokens.surface, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 8, 0);
    lv_obj_set_style_pad_right(panel, 8, 0);
    lv_obj_set_style_pad_top(panel, 8, 0);
    lv_obj_set_style_pad_bottom(panel, 10, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    return panel;
}

lv_obj_t* make_doc_body(lv_obj_t* panel, const lv_font_t* font, const KvxTokens& tokens) {
    auto* label = lv_label_create(panel);
    lv_label_set_text(label, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, kScreenWidth - 24);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, tokens.ink, 0);
    // Margins keep glyphs inside the rounded clip while the panel scrolls.
    lv_obj_set_style_margin_top(label, 2, 0);
    lv_obj_set_style_margin_bottom(label, 4, 0);
    return label;
}

lv_obj_t* make_kv_row(lv_obj_t* parent,
                      const char* label,
                      const char* value,
                      const lv_font_t* font,
                      const KvxTokens& tokens) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_top(row, 1, 0);
    lv_obj_set_style_pad_bottom(row, 1, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto* key = lv_label_create(row);
    lv_label_set_text(key, label ? label : "");
    lv_obj_set_style_text_font(key, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(key, tokens.ink_dim, 0);
    lv_obj_set_width(key, 64);
    lv_label_set_long_mode(key, LV_LABEL_LONG_WRAP);

    auto* val = lv_label_create(row);
    lv_label_set_text(val, value ? value : "");
    lv_obj_set_user_data(val, reinterpret_cast<void*>(kRoleLabel));
    lv_obj_set_style_text_font(val, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(val, tokens.ink, 0);
    lv_obj_set_flex_grow(val, 1);
    // Wrap so long GPS/power errors stay readable on the 320px panel (scrolls).
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
    return row;
}

namespace {

lv_color_t pill_color(StatusPillKind kind, const KvxTokens& tokens) {
    switch (kind) {
    case StatusPillKind::Ok:
        return tokens.accent;
    case StatusPillKind::Missing:
        return tokens.ink_dim;
    case StatusPillKind::Error:
        return lv_color_hex(0xE08080);
    case StatusPillKind::Busy:
        return tokens.main;
    case StatusPillKind::Idle:
    default:
        return tokens.rule;
    }
}

} // namespace

lv_obj_t* make_status_pill(lv_obj_t* parent,
                           const char* text,
                           StatusPillKind kind,
                           const lv_font_t* font,
                           const KvxTokens& tokens) {
    auto* pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_height(pill, 14);
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(pill, 4, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(pill, 6, 0);
    lv_obj_set_style_pad_right(pill, 6, 0);
    lv_obj_set_style_pad_top(pill, 1, 0);
    lv_obj_set_style_pad_bottom(pill, 1, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    auto* label = lv_label_create(pill);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    set_status_pill(pill, text, kind, tokens);
    return pill;
}

void set_status_pill(lv_obj_t* pill, const char* text, StatusPillKind kind, const KvxTokens& tokens) {
    if (!pill) return;
    const lv_color_t c = pill_color(kind, tokens);
    lv_obj_set_style_bg_color(pill, lv_color_mix(c, tokens.bg, 90), 0);
    if (auto* label = lv_obj_get_child(pill, 0)) {
        if (text) lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, c, 0);
    }
}

lv_obj_t* make_line_input(lv_obj_t* parent,
                          const KvxTokens& tokens,
                          const lv_font_t* font,
                          int width) {
    const lv_font_t* use = font ? font : &lv_font_montserrat_12;
    // CJK faces report a tall em box. ASCII entry fits Montserrat, and the
    // glyphs stay inside a single row on this panel.
    if (lv_font_get_line_height(use) > 16) {
        use = &lv_font_montserrat_12;
    }
    const int line = lv_font_get_line_height(use);
    constexpr int kPadV = 2;
    constexpr int kBorder = 1;

    auto* ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, width);
    lv_obj_set_style_text_font(ta, use, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, use, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_pad_top(ta, kPadV, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ta, kPadV, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, kBorder, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, tokens.main, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, tokens.surface, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, tokens.ink, LV_PART_MAIN);
    lv_obj_set_style_outline_width(ta, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(ta, tokens.accent, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(ta, tokens.accent, LV_PART_CURSOR | LV_STATE_FOCUSED);

    // one_line replaces any height with LV_SIZE_CONTENT. Set the row height after.
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_height(ta, line + kPadV * 2 + kBorder * 2);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);

    const uint32_t children = lv_obj_get_child_count(ta);
    for (uint32_t i = 0; i < children; ++i) {
        auto* child = lv_obj_get_child(ta, i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_font(child, use, 0);
            lv_obj_set_style_text_color(child, tokens.ink, 0);
        }
    }
    return ta;
}

} // namespace view

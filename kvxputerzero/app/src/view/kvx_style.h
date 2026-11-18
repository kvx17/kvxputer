#pragma once

#include "lvgl.h"

#include <cstdint>

namespace view {

struct KvxTokens {
    lv_color_t main;
    lv_color_t accent;
    lv_color_t bg;
    lv_color_t ink;
    lv_color_t ink_dim;
    lv_color_t bar;
    lv_color_t surface;
    lv_color_t surface_sel;
    lv_color_t rule;
    lv_color_t icon_idle;
    lv_color_t icon_sel;
    bool light_bg = false;
};

enum class TileKind {
    Normal = 0,
    ComingSoon,
    UseStock,
};

struct TileState {
    bool selected = false;
    TileKind kind = TileKind::Normal;
};

KvxTokens make_tokens(uint32_t main_rgb, uint32_t accent_rgb, uint32_t bg_rgb);

void style_bar(lv_obj_t* obj, const KvxTokens& tokens);
void style_tile(lv_obj_t* obj, int radius);

void apply_tile_state(lv_obj_t* tile,
                      lv_obj_t* icon,
                      lv_obj_t* label,
                      lv_obj_t* badge,
                      const TileState& state,
                      const KvxTokens& tokens);

lv_obj_t* make_list(lv_obj_t* parent, int top);

lv_obj_t* make_list_row(lv_obj_t* list,
                        const char* text,
                        const char* icon,
                        const lv_font_t* font,
                        const lv_font_t* icon_font,
                        const KvxTokens& tokens,
                        uint32_t swatch_rgb = 0);

void apply_row_state(lv_obj_t* row, bool selected, const KvxTokens& tokens);

// Log lines: no per-line surface; selected line gets surface_sel fill, no border.
void apply_log_row_state(lv_obj_t* row, bool selected, const KvxTokens& tokens);
lv_obj_t* make_log_row(lv_obj_t* list, const lv_font_t* font, const KvxTokens& tokens);

// Shared content-type chrome (Document / Info). Border means selectable — these never border.
lv_obj_t* make_panel(lv_obj_t* parent, int top, const KvxTokens& tokens);
lv_obj_t* make_doc_body(lv_obj_t* panel, const lv_font_t* font, const KvxTokens& tokens);
lv_obj_t* make_kv_row(lv_obj_t* parent,
                      const char* label,
                      const char* value,
                      const lv_font_t* font,
                      const KvxTokens& tokens);
enum class StatusPillKind { Idle, Ok, Missing, Error, Busy };
lv_obj_t* make_status_pill(lv_obj_t* parent,
                           const char* text,
                           StatusPillKind kind,
                           const lv_font_t* font,
                           const KvxTokens& tokens);
void set_status_pill(lv_obj_t* pill, const char* text, StatusPillKind kind, const KvxTokens& tokens);

// Single-row text field. The default textarea theme pads with DPI-scaled
// values and lv_textarea_set_one_line() then sizes to that content, which
// fills the 170px panel and clips the glyphs. This keeps one visible row.
lv_obj_t* make_line_input(lv_obj_t* parent,
                          const KvxTokens& tokens,
                          const lv_font_t* font,
                          int width);

} // namespace view

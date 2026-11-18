#include "pda_hub_screen.h"

#include "asset_manager.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

constexpr int kCols = 4;
constexpr int kRows = 2;

// Column-major like Cardputer: slot = col * rows + row
void slot_row_col(int slot, int& row, int& col) {
    row = slot % kRows;
    col = slot / kRows;
}

int index_from_row_col(int row, int col) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) return -1;
    return col * kRows + row;
}

const char* short_label(int id) {
    const char* full = tools::pda::channel_label(static_cast<tools::pda::Channel>(id));
    // Truncate visually for tile; LVGL will clip if needed.
    return full;
}

} // namespace

PdaHubScreen::PdaHubScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    selected_ = view_model.model().pda_hub_index();
    init();
}

void PdaHubScreen::build() {
    std::string error;
    tools::pda::ensure_dirs(error);
    make_header(content(), "PDA");

    constexpr int margin = 6;
    constexpr int gap = 4;
    constexpr int footer_h = 16;
    const int grid_top = 20;
    const int grid_h = view::kContentHeight - grid_top - footer_h - 2;
    const int cell_w = (view::kScreenWidth - 2 * margin - (kCols - 1) * gap) / kCols;
    const int cell_h = (grid_h - (kRows - 1) * gap) / kRows;

    auto* font12 = assets().load_standard_font(12);
    auto* font10 = assets().load_standard_font(10);

    for (int id = 0; id < 8; ++id) {
        int row = 0, col = 0;
        slot_row_col(id, row, col);
        const int x = margin + col * (cell_w + gap);
        const int y = grid_top + row * (cell_h + gap);

        auto* tile = lv_obj_create(content());
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, cell_w, cell_h);
        lv_obj_set_pos(tile, x, y);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 2, 0);

        auto* digit = lv_label_create(tile);
        char dig[2] = {'?', '\0'};
        const auto& binds = view_model().model().pda_key_bind();
        for (int k = 0; k < 8; ++k) {
            if (binds[static_cast<size_t>(k)] == static_cast<uint8_t>(id)) {
                dig[0] = static_cast<char>('1' + k);
                break;
            }
        }
        lv_label_set_text(digit, dig);
        lv_obj_set_style_text_font(digit, font10 ? font10 : &lv_font_montserrat_14, 0);
        lv_obj_align(digit, LV_ALIGN_TOP_LEFT, 3, 2);

        auto* label = lv_label_create(tile);
        lv_label_set_text(label, short_label(id));
        lv_obj_set_style_text_font(label, font12 ? font12 : &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, cell_w - 6);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 2);

        tiles_[id] = tile;
    }

    footer_ = lv_label_create(content());
    lv_obj_set_style_text_font(footer_, font10 ? font10 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(footer_, sec_color(), 0);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, -2);
    refresh();
}

void PdaHubScreen::refresh() {
    view_model().model().set_pda_hub_index(selected_);
    for (int id = 0; id < 8; ++id) {
        const bool on = id == selected_;
        lv_obj_set_style_bg_color(tiles_[id], on ? sec_color() : pri_color(), 0);
        lv_obj_set_style_border_color(tiles_[id], on ? sec_color() : pri_color(), 0);
        auto* digit = lv_obj_get_child(tiles_[id], 0);
        auto* label = lv_obj_get_child(tiles_[id], 1);
        const lv_color_t fg = on ? bg_color() : sec_color();
        if (digit) lv_obj_set_style_text_color(digit, fg, 0);
        if (label) lv_obj_set_style_text_color(label, fg, 0);

        // Refresh bound digit
        if (digit) {
            char dig[2] = {'?', '\0'};
            const auto& binds = view_model().model().pda_key_bind();
            for (int k = 0; k < 8; ++k) {
                if (binds[static_cast<size_t>(k)] == static_cast<uint8_t>(id)) {
                    dig[0] = static_cast<char>('1' + k);
                    break;
                }
            }
            lv_label_set_text(digit, dig);
        }
    }
    if (footer_) {
        lv_label_set_text_fmt(footer_, "%s  1-8 open  9 bind",
                              tools::pda::channel_label(static_cast<tools::pda::Channel>(selected_)));
    }
}

void PdaHubScreen::move(int drow, int dcol) {
    int row = 0, col = 0;
    slot_row_col(selected_, row, col);
    const int next = index_from_row_col(row + drow, col + dcol);
    if (next >= 0) {
        selected_ = next;
        refresh();
    }
}

void PdaHubScreen::activate() {
    view_model().open_pda_channel(selected_);
}

void PdaHubScreen::open_bound(int key1to8) {
    const auto& binds = view_model().model().pda_key_bind();
    int ch = binds[static_cast<size_t>(key1to8 - 1)];
    if (ch < 0 || ch > 7) ch = key1to8 - 1;
    selected_ = ch;
    refresh();
    view_model().open_pda_channel(ch);
}

bool PdaHubScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_UP || key == 'w' || key == 'W') {
        move(-1, 0);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 's' || key == 'S') {
        move(1, 0);
        return true;
    }
    if (key == LV_KEY_LEFT || key == 'a' || key == 'A') {
        move(0, -1);
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'd' || key == 'D') {
        move(0, 1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ' || key == '\n') {
        activate();
        return true;
    }
    if (key >= '1' && key <= '8') {
        open_bound(static_cast<int>(key - '0'));
        return true;
    }
    if (key == '9') {
        view_model().open_pda_rebind();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaHubScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move(-1, 0); }},
        {view::ICON_CHECK, [this]() { activate(); }},
        {view::ICON_CARET_DOWN, [this]() { move(1, 0); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

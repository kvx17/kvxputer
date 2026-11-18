#include "pda_hub_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

constexpr int kCols = 4;
constexpr int kRows = 2;

void slot_row_col(int slot, int& row, int& col) {
    row = slot % kRows;
    col = slot / kRows;
}

int index_from_row_col(int row, int col) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) return -1;
    return col * kRows + row;
}

const char* pda_icon(int id) {
    switch (id) {
    case 0:
        return view::ICON_PDA_NOTES;
    case 1:
        return view::ICON_PDA_MEMOS;
    case 2:
        return view::ICON_PDA_TODO;
    case 3:
        return view::ICON_PDA_CALENDAR;
    case 4:
        return view::ICON_PDA_CONTACTS;
    case 5:
        return view::ICON_PDA_ALARMS;
    case 6:
        return view::ICON_PDA_WORLD;
    case 7:
        return view::ICON_PDA_CALC;
    default:
        return view::ICON_PDA_NOTES;
    }
}

const char* short_label(int id) {
    switch (id) {
    case 0:
        return "Notes";
    case 1:
        return "Memos";
    case 2:
        return "To-Do";
    case 3:
        return "Calendar";
    case 4:
        return "Contacts";
    case 5:
        return "Alarms";
    case 6:
        return "World";
    case 7:
        return "Calc";
    default:
        return "PDA";
    }
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

    const auto tok = tokens();
    constexpr int margin = 6;
    constexpr int gap = 4;
    constexpr int footer_h = 14;
    const int grid_top = 20;
    const int grid_h = view::kContentHeight - grid_top - footer_h - 2;
    const int cell_w = (view::kScreenWidth - 2 * margin - (kCols - 1) * gap) / kCols;
    const int cell_h = (grid_h - (kRows - 1) * gap) / kRows;

    auto* font11 = assets().load_standard_font(11);
    auto* font10 = assets().load_standard_font(10);
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 22);

    for (int id = 0; id < 8; ++id) {
        int row = 0, col = 0;
        slot_row_col(id, row, col);
        const int x = margin + col * (cell_w + gap);
        const int y = grid_top + row * (cell_h + gap);

        auto* tile = lv_obj_create(content());
        view::style_tile(tile, 8);
        lv_obj_set_size(tile, cell_w, cell_h);
        lv_obj_set_pos(tile, x, y);

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
        lv_obj_set_style_text_font(digit, font10 ? font10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_hor(digit, 2, 0);
        lv_obj_set_style_radius(digit, 3, 0);
        lv_obj_set_style_bg_opa(digit, LV_OPA_40, 0);
        lv_obj_set_style_bg_color(digit, tok.rule, 0);
        lv_obj_align(digit, LV_ALIGN_TOP_LEFT, 3, 2);

        auto* icon = lv_label_create(tile);
        lv_label_set_text(icon, pda_icon(id));
        if (icon_font) {
            lv_obj_set_style_text_font(icon, icon_font, 0);
        }
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

        auto* label = lv_label_create(tile);
        lv_label_set_text(label, short_label(id));
        lv_obj_set_style_text_font(label, font11 ? font11 : &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, cell_w - 6);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -3);

        tiles_[id] = tile;
        tile_digits_[id] = digit;
        tile_icons_[id] = icon;
        tile_labels_[id] = label;
    }

    footer_ = lv_label_create(content());
    lv_obj_set_style_text_font(footer_, font10 ? font10 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer_, tok.ink_dim, 0);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, -2);
    refresh();
}

void PdaHubScreen::refresh() {
    view_model().model().set_pda_hub_index(selected_);
    const auto tok = tokens();
    for (int id = 0; id < 8; ++id) {
        view::TileState state;
        state.selected = (id == selected_);
        state.kind = view::TileKind::Normal;
        view::apply_tile_state(tiles_[id], tile_icons_[id], tile_labels_[id], nullptr, state, tok);

        if (tile_digits_[id]) {
            char dig[2] = {'?', '\0'};
            const auto& binds = view_model().model().pda_key_bind();
            for (int k = 0; k < 8; ++k) {
                if (binds[static_cast<size_t>(k)] == static_cast<uint8_t>(id)) {
                    dig[0] = static_cast<char>('1' + k);
                    break;
                }
            }
            lv_label_set_text(tile_digits_[id], dig);
            lv_obj_set_style_text_color(tile_digits_[id],
                                        state.selected ? tok.ink : tok.ink_dim, 0);
        }
    }
    if (footer_) {
        lv_label_set_text_fmt(footer_, "%s  1-8 open  9 bind",
                              tools::pda::channel_label(static_cast<tools::pda::Channel>(selected_)));
        lv_obj_set_style_text_color(footer_, tok.ink_dim, 0);
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

#include "channel_grid_screen.h"

#include "asset_manager.h"
#include "channel_registry.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

void channel_obs(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<ChannelGridScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_selection();
}

} // namespace

ChannelGridScreen::ChannelGridScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void ChannelGridScreen::build() {
    auto* root_content = content();

    icon_font_ = assets().load_font("Phosphor-Fill.ttf", 36);

    constexpr int kFooterH = 16;
    footer_ = lv_obj_create(root_content);
    lv_obj_remove_style_all(footer_);
    lv_obj_set_size(footer_, view::kScreenWidth, kFooterH);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(footer_, pri_color(), 0);
    lv_obj_set_style_bg_opa(footer_, LV_OPA_COVER, 0);

    auto* footer_label = lv_label_create(footer_);
    lv_obj_set_style_text_color(footer_label, lv_color_hex(0xFFFFFF), 0);
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(footer_label, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_center(footer_label);
    lv_obj_set_user_data(footer_, footer_label);

    const int grid_h = view::kContentHeight - kFooterH - 2;
    grid_ = lv_obj_create(root_content);
    lv_obj_remove_style_all(grid_);
    lv_obj_set_size(grid_, view::kScreenWidth - 4, grid_h);
    lv_obj_align(grid_, LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int gap = 3;
    const int cell_w = (view::kScreenWidth - 4 - gap * (kCols - 1)) / kCols;
    const int cell_h = (grid_h - gap * (kRows - 1)) / kRows;

    for (int slot = 0; slot < kSlots; ++slot) {
        // Column-major like Cardputer: 0 2 4 / 1 3 5
        const int row = slot % kRows;
        const int col = slot / kRows;
        auto* tile = lv_obj_create(grid_);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, cell_w, cell_h);
        lv_obj_set_pos(tile, col * (cell_w + gap), row * (cell_h + gap));
        lv_obj_set_style_radius(tile, 6, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 2, 0);

        auto* icon = lv_label_create(tile);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
        if (icon_font_) {
            lv_obj_set_style_text_font(icon, icon_font_, 0);
        } else {
            auto* tfont = assets().load_standard_font(16);
            lv_obj_set_style_text_font(icon, tfont ? tfont : &lv_font_montserrat_16, 0);
        }
        lv_obj_center(icon);

        tiles_[static_cast<size_t>(slot)] = tile;
        tile_icons_[static_cast<size_t>(slot)] = icon;
    }

    channel_observer_ =
        lv_subject_add_observer_with_target(view_model().channel_index_subject(), channel_obs, this, this);
    rebuild_tiles();
    refresh_selection();
}

ChannelGridScreen::~ChannelGridScreen() {
    if (channel_observer_) {
        lv_observer_remove(channel_observer_);
        channel_observer_ = nullptr;
    }
}

void ChannelGridScreen::rebuild_tiles() {
    const int index = view_model().channel_index();
    const int page = index / kSlots;
    const int page_base = page * kSlots;

    for (int slot = 0; slot < kSlots; ++slot) {
        const int ch = page_base + slot;
        auto* icon = tile_icons_[static_cast<size_t>(slot)];
        if (ch >= 0 && static_cast<size_t>(ch) < model::kChannelCount) {
            const auto& desc = model::kChannels[static_cast<size_t>(ch)];
            if (icon_font_ && desc.icon && desc.icon[0] != '\0') {
                lv_label_set_text(icon, desc.icon);
            } else {
                // Fallback: first letter of the label.
                char letter[2] = {desc.label[0], '\0'};
                lv_label_set_text(icon, letter);
            }
            lv_obj_clear_flag(tiles_[static_cast<size_t>(slot)], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(icon, "");
            lv_obj_add_flag(tiles_[static_cast<size_t>(slot)], LV_OBJ_FLAG_HIDDEN);
        }
    }

    auto* footer_label = static_cast<lv_obj_t*>(lv_obj_get_user_data(footer_));
    if (footer_label) {
        const auto& ch = view_model().current_channel();
        lv_label_set_text_fmt(footer_label, "%s  ·  %d/%d", ch.label, page + 1,
                              static_cast<int>((model::kChannelCount + kSlots - 1) / kSlots));
    }
}

void ChannelGridScreen::refresh_selection() {
    rebuild_tiles();
    const int index = view_model().channel_index();
    const int page_base = (index / kSlots) * kSlots;
    const int selected_slot = index - page_base;

    for (int slot = 0; slot < kSlots; ++slot) {
        auto* tile = tiles_[static_cast<size_t>(slot)];
        const bool selected = (slot == selected_slot);
        lv_obj_set_style_bg_color(tile, selected ? pri_color() : lv_color_darken(pri_color(), 40), 0);
        lv_obj_set_style_border_color(tile, selected ? sec_color() : pri_color(), 0);
        auto* icon = tile_icons_[static_cast<size_t>(slot)];
        if (icon) {
            lv_obj_set_style_text_color(icon, selected ? sec_color() : lv_color_hex(0xFFFFFF), 0);
        }
    }
    lv_obj_set_style_bg_color(footer_, pri_color(), 0);
}

void ChannelGridScreen::move_selection(int d_row, int d_col) {
    const int count = static_cast<int>(model::kChannelCount);
    int index = view_model().channel_index();
    const int page_base = (index / kSlots) * kSlots;
    int slot = index - page_base;
    int row = slot % kRows;
    int col = slot / kRows;

    if (d_row != 0) {
        int nr = row + d_row;
        if (nr < 0 || nr >= kRows) {
            view_model().move_channel(d_row > 0 ? 1 : -1);
            return;
        }
        int ni = page_base + col * kRows + nr;
        if (ni >= 0 && ni < count) {
            view_model().set_channel_index(ni);
        } else {
            view_model().move_channel(d_row > 0 ? 1 : -1);
        }
        return;
    }

    if (d_col != 0) {
        int nc = col + d_col;
        if (nc < 0 || nc >= kCols) {
            // Page or wrap via sequential step.
            view_model().move_channel(d_col > 0 ? kRows : -kRows);
            return;
        }
        int ni = page_base + nc * kRows + row;
        if (ni >= 0 && ni < count) {
            view_model().set_channel_index(ni);
        } else {
            view_model().move_channel(d_col > 0 ? 1 : -1);
        }
    }
}

bool ChannelGridScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_RIGHT) {
        move_selection(0, 1);
        return true;
    }
    if (key == LV_KEY_LEFT) {
        move_selection(0, -1);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        move_selection(1, 0);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1, 0);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == ' ') {
        view_model().open_selected_channel();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> ChannelGridScreen::nav_actions() {
    return {{
        {view::ICON_SKIP_BACK, [this]() { view_model().move_channel(-kSlots); }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1, 0); }},
        {view::ICON_PLAY, [this]() { view_model().open_selected_channel(); }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1, 0); }},
        {view::ICON_SKIP_FORWARD, [this]() { view_model().move_channel(kSlots); }},
    }};
}

} // namespace screen

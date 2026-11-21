#include "channel_grid_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <cstring>

namespace screen {
namespace {

void channel_obs(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<ChannelGridScreen*>(lv_observer_get_user_data(observer));
    if (self) self->refresh_selection();
}

const char* channel_icon(const char* id) {
    if (!id) return view::ICON_CH_TOOLS;
    if (std::strcmp(id, "wifi") == 0) return view::ICON_CH_WIFI;
    if (std::strcmp(id, "discovery") == 0) return view::ICON_CH_DISCOVERY;
    if (std::strcmp(id, "netops") == 0) return view::ICON_CH_NETOPS;
    if (std::strcmp(id, "ble") == 0) return view::ICON_CH_BLE;
    if (std::strcmp(id, "rf") == 0) return view::ICON_CH_RF;
    if (std::strcmp(id, "nrf24") == 0) return view::ICON_CH_NRF24;
    if (std::strcmp(id, "lora") == 0) return view::ICON_CH_LORA;
    if (std::strcmp(id, "fm") == 0) return view::ICON_CH_FM;
    if (std::strcmp(id, "infrared") == 0) return view::ICON_CH_IR;
    if (std::strcmp(id, "ethernet") == 0) return view::ICON_CH_ETH;
    if (std::strcmp(id, "usb") == 0) return view::ICON_CH_USB;
    if (std::strcmp(id, "gps") == 0) return view::ICON_CH_GPS;
    if (std::strcmp(id, "rfid") == 0) return view::ICON_CH_RFID;
    if (std::strcmp(id, "nfc") == 0) return view::ICON_CH_NFC;
    if (std::strcmp(id, "files") == 0) return view::ICON_CH_FILES;
    if (std::strcmp(id, "camera") == 0) return view::ICON_CH_CAMERA;
    if (std::strcmp(id, "gallery") == 0) return view::ICON_CH_GALLERY;
    if (std::strcmp(id, "scripts") == 0) return view::ICON_CH_SCRIPTS;
    if (std::strcmp(id, "pcconnect") == 0) return view::ICON_CH_USB;
    if (std::strcmp(id, "clock") == 0) return view::ICON_CH_CLOCK;
    if (std::strcmp(id, "charge") == 0) return view::ICON_CH_CHARGE;
    if (std::strcmp(id, "others") == 0) return view::ICON_CH_TOOLS;
    if (std::strcmp(id, "modules") == 0) return view::ICON_CH_MODULES;
    if (std::strcmp(id, "config") == 0) return view::ICON_CH_CONFIG;
    return view::ICON_CH_TOOLS;
}

view::TileKind tile_kind_for(model::ChannelKind kind) {
    switch (kind) {
    case model::ChannelKind::ComingSoon:
        return view::TileKind::ComingSoon;
    case model::ChannelKind::UseStock:
        return view::TileKind::UseStock;
    default:
        return view::TileKind::Normal;
    }
}

const char* status_hint(model::ChannelKind kind) {
    switch (kind) {
    case model::ChannelKind::ComingSoon:
        return "Coming soon";
    case model::ChannelKind::UseStock:
        return "Stock app";
    default:
        return "";
    }
}

} // namespace

ChannelGridScreen::ChannelGridScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void ChannelGridScreen::build() {
    auto* root_content = content();
    const auto tok = tokens();

    icon_font_ = assets().load_font("Phosphor-Fill.ttf", 28);
    badge_font_ = assets().load_font("Phosphor-Fill.ttf", 10);

    footer_ = lv_obj_create(root_content);
    lv_obj_remove_style_all(footer_);
    lv_obj_set_size(footer_, view::kScreenWidth, kFooterH);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_MID, 0, 0);
    view::style_bar(footer_, tok);

    auto* rule = lv_obj_create(footer_);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, view::kScreenWidth, 1);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(rule, tok.rule, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    auto* name_font = assets().load_standard_font(11);
    footer_name_ = lv_label_create(footer_);
    lv_obj_set_style_text_color(footer_name_, tok.ink, 0);
    lv_obj_set_style_text_font(footer_name_, name_font ? name_font : &lv_font_montserrat_12, 0);
    lv_obj_align(footer_name_, LV_ALIGN_LEFT_MID, 8, 0);

    footer_hint_ = lv_label_create(footer_);
    auto* hint_font = assets().load_standard_font(10);
    lv_obj_set_style_text_color(footer_hint_, tok.ink_dim, 0);
    lv_obj_set_style_text_font(footer_hint_, hint_font ? hint_font : &lv_font_montserrat_12, 0);
    lv_obj_align(footer_hint_, LV_ALIGN_LEFT_MID, 70, 0);

    constexpr int kDotSize = 5;
    constexpr int kDotGap = 4;
    const int dots_w = kPageCount * kDotSize + (kPageCount - 1) * kDotGap;
    for (int i = 0; i < kPageCount; ++i) {
        auto* dot = lv_obj_create(footer_);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, kDotSize, kDotSize);
        lv_obj_set_style_radius(dot, kDotSize / 2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -8 - (dots_w - (i + 1) * kDotSize - i * kDotGap), 0);
        page_dots_[static_cast<size_t>(i)] = dot;
    }

    const int grid_h = view::kContentHeight - kFooterH;
    grid_ = lv_obj_create(root_content);
    lv_obj_remove_style_all(grid_);
    lv_obj_set_size(grid_, view::kScreenWidth, grid_h);
    lv_obj_align(grid_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int margin_x = 6;
    constexpr int margin_top = 4;
    constexpr int gap = 4;
    const int cell_w = (view::kScreenWidth - 2 * margin_x - gap * (kCols - 1)) / kCols;
    const int cell_h = (grid_h - margin_top - gap * (kRows - 1) - 2) / kRows;

    auto* label_font = assets().load_standard_font(11);

    for (int slot = 0; slot < kSlots; ++slot) {
        const int row = slot % kRows;
        const int col = slot / kRows;
        auto* tile = lv_obj_create(grid_);
        view::style_tile(tile, 8);
        lv_obj_set_size(tile, cell_w, cell_h);
        lv_obj_set_pos(tile, margin_x + col * (cell_w + gap), margin_top + row * (cell_h + gap));

        auto* icon = lv_label_create(tile);
        if (icon_font_) {
            lv_obj_set_style_text_font(icon, icon_font_, 0);
        } else {
            auto* tfont = assets().load_standard_font(16);
            lv_obj_set_style_text_font(icon, tfont ? tfont : &lv_font_montserrat_16, 0);
        }
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 6);

        auto* label = lv_label_create(tile);
        lv_obj_set_style_text_font(label, label_font ? label_font : &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, cell_w - 8);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);

        auto* badge = lv_label_create(tile);
        if (badge_font_) {
            lv_obj_set_style_text_font(badge, badge_font_, 0);
        }
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -3, 3);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);

        tiles_[static_cast<size_t>(slot)] = tile;
        tile_icons_[static_cast<size_t>(slot)] = icon;
        tile_labels_[static_cast<size_t>(slot)] = label;
        tile_badges_[static_cast<size_t>(slot)] = badge;
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
    const auto tok = tokens();
    const int index = view_model().channel_index();
    const int page = index / kSlots;
    const int page_base = page * kSlots;

    for (int slot = 0; slot < kSlots; ++slot) {
        const int ch = page_base + slot;
        auto* icon = tile_icons_[static_cast<size_t>(slot)];
        auto* label = tile_labels_[static_cast<size_t>(slot)];
        auto* tile = tiles_[static_cast<size_t>(slot)];
        if (ch >= 0 && static_cast<size_t>(ch) < model::kChannelCount) {
            const auto& desc = model::kChannels[static_cast<size_t>(ch)];
            if (icon_font_) {
                lv_label_set_text(icon, channel_icon(desc.id));
            } else {
                char letter[2] = {desc.label[0], '\0'};
                lv_label_set_text(icon, letter);
            }
            lv_label_set_text(label, desc.label);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(icon, "");
            lv_label_set_text(label, "");
            lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (footer_name_) {
        const auto& ch = view_model().current_channel();
        lv_label_set_text(footer_name_, ch.label);
        if (footer_hint_) {
            lv_label_set_text(footer_hint_, status_hint(ch.kind));
            // Reposition hint after the name.
            lv_obj_update_layout(footer_name_);
            const lv_coord_t name_w = lv_obj_get_width(footer_name_);
            lv_obj_align(footer_hint_, LV_ALIGN_LEFT_MID, 8 + name_w + 8, 0);
        }
    }

    for (int i = 0; i < kPageCount; ++i) {
        auto* dot = page_dots_[static_cast<size_t>(i)];
        if (!dot) continue;
        lv_obj_set_style_bg_color(dot, (i == page) ? tok.accent : tok.rule, 0);
    }

    view::style_bar(footer_, tok);
}

void ChannelGridScreen::refresh_selection() {
    rebuild_tiles();
    const auto tok = tokens();
    const int index = view_model().channel_index();
    const int page_base = (index / kSlots) * kSlots;
    const int selected_slot = index - page_base;

    for (int slot = 0; slot < kSlots; ++slot) {
        const int ch = page_base + slot;
        if (ch < 0 || static_cast<size_t>(ch) >= model::kChannelCount) continue;

        view::TileState state;
        state.selected = (slot == selected_slot);
        state.kind = tile_kind_for(model::kChannels[static_cast<size_t>(ch)].kind);
        view::apply_tile_state(tiles_[static_cast<size_t>(slot)],
                               tile_icons_[static_cast<size_t>(slot)],
                               tile_labels_[static_cast<size_t>(slot)],
                               tile_badges_[static_cast<size_t>(slot)],
                               state,
                               tok);
    }
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
        {view::ICON_CHECK, [this]() { view_model().open_selected_channel(); }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1, 0); }},
        {view::ICON_SKIP_FORWARD, [this]() { view_model().move_channel(kSlots); }},
    }};
}

} // namespace screen

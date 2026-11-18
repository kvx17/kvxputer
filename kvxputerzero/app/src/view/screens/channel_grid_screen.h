#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>

namespace screen {

class ChannelGridScreen final : public KvxScreen {
public:
    ChannelGridScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ChannelGridScreen() override;

    void refresh_selection();

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void rebuild_tiles();
    void move_selection(int d_row, int d_col);

    static constexpr int kCols = 3;
    static constexpr int kRows = 2;
    static constexpr int kSlots = kCols * kRows;

    lv_obj_t* footer_{nullptr};
    lv_obj_t* grid_{nullptr};
    std::array<lv_obj_t*, kSlots> tiles_{};
    std::array<lv_obj_t*, kSlots> tile_icons_{};
    lv_font_t* icon_font_{nullptr};
    lv_observer_t* channel_observer_{nullptr};
};

} // namespace screen

#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>

namespace screen {

class PdaHubScreen final : public KvxScreen {
public:
    PdaHubScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();
    void move(int drow, int dcol);
    void activate();
    void open_bound(int key1to8);

    lv_obj_t* tiles_[8]{};
    lv_obj_t* tile_icons_[8]{};
    lv_obj_t* tile_labels_[8]{};
    lv_obj_t* tile_digits_[8]{};
    lv_obj_t* footer_{nullptr};
    int selected_ = 0;
};

} // namespace screen

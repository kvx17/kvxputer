#pragma once

#include "kvx_screen.h"

#include "launch_catalog.h"

#include <vector>

namespace screen {

class SearchScreen final : public KvxScreen {
public:
    SearchScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void rebuild();
    void move_selection(int delta);
    void activate();
    int visible_rows() const;

    lv_obj_t* input_{nullptr};
    lv_obj_t* list_{nullptr};
    std::string query_;
    std::vector<const model::LaunchTarget*> shown_;
    int selected_ = 0;
    int visible_ = 5;
    bool showing_recent_ = false;
};

} // namespace screen

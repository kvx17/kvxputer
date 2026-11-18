#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace screen {

struct MenuEntry {
    std::string label;
    std::function<void()> action;
    const char* icon = nullptr;
    uint32_t swatch = 0; // 0 = none
};

class OptionsListScreen final : public KvxScreen {
public:
    OptionsListScreen(viewmodel::BaseViewModel& view_model,
                      app::AssetManager& assets,
                      std::string title,
                      std::vector<MenuEntry> entries);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();
    void move_selection(int delta);
    void activate();

    std::string title_;
    std::vector<MenuEntry> entries_;
    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> rows_;
    int selected_ = 0;
};

} // namespace screen

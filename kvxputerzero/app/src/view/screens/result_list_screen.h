#pragma once

#include "kvx_screen.h"

#include <functional>
#include <string>
#include <vector>

namespace screen {

class ResultListScreen final : public KvxScreen {
public:
    ResultListScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                     std::vector<std::string> lines, std::function<void()> on_save = nullptr);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();
    void move_selection(int delta);

    std::string title_;
    std::vector<std::string> lines_;
    std::function<void()> on_save_;
    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> rows_;
    int selected_ = 0;
};

} // namespace screen

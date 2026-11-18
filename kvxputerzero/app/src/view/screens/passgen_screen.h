#pragma once

#include "kvx_screen.h"
#include "passgen_lib.h"

#include <string>
#include <vector>

namespace screen {

class PassgenScreen final : public KvxScreen {
public:
    PassgenScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();
    void regenerate();
    void save();

    tools::passgen::Options opt_;
    std::vector<std::string> passwords_;
    lv_obj_t* body_{nullptr};
    std::string status_;
};

} // namespace screen

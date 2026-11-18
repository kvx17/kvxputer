#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>
#include <string>

namespace screen {

class PdaViewScreen final : public KvxScreen {
public:
    PdaViewScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void open_edit();
    void do_delete();

    lv_obj_t* body_{nullptr};
};

} // namespace screen

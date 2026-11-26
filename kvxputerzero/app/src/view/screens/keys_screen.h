#pragma once

#include "kvx_screen.h"

#include <string>

namespace screen {

// Keymap checker: big name + code/hex of every key pressed, with a recent row.
// Useful for verifying the Sym/Fn layers. (pocket-zero-tools "Keys".)
class KeysScreen final : public KvxScreen {
public:
    KeysScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();

    lv_obj_t* big_{nullptr};
    lv_obj_t* code_{nullptr};
    lv_obj_t* recent_{nullptr};
    lv_obj_t* hint_{nullptr};
    std::string last_name_{"--"};
    uint32_t last_key_{0};
    bool have_key_{false};
    std::string recent_text_;
    int esc_count_{0};
};

} // namespace screen

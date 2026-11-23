#pragma once

#include "kvx_screen.h"

namespace screen {

class ShortcutCaptureScreen final : public KvxScreen {
public:
    ShortcutCaptureScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void set_status(const char* text);

    lv_obj_t* status_{nullptr};
};

} // namespace screen

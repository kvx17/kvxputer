#pragma once

#include "kvx_screen.h"

#include "lvgl.h"

#include <array>
#include <cstdint>
#include <vector>

namespace screen {

// Config → About: short intro, three scannable links, then this device.
class AboutScreen final : public KvxScreen {
public:
    AboutScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~AboutScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    struct QrSlot {
        std::vector<uint8_t> pixels;
        lv_image_dsc_t dsc{};
    };

    void scroll_by(int px);
    void update_scroll_hint();
    bool paint_qr(QrSlot& slot, const char* url);
    void add_link(const char* title, const char* shown, const char* url, QrSlot& slot);

    lv_obj_t* panel_{nullptr};
    lv_obj_t* scroll_hint_{nullptr};
    std::array<lv_obj_t*, 3> images_{};
    QrSlot repo_qr_{};
    QrSlot github_qr_{};
    QrSlot coffee_qr_{};
};

} // namespace screen

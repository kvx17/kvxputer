#pragma once

#include "kvx_screen.h"

#include "launch_catalog.h"

#include <vector>

namespace screen {

// Filterable window of launch targets. Only the visible rows are created, and the
// key that opened this screen cannot confirm the first row.
class ShortcutPickScreen final : public KvxScreen {
public:
    ShortcutPickScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ShortcutPickScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void rebuild();
    void move_selection(int delta);
    void activate();
    static void on_release(uint32_t key, const char* name, void* user_data);

    lv_obj_t* input_{nullptr};
    lv_obj_t* list_{nullptr};
    std::string query_;
    std::vector<const model::LaunchTarget*> shown_;
    int selected_ = 0;
    int visible_ = 5;
    // Stays false until the key that opened the picker is released.
    bool ready_ = false;
};

} // namespace screen

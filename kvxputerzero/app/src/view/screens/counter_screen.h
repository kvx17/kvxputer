#pragma once

#include "kvx_screen.h"

namespace screen {

// Hand counter: Enter +1, +10 step, persisted across runs. Reference adoption of
// the shared instrument chrome (status chip + big readout + caption + hint bar).
// (pocket-zero-tools "Counter".)
class CounterScreen final : public KvxScreen {
public:
    CounterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~CounterScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void add(int delta);
    void reset();
    void refresh();
    void load();
    void save();

    lv_obj_t* big_{nullptr};
    lv_obj_t* caption_{nullptr};
    lv_obj_t* chip_{nullptr};
    lv_obj_t* hint_{nullptr};
    long count_{0};
    int last_delta_{0};
};

} // namespace screen

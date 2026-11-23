#pragma once

#include "kvx_screen.h"

namespace screen {

enum class LifeGame {
    Yugioh = 0,
    Magic = 1,
    Truco = 2,
};

// Two-sided scoreboard for Yu-Gi-Oh, Magic, and Truco Argentino.
class LifeCounterScreen final : public KvxScreen {
public:
    LifeCounterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, LifeGame game);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void reset_scores();
    void add_to_side(int delta);
    void set_step(int step);
    void refresh();
    const char* title() const;
    const char* side_name(int index) const;

    LifeGame game_;
    int scores_[2]{0, 0};
    int poison_[2]{0, 0};
    int side_{0};
    int step_{1};
    int origin_{8000};
    int target_{30};
    bool poison_mode_{false};

    lv_obj_t* name_[2]{nullptr, nullptr};
    lv_obj_t* score_[2]{nullptr, nullptr};
    lv_obj_t* sub_[2]{nullptr, nullptr};
    lv_obj_t* status_{nullptr};
};

} // namespace screen

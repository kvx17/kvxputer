#pragma once

#include "action_bar.h"
#include "calc_key_bar.h"
#include "calc_lib.h"
#include "kvx_screen.h"

#include <array>
#include <string>

namespace screen {

class CalculatorScreen final : public KvxScreen {
public:
    CalculatorScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh_history();
    void refresh_mode();
    void evaluate_now();
    void toggle_mode();
    void insert_char(char ch);
    bool type_key(uint32_t key);

    tools::calc::State state_;
    CalcKeyBar keys_;
    lv_obj_t* input_ta_{nullptr};
    lv_obj_t* history_label_{nullptr};
    lv_obj_t* example_label_{nullptr};
};

} // namespace screen

#pragma once

#include "action_bar.h"
#include "calc_key_bar.h"
#include "eng_calc_lib.h"
#include "kvx_screen.h"

#include <array>
#include <string>

namespace screen {

class EngCalcScreen final : public KvxScreen {
public:
    EngCalcScreen(viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets,
                  tools::engcalc::Op op);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void run_now();
    void insert_char(char ch);
    bool type_key(uint32_t key);
    void scroll_result(int delta_px);
    void update_scroll_hint();
    static uint32_t map_nav_letter(uint32_t key);

    tools::engcalc::Op op_;
    CalcKeyBar keys_;
    lv_obj_t* input_ta_{nullptr};
    lv_obj_t* output_box_{nullptr};
    lv_obj_t* output_label_{nullptr};
    lv_obj_t* scroll_hint_{nullptr};
    bool result_focus_{false};
};

} // namespace screen

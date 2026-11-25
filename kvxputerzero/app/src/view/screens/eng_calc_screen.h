#pragma once

#include "action_bar.h"
#include "calc_help_modal.h"
#include "calc_key_bar.h"
#include "calc_persist.h"
#include "eng_calc_form.h"
#include "eng_calc_lib.h"
#include "kvx_screen.h"

#include <array>
#include <string>
#include <vector>

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
    std::array<view::widgets::HintIcon, 2> hint_icons() override;

private:
    void run_now();
    void insert_char(char ch);
    bool type_key(uint32_t key);
    void scroll_result(int delta_px);
    void update_scroll_hint();
    void rebuild_fields();
    void layout_below_fields();
    void focus_field(int index);
    void cycle_mode(int delta);
    void sync_values_from_ui();
    void apply_line(const std::string& line);
    void open_help();
    void open_favorites();
    void add_current_favorite();
    void reformat_active_field();
    tools::engcalc::AutoFormat active_format() const;
    lv_obj_t* active_ta() const;
    static uint32_t map_nav_letter(uint32_t key);

    tools::engcalc::Op op_;
    tools::calcpersist::OpState persist_;
    int mode_index_{0};
    int focus_{0}; // -1 = mode chip, else field index
    std::vector<std::string> values_;
    int fields_bottom_y_{20};

    CalcKeyBar keys_;
    view::widgets::CalcHelpModal help_;
    view::widgets::CalcHelpModal favorites_;

    lv_obj_t* mode_label_{nullptr};
    lv_obj_t* fields_row_{nullptr};
    std::vector<lv_obj_t*> field_tas_;
    lv_obj_t* paste_ta_{nullptr};
    lv_obj_t* output_box_{nullptr};
    lv_obj_t* output_label_{nullptr};
    lv_obj_t* scroll_hint_{nullptr};
    bool result_focus_{false};
    bool keys_built_{false};
};

} // namespace screen

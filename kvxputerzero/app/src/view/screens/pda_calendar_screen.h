#pragma once

#include "action_bar.h"
#include "kvx_screen.h"
#include "pda_lib.h"

#include <array>
#include <string>
#include <vector>

namespace screen {

class PdaCalendarScreen final : public KvxScreen {
public:
    PdaCalendarScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void redraw();
    void open_day();

    lv_obj_t* grid_{nullptr};
    lv_obj_t* cells_[32]{};
    lv_obj_t* header_label_{nullptr};
    int year_ = 2025;
    int month_ = 1;
    int day_ = 1;
    int event_count_[32]{};
};

class PdaDayScreen final : public KvxScreen {
public:
    PdaDayScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void reload();
    void refresh();
    void add_event();
    void delete_selected();

    std::vector<tools::pda::Event> day_events_;
    std::vector<tools::pda::Event> all_events_;
    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> rows_;
    int selected_ = 0;
    std::string date_;
};

} // namespace screen

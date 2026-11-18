#pragma once

#include "action_bar.h"
#include "kvx_screen.h"

#include <array>
#include <string>

namespace screen {

class PdaEditScreen final : public KvxScreen {
public:
    PdaEditScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void commit();
    bool commit_notes();
    bool commit_memos();
    bool commit_todo();
    bool commit_contacts_field();
    bool commit_alarm_step();
    bool commit_filter();
    bool commit_due();
    bool commit_calendar_event();

    lv_obj_t* input_ta_{nullptr};
    std::string title_;
    bool multiline_ = false;
};

} // namespace screen

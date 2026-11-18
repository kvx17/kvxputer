#pragma once

#include "action_bar.h"
#include "kvx_screen.h"
#include "pda_lib.h"

#include <array>
#include <string>
#include <vector>

namespace screen {

class PdaListScreen final : public KvxScreen {
public:
    PdaListScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void reload();
    void refresh();
    void move_selection(int delta);
    void activate();
    void create_new();
    void open_item_actions();

    std::vector<std::string> labels_;
    std::vector<tools::pda::NoteRef> notes_;
    std::vector<std::string> memos_;
    std::vector<tools::pda::Task> tasks_;
    std::vector<tools::pda::Contact> contacts_;
    std::vector<tools::pda::Alarm> alarms_;
    std::string filter_;

    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> rows_;
    int selected_ = 0;
};

} // namespace screen

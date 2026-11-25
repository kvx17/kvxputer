#pragma once

#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <string>

namespace screen {

class RfSettingsScreen final : public KvxScreen {
public:
    RfSettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Row {
        Region = 0,
        Preset,
        Pa,
        Step,
        RxBw,
        Threshold,
        Floor,
        Ref,
        RssiCal,
        RecordMax,
        Count,
    };
    void refresh();
    void adjust(int delta);
    void save();

    platform::RfSettings settings_;
    int row_{0};
    lv_obj_t* label_{nullptr};
};

} // namespace screen

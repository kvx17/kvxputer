#pragma once

#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <string>
#include <thread>

namespace screen {

class RfSendTestScreen final : public KvxScreen {
public:
    RfSendTestScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RfSendTestScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void refresh();
    void start_tx();
    void stop_tx();
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    bool tx_active_{false};
    std::string status_{"Idle"};
    std::string block_reason_;
    lv_obj_t* label_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

} // namespace screen

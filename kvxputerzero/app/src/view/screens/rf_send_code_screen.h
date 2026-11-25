#pragma once

#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class RfSendCodeScreen final : public KvxScreen {
public:
    RfSendCodeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RfSendCodeScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Field { Proto = 0, Bits, Key, Freq, Count };
    void refresh();
    void cycle_proto(int delta);
    void start_tx();
    void stop_tx();
    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    Field field_{Field::Proto};
    int proto_idx_{0};
    int bits_{24};
    uint64_t key_{0x55AAAA};
    std::string status_{"Enter TX"};
    bool tx_active_{false};
    bool editing_key_{false};
    std::string key_buf_;

    lv_obj_t* label_{nullptr};
    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

} // namespace screen

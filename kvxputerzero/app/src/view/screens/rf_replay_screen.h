#pragma once

#include "cc1101.h"
#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class RfReplayScreen final : public KvxScreen {
public:
    // library_mode: decode/save protocol; else replay/custom
    RfReplayScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, bool library_mode);
    ~RfReplayScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void reload();
    void refresh();
    void select_delta(int d);
    void play_selected();
    void delete_selected();
    void rename_selected();
    void decode_selected();
    void stop_tx();
    void bind_favourite_slot(size_t slot);
    void play_favourite_slot(size_t slot);
    void import_subs_from_dir(const std::string& dir);
    static void timer_cb(lv_timer_t* t);

    bool library_mode_{false};
    bool help_on_{false};
    bool renaming_{false};
    std::string rename_buf_;
    platform::RfSettings settings_;
    std::vector<std::string> names_;
    std::vector<platform::SubFileInfo> meta_;
    int index_{0};
    std::string filter_;
    int repeats_{3};
    int delay_ms_{50};
    bool override_freq_{false};
    uint32_t override_hz_{0};
    std::string status_;
    bool tx_active_{false};

    // Simple pulse editor buffer (when composing)
    std::vector<int> edit_timings_;
    bool editing_{false};

    lv_obj_t* label_{nullptr};
    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

} // namespace screen

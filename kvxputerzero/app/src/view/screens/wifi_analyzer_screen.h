#pragma once

#include "kvx_screen.h"
#include "wifi_monitor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class WifiAnalyzerScreen final : public KvxScreen {
public:
    WifiAnalyzerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~WifiAnalyzerScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Mode { Bars, ApList, Detail };

    void start_worker();
    void stop_worker();
    void on_timer();
    void draw_bars();
    void update_footer();
    void refresh_list_ui();
    void open_ap_list(uint8_t filter_ch);
    void open_detail(int ap_idx);
    void leave_mode();
    void export_csv();

    static void timer_cb(lv_timer_t* t);

    Mode mode_{Mode::Bars};
    platform::WifiAnalyzerState state_{};
    std::string status_msg_;
    std::string error_msg_;
    bool session_ok_{false};

    uint8_t list_filter_{0};
    int list_sel_{0};
    int detail_ap_{-1};
    std::vector<std::string> list_rows_;

    lv_obj_t* footer_lbl_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_obj_t* list_cont_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{320};
    int plot_h_{100};

    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> data_ready_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex data_mu_;

    // Shared with worker for channel hops
    std::atomic<uint8_t> want_ch_{1};
    std::atomic<uint16_t> want_dwell_{350};
    std::atomic<uint8_t> want_lock_{0};
};

} // namespace screen

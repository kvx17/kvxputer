#pragma once

#include "cc1101.h"
#include "kvx_screen.h"
#include "rf_settings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace screen {

class RfSpectrumScreen final : public KvxScreen {
public:
    RfSpectrumScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RfSpectrumScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class PresetId {
        Band315,
        Band433,
        Band868,
        Band915,
        Full,
        Custom,
    };

    void apply_preset(PresetId id);
    void rebuild_freqs();
    void start_worker();
    void stop_worker();
    void on_timer();
    void draw_plot();
    void update_header();
    void open_peak_menu();
    void cycle_trace_mode();
    void place_user_marker();
    void clear_user_markers();
    void pulse_alert();

    static void timer_cb(lv_timer_t* t);

    platform::RfSettings settings_;
    PresetId preset_{PresetId::Band433};
    uint32_t center_hz_{433920000};
    uint32_t span_hz_{2000000};
    uint32_t step_hz_{25000};
    uint32_t custom_start_{433000000};
    uint32_t custom_stop_{435000000};

    std::vector<uint32_t> freqs_;
    std::vector<int> rssi_;
    std::vector<int> peak_hold_;
    std::vector<int> avg_acc_;
    std::vector<int> avg_n_;
    std::vector<std::pair<uint32_t, uint32_t>> gaps_;

    enum class TraceMode { Live, Max, Avg };
    TraceMode trace_mode_{TraceMode::Live};
    bool peak_hold_on_{false};
    bool peak_hold_decay_{true};

    int peak_idx_{-1};
    int peak_dbm_{-120};
    uint32_t peak_hz_{0};

    struct Marker {
        bool set{false};
        int idx{0};
        uint32_t hz{0};
        int dbm{-120};
    };
    std::array<Marker, 3> user_markers_{};
    int next_marker_{0};

    bool threshold_alert_{false};
    uint32_t last_sweep_ms_{0};
    bool show_waterfall_{false};
    static constexpr int kWaterRows = 28;
    static constexpr int kWaterCols = 100;
    std::array<int8_t, kWaterRows * kWaterCols> water_{};

    bool cap_ok_{false};
    std::string cap_error_;
    bool demo_{false};

    lv_obj_t* header_lbl_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{320};
    int plot_h_{100};
    int water_h_{0};

    lv_timer_t* timer_{nullptr};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> data_ready_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex data_mu_;
};

} // namespace screen

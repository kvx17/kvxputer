#pragma once

#include "kvx_screen.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

// Nearby Wi-Fi networks and Bluetooth devices in one list; select one to watch
// its signal over repeated scans. (pocket-zero-tools "Radio".)
class RadioScreen final : public KvxScreen {
public:
    RadioScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RadioScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    struct Entry {
        bool wifi{true};
        std::string label;
        std::string key;   // bssid / address, stable across scans
        int rssi{-127};
    };

    static void timer_cb(lv_timer_t* t);
    void on_timer();
    void start_worker();
    void stop_worker();
    void render();
    void enter_watch();

    std::thread worker_;
    std::mutex mu_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> data_ready_{false};
    bool scan_cancel_{false};  // plain flag passed to the blocking scanners

    std::vector<Entry> entries_;
    std::string status_{"Scanning..."};
    int watch_rssi_{-127};

    lv_obj_t* header_{nullptr};
    lv_obj_t* body_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    lv_timer_t* timer_{nullptr};
    int stride_{0};
    int w_{0};
    int h_{0};

    int sel_{0};
    bool watching_{false};
    std::string watch_key_;
    std::string watch_label_;
    std::deque<int> trace_;
};

} // namespace screen

#pragma once

#include "instrument.h"
#include "kvx_screen.h"
#include "weather.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace screen {

// Weather app (Open-Meteo). A new main-menu channel. Left/Right switch category
// faces (Phase 1: Conditions, Temperature); Tab toggles Hourly vs Day; Up/Down
// switch the active saved city (Phase 2 adds the list, so Phase 1 is single-city).
class WeatherScreen final : public KvxScreen {
public:
    WeatherScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~WeatherScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static constexpr int kSlots = 6;

    static void poll_cb(lv_timer_t* t);
    void on_poll();
    void start_fetch();
    void stop_worker();
    void redraw();            // rebuild body for the current face + mode
    void update_current();    // current-conditions strip
    void update_status(const char* text, view::StatusPillKind kind);
    void cycle_city(int delta);
    const char* unit_suffix() const { return cfg_.celsius ? "C" : "F"; }

    static constexpr int kFaceCount = 8;  // Conditions, Temp, Precip, Wind, Pressure, Humidity, UV, Sun

    platform::WxConfig cfg_;
    platform::Weather wx_;
    bool daily_mode_{false};
    int face_{0};

    lv_obj_t* face_label_{nullptr};  // "< NAME >  i/8"

    // Current-conditions strip.
    lv_obj_t* city_{nullptr};
    lv_obj_t* temp_{nullptr};
    lv_obj_t* cond_icon_{nullptr};
    lv_obj_t* cond_text_{nullptr};
    lv_obj_t* sub_{nullptr};
    lv_obj_t* chip_{nullptr};
    lv_obj_t* caption_{nullptr};

    // Body: a strip of slots (Conditions) or a canvas graph (Temperature).
    std::array<lv_obj_t*, kSlots> slot_lbl_{};
    std::array<lv_obj_t*, kSlots> slot_icon_{};
    std::array<lv_obj_t*, kSlots> slot_val_{};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int cw_{0};
    int ch_{0};

    const lv_font_t* font_{nullptr};
    const lv_font_t* small_font_{nullptr};
    const lv_font_t* icon_font_{nullptr};

    // Background fetch.
    std::thread worker_;
    std::mutex mu_;
    std::atomic<bool> cancel_{false};
    bool pending_ready_{false};
    bool worker_done_{false};
    bool fetching_{false};
    int64_t last_age_shown_{-1};  // minutes-ago last written to the chip
    platform::Weather pending_;
    std::string pending_err_;
    lv_timer_t* poll_{nullptr};
};

} // namespace screen

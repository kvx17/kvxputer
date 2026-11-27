#pragma once

#include "kvx_screen.h"
#include "weather.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

// Weather setup: units toggle + the saved-city list (up to 10) with add (via
// Open-Meteo geocoding search), set-active and delete. Reached from the Weather
// screen's gear slot / 's'. Persists to the shared weather config; the Weather
// screen reloads it when it is rebuilt on return.
class WeatherSettingsScreen final : public KvxScreen {
public:
    WeatherSettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~WeatherSettingsScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    bool handle_long_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;
    const char* typing_hint() const override;

private:
    enum class Mode { Menu, AddMenu, Input, Results };
    // How the Input text is interpreted, or the source that fills Results.
    enum class Source { Online, Offline, Coords, Gps };

    static constexpr int kMaxCities = 10;
    // Menu row layout: 0 Units, 1 Refresh, then cities, then Add city.
    static constexpr int kFirstCityRow = 2;

    static void poll_cb(lv_timer_t* t);
    void on_poll();
    static void key_release_cb(uint32_t key, const char* name, void* ud);
    void on_enter_release();

    void enter_mode(Mode m);
    void rebuild();            // rebuild the row list for the current mode
    void activate();           // Enter on the selected row
    void delete_selected_city();
    void begin_delete_confirm(int city_idx);
    void confirm_delete();
    void cancel_delete();
    void move_sel(int delta);
    void add_city(const platform::WxGeoResult& r);
    void start_search();       // Online / Offline from the Input text
    void start_gps();          // GPS fix -> Results (single entry)
    void commit_coords();      // parse "lat, lon" from Input and add
    void stop_worker();

    platform::WxConfig cfg_;
    Mode mode_{Mode::Menu};
    Source source_{Source::Online};
    int sel_{0};
    std::string cities_path_;

    lv_obj_t* list_{nullptr};       // scrollable row container (Menu / AddMenu / Results)
    lv_obj_t* input_{nullptr};      // search / coords textarea
    lv_obj_t* search_hint_{nullptr};
    lv_obj_t* chip_{nullptr};
    std::vector<lv_obj_t*> rows_;

    std::vector<platform::WxGeoResult> results_;

    // Hold-Enter-to-delete: Enter on a city defers to release (short = select),
    // long-press opens a delete confirmation.
    int pending_enter_row_{-1};
    bool confirm_delete_{false};
    int delete_idx_{-1};

    const lv_font_t* font_{nullptr};
    const lv_font_t* icon_font_{nullptr};

    // Background geocoding.
    std::thread worker_;
    std::mutex mu_;
    std::atomic<bool> cancel_{false};
    bool gps_cancel_{false};  // plain flag for wait_for_fix(bool*)
    bool pending_ready_{false};
    bool searching_{false};
    std::vector<platform::WxGeoResult> pending_;
    std::string pending_err_;
    lv_timer_t* poll_{nullptr};
};

} // namespace screen

#include "weather_settings_screen.h"

#include "asset_manager.h"
#include "gps_fix.h"
#include "instrument.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>
#include <cstdlib>

namespace screen {

WeatherSettingsScreen::WeatherSettingsScreen(viewmodel::BaseViewModel& view_model,
                                             app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    cfg_ = platform::wx_load_config();
    init();
}

WeatherSettingsScreen::~WeatherSettingsScreen() {
    platform::clear_screen_key_release_listener(key_release_cb, this);
    platform::set_text_entry(false);
    stop_worker();
    if (poll_) lv_timer_delete(poll_);
}

void WeatherSettingsScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Weather setup");

    font_ = assets().load_standard_font(13);
    icon_font_ = assets().load_font("Phosphor-Fill.ttf", 14);
    cities_path_ = assets().resolve("files/cities15000.tsv").string();

    chip_ = view::widgets::make_status_chip(root, "", view::StatusPillKind::Idle,
                                            assets().load_standard_font(11), tok);

    // Scrollable row list for Menu / AddMenu / Results.
    list_ = lv_obj_create(root);
    lv_obj_remove_style_all(list_);
    lv_obj_set_pos(list_, 4, 20);
    lv_obj_set_size(list_, view::kScreenWidth - 8, view::kContentHeight - 22);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 3, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    // Search / coords input (hidden until Input mode).
    search_hint_ = lv_label_create(root);
    lv_obj_set_style_text_font(search_hint_, font_ ? font_ : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(search_hint_, tok.ink, 0);
    lv_label_set_long_mode(search_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(search_hint_, view::kScreenWidth - 16);
    lv_obj_set_pos(search_hint_, 8, 24);
    lv_obj_add_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);

    input_ = view::make_line_input(root, tok, font_, view::kScreenWidth - 16);
    lv_obj_set_pos(input_, 8, 48);
    lv_obj_add_flag(input_, LV_OBJ_FLAG_HIDDEN);

    poll_ = lv_timer_create(poll_cb, 120, this);
    platform::set_screen_key_release_listener(key_release_cb, this);
    enter_mode(Mode::Menu);
}

void WeatherSettingsScreen::enter_mode(Mode m) {
    mode_ = m;
    sel_ = 0;
    const bool input = m == Mode::Input;
    if (input) {
        platform::set_text_entry(true);
        lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(input_, LV_OBJ_FLAG_HIDDEN);
        const char* hint = "Type a city name, then Enter to search.";
        if (source_ == Source::Offline) hint = "Type a city (offline list), Enter to search.";
        else if (source_ == Source::Coords) hint = "Enter latitude, longitude  (e.g. -34.6, -58.4)";
        lv_label_set_text(search_hint_, hint);
        lv_textarea_set_text(input_, "");
        view::set_status_pill(chip_, "", view::StatusPillKind::Idle, tokens());
    } else {
        platform::set_text_entry(false);
        lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(input_, LV_OBJ_FLAG_HIDDEN);
    }
    rebuild();
}

void WeatherSettingsScreen::rebuild() {
    const auto tok = tokens();
    lv_obj_clean(list_);
    rows_.clear();
    const lv_font_t* f = font_ ? font_ : &lv_font_montserrat_14;
    char buf[96];

    if (mode_ == Mode::Menu) {
        std::snprintf(buf, sizeof(buf), "Units:  %s", cfg_.celsius ? "Celsius (C)" : "Fahrenheit (F)");
        rows_.push_back(view::make_list_row(list_, buf, view::ICON_INFO, f, icon_font_, tok));
        if (cfg_.refresh_min <= 0)
            std::snprintf(buf, sizeof(buf), "Refresh:  off");
        else
            std::snprintf(buf, sizeof(buf), "Refresh:  every %d min", cfg_.refresh_min);
        rows_.push_back(view::make_list_row(list_, buf, view::ICON_CH_CLOCK, f, icon_font_, tok));
        for (size_t i = 0; i < cfg_.cities.size(); ++i) {
            const bool active = static_cast<int>(i) == cfg_.active;
            std::snprintf(buf, sizeof(buf), "%s%s", cfg_.cities[i].name.c_str(),
                          active ? "   (active)" : "");
            rows_.push_back(view::make_list_row(list_, buf, view::ICON_WEATHER, f, icon_font_, tok));
        }
        if (static_cast<int>(cfg_.cities.size()) < kMaxCities) {
            rows_.push_back(view::make_list_row(list_, "Add city", view::ICON_PLUS, f, icon_font_, tok));
        }
    } else if (mode_ == Mode::AddMenu) {
        rows_.push_back(view::make_list_row(list_, "Search online", view::ICON_CH_WIFI, f, icon_font_, tok));
        rows_.push_back(view::make_list_row(list_, "Search offline list", view::ICON_CH_FILES, f, icon_font_, tok));
        rows_.push_back(view::make_list_row(list_, "Enter coordinates", view::ICON_COMPASS, f, icon_font_, tok));
        rows_.push_back(view::make_list_row(list_, "Use GPS", view::ICON_CH_GPS, f, icon_font_, tok));
    } else if (mode_ == Mode::Results) {
        if (results_.empty()) {
            rows_.push_back(view::make_list_row(list_, "No matches - Esc to retry", view::ICON_INFO, f,
                                                icon_font_, tok));
        } else {
            for (const auto& r : results_) {
                std::snprintf(buf, sizeof(buf), "%s", r.name.c_str());
                rows_.push_back(view::make_list_row(list_, buf, view::ICON_WEATHER, f, icon_font_, tok));
            }
        }
    }

    if (sel_ >= static_cast<int>(rows_.size())) sel_ = static_cast<int>(rows_.size()) - 1;
    if (sel_ < 0) sel_ = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
        view::apply_row_state(rows_[i], static_cast<int>(i) == sel_, tok);
    if (!rows_.empty()) lv_obj_scroll_to_view(rows_[static_cast<size_t>(sel_)], LV_ANIM_OFF);
}

void WeatherSettingsScreen::move_sel(int delta) {
    const int n = static_cast<int>(rows_.size());
    if (n <= 0) return;
    const auto tok = tokens();
    sel_ = (sel_ + delta + n) % n;
    for (int i = 0; i < n; ++i) view::apply_row_state(rows_[static_cast<size_t>(i)], i == sel_, tok);
    lv_obj_scroll_to_view(rows_[static_cast<size_t>(sel_)], LV_ANIM_OFF);
}

void WeatherSettingsScreen::add_city(const platform::WxGeoResult& r) {
    if (static_cast<int>(cfg_.cities.size()) < kMaxCities) {
        cfg_.cities.push_back({r.name, r.lat, r.lon});
        cfg_.active = static_cast<int>(cfg_.cities.size()) - 1;
        platform::wx_save_config(cfg_);
    }
    view_model().go_back();  // back to Weather, which refetches the active city
}

void WeatherSettingsScreen::activate() {
    if (mode_ == Mode::Menu) {
        const int ncities = static_cast<int>(cfg_.cities.size());
        if (sel_ == 0) {
            cfg_.celsius = !cfg_.celsius;
            platform::wx_save_config(cfg_);
            rebuild();
            return;
        }
        if (sel_ == 1) {
            // Cycle refresh interval: off -> 10 -> 15 -> 30 -> 60 -> off.
            static const int steps[] = {0, 10, 15, 30, 60};
            int idx = 0;
            for (int i = 0; i < 5; ++i)
                if (cfg_.refresh_min == steps[i]) idx = i;
            cfg_.refresh_min = steps[(idx + 1) % 5];
            platform::wx_save_config(cfg_);
            rebuild();
            return;
        }
        if (sel_ >= kFirstCityRow && sel_ < kFirstCityRow + ncities) {
            cfg_.active = sel_ - kFirstCityRow;
            platform::wx_save_config(cfg_);
            view_model().go_back();
            return;
        }
        enter_mode(Mode::AddMenu);
        return;
    }
    if (mode_ == Mode::AddMenu) {
        switch (sel_) {
        case 0: source_ = Source::Online; enter_mode(Mode::Input); break;
        case 1: source_ = Source::Offline; enter_mode(Mode::Input); break;
        case 2: source_ = Source::Coords; enter_mode(Mode::Input); break;
        default: source_ = Source::Gps; start_gps(); break;
        }
        return;
    }
    if (mode_ == Mode::Results) {
        if (results_.empty()) {
            enter_mode(Mode::AddMenu);
            return;
        }
        add_city(results_[static_cast<size_t>(sel_)]);
        return;
    }
}

void WeatherSettingsScreen::delete_selected_city() {
    if (mode_ != Mode::Menu) return;
    const int ncities = static_cast<int>(cfg_.cities.size());
    if (sel_ >= kFirstCityRow && sel_ < kFirstCityRow + ncities)
        begin_delete_confirm(sel_ - kFirstCityRow);
}

void WeatherSettingsScreen::begin_delete_confirm(int city_idx) {
    if (city_idx < 0 || city_idx >= static_cast<int>(cfg_.cities.size())) return;
    if (cfg_.cities.size() <= 1) {
        view::set_status_pill(chip_, "keep 1 city", view::StatusPillKind::Missing, tokens());
        return;  // never delete the last city
    }
    delete_idx_ = city_idx;
    confirm_delete_ = true;
    pending_enter_row_ = -1;
    platform::set_text_entry(false);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);
    char b[120];
    std::snprintf(b, sizeof(b), "Delete \"%s\"?\n\nEnter = delete    Esc = cancel",
                  cfg_.cities[static_cast<size_t>(city_idx)].name.c_str());
    lv_label_set_text(search_hint_, b);
    view::set_status_pill(chip_, "confirm", view::StatusPillKind::Error, tokens());
}

void WeatherSettingsScreen::confirm_delete() {
    confirm_delete_ = false;
    const int idx = delete_idx_;
    delete_idx_ = -1;
    if (idx >= 0 && idx < static_cast<int>(cfg_.cities.size()) && cfg_.cities.size() > 1) {
        cfg_.cities.erase(cfg_.cities.begin() + idx);
        if (cfg_.active >= static_cast<int>(cfg_.cities.size())) cfg_.active = 0;
        else if (cfg_.active > idx) --cfg_.active;
        platform::wx_save_config(cfg_);
    }
    lv_obj_add_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    view::set_status_pill(chip_, "", view::StatusPillKind::Idle, tokens());
    rebuild();
}

void WeatherSettingsScreen::cancel_delete() {
    confirm_delete_ = false;
    delete_idx_ = -1;
    lv_obj_add_flag(search_hint_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    view::set_status_pill(chip_, "", view::StatusPillKind::Idle, tokens());
    rebuild();
}

void WeatherSettingsScreen::key_release_cb(uint32_t key, const char* /*name*/, void* ud) {
    auto* self = static_cast<WeatherSettingsScreen*>(ud);
    if (self && key == LV_KEY_ENTER) self->on_enter_release();
}

void WeatherSettingsScreen::on_enter_release() {
    // A short Enter tap on a city row (no long-press, not confirming) selects it.
    if (confirm_delete_ || mode_ != Mode::Menu) {
        pending_enter_row_ = -1;
        return;
    }
    const int row = pending_enter_row_;
    pending_enter_row_ = -1;
    const int ncities = static_cast<int>(cfg_.cities.size());
    if (row >= kFirstCityRow && row < kFirstCityRow + ncities && row == sel_) {
        cfg_.active = row - kFirstCityRow;
        platform::wx_save_config(cfg_);
        view_model().go_back();
    }
}

void WeatherSettingsScreen::commit_coords() {
    const char* txt = input_ ? lv_textarea_get_text(input_) : "";
    std::string s = txt ? txt : "";
    // Accept "lat, lon" or "lat lon".
    for (char& c : s) if (c == ',') c = ' ';
    char* end = nullptr;
    const double lat = std::strtod(s.c_str(), &end);
    const double lon = std::strtod(end, &end);
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || (lat == 0.0 && lon == 0.0)) {
        view::set_status_pill(chip_, "bad coords", view::StatusPillKind::Error, tokens());
        return;
    }
    char name[48];
    std::snprintf(name, sizeof(name), "Pin %.3f, %.3f", lat, lon);
    platform::WxGeoResult r;
    r.name = name;
    r.lat = static_cast<float>(lat);
    r.lon = static_cast<float>(lon);
    add_city(r);
}

void WeatherSettingsScreen::start_search() {
    const char* q = input_ ? lv_textarea_get_text(input_) : "";
    std::string query = q ? q : "";
    if (query.empty()) return;
    stop_worker();
    cancel_ = false;
    pending_ready_ = false;
    searching_ = true;
    view::set_status_pill(chip_, "searching", view::StatusPillKind::Busy, tokens());
    const Source src = source_;
    const std::string path = cities_path_;
    worker_ = std::thread([this, query, src, path]() {
        std::string err;
        auto res = src == Source::Offline ? platform::wx_offline_search(path, query, err)
                                          : platform::wx_geocode(query, err);
        std::lock_guard<std::mutex> lock(mu_);
        pending_ = std::move(res);
        pending_err_ = std::move(err);
        pending_ready_ = true;
    });
}

void WeatherSettingsScreen::start_gps() {
    stop_worker();
    gps_cancel_ = false;
    pending_ready_ = false;
    searching_ = true;
    view::set_status_pill(chip_, "locating", view::StatusPillKind::Busy, tokens());
    worker_ = std::thread([this]() {
        // Try the Cap LoRa onboard GNSS, then the Unit GPS v1.1 on Grove.
        platform::GpsFix f =
            platform::wait_for_fix(7000, &gps_cancel_, {}, 115200, platform::GpsHardware::CapLoRa);
        if (!f.valid && !gps_cancel_)
            f = platform::wait_for_fix(7000, &gps_cancel_, {}, 9600, platform::GpsHardware::UnitGpsV11);
        std::vector<platform::WxGeoResult> res;
        if (f.valid) {
            char name[48];
            std::snprintf(name, sizeof(name), "GPS  %.3f, %.3f", f.lat, f.lon);
            platform::WxGeoResult r;
            r.name = name;
            r.lat = static_cast<float>(f.lat);
            r.lon = static_cast<float>(f.lon);
            res.push_back(r);
        }
        std::lock_guard<std::mutex> lock(mu_);
        pending_ = std::move(res);
        pending_err_ = f.valid ? "" : (f.error.empty() ? "no fix" : f.error);
        pending_ready_ = true;
    });
}

void WeatherSettingsScreen::stop_worker() {
    cancel_ = true;
    gps_cancel_ = true;
    if (worker_.joinable()) worker_.join();
    searching_ = false;
}

void WeatherSettingsScreen::poll_cb(lv_timer_t* t) {
    auto* self = static_cast<WeatherSettingsScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_poll();
}

void WeatherSettingsScreen::on_poll() {
    bool ready = false;
    std::vector<platform::WxGeoResult> res;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ready = pending_ready_;
        if (ready) {
            res = std::move(pending_);
            err = pending_err_;
            pending_ready_ = false;
        }
    }
    if (ready) {
        if (worker_.joinable()) worker_.join();
        searching_ = false;
        results_ = std::move(res);
        view::set_status_pill(chip_,
                              results_.empty() ? (err.empty() ? "no match" : err.c_str()) : "pick one",
                              results_.empty() ? view::StatusPillKind::Missing : view::StatusPillKind::Ok,
                              tokens());
        enter_mode(Mode::Results);
    }
}

bool WeatherSettingsScreen::handle_key(uint32_t key) {
    // Delete-confirmation overlay takes priority over everything.
    if (confirm_delete_) {
        if (key == LV_KEY_ENTER) {
            confirm_delete();
            return true;
        }
        if (key == LV_KEY_ESC) {
            cancel_delete();
            return true;
        }
        return true;  // swallow other keys while confirming
    }

    if (key == LV_KEY_ESC) {
        if (mode_ == Mode::Input || mode_ == Mode::Results) {
            enter_mode(Mode::AddMenu);
            return true;
        }
        if (mode_ == Mode::AddMenu) {
            enter_mode(Mode::Menu);
            return true;
        }
        return KvxScreen::handle_key(key);
    }

    if (mode_ == Mode::Input) {
        if (key == LV_KEY_ENTER) {
            if (source_ == Source::Coords) commit_coords();
            else start_search();
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == 8) {
            lv_textarea_delete_char(input_);
            return true;
        }
        if (key >= 32 && key <= 126) {
            lv_textarea_add_char(input_, key);
            return true;
        }
        return KvxScreen::handle_key(key);
    }

    // Menu / AddMenu / Results.
    if (key == LV_KEY_UP) {
        move_sel(-1);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        move_sel(1);
        return true;
    }
    if (key == LV_KEY_ENTER) {
        // On a city row, defer to release: a short tap selects, a long-press deletes.
        const int ncities = static_cast<int>(cfg_.cities.size());
        if (mode_ == Mode::Menu && sel_ >= kFirstCityRow && sel_ < kFirstCityRow + ncities) {
            pending_enter_row_ = sel_;
            return true;
        }
        activate();
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        activate();
        return true;
    }
    if ((key == 'd' || key == LV_KEY_BACKSPACE || key == 127) && mode_ == Mode::Menu) {
        delete_selected_city();
        return true;
    }
    return KvxScreen::handle_key(key);
}

bool WeatherSettingsScreen::handle_long_key(uint32_t key) {
    if (key == LV_KEY_ENTER && !confirm_delete_ && mode_ == Mode::Menu) {
        const int ncities = static_cast<int>(cfg_.cities.size());
        if (sel_ >= kFirstCityRow && sel_ < kFirstCityRow + ncities) {
            begin_delete_confirm(sel_ - kFirstCityRow);
            return true;
        }
    }
    return false;
}

const char* WeatherSettingsScreen::typing_hint() const {
    return mode_ == Mode::Input ? "Type - Enter confirm - Esc back" : nullptr;
}

std::array<view::widgets::ActionSlot, 5> WeatherSettingsScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() {
             if (mode_ == Mode::Input || mode_ == Mode::Results) enter_mode(Mode::AddMenu);
             else if (mode_ == Mode::AddMenu) enter_mode(Mode::Menu);
             else view_model().go_back();
         }},
        {view::ICON_ARROW_UP, [this]() { move_sel(-1); }},
        {view::ICON_CHECK, [this]() { activate(); }},
        {view::ICON_ARROW_DOWN, [this]() { move_sel(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

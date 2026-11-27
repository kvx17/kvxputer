#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// Open-Meteo weather for the Weather app. One forecast call carries the current
// conditions plus hourly and daily series, so face/mode switches never refetch.
// All fetch + parse is plain-HTTP (Open-Meteo answers on http://, no key).

struct WxHour {
    int64_t time{0};        // unix seconds
    float temp{0.f};        // temperature_2m
    float precip_prob{0.f}; // precipitation_probability %
    float precip{0.f};      // precipitation amount (mm/inch)
    float wind_speed{0.f};
    float wind_dir{0.f};
    float pressure{0.f};    // pressure_msl
    float humidity{0.f};    // relative_humidity_2m %
    float uv{0.f};          // uv_index
    int weather_code{-1};
};

struct WxDay {
    int64_t time{0};        // unix seconds (local midnight)
    float t_max{0.f};
    float t_min{0.f};
    float precip_prob{0.f}; // precipitation_probability_max %
    float precip_sum{0.f};  // precipitation_sum
    float wind_max{0.f};    // wind_speed_10m_max
    float wind_dir{0.f};    // wind_direction_10m_dominant
    float uv_max{0.f};      // uv_index_max
    int weather_code{-1};
    int64_t sunrise{0};
    int64_t sunset{0};
};

struct Weather {
    bool valid{false};
    int64_t fetched_at{0};   // unix seconds when this data was fetched
    int utc_offset{0};       // utc_offset_seconds for local time rendering

    // Current conditions.
    float temp{0.f};
    float apparent{0.f};
    float humidity{0.f};
    float wind_speed{0.f};
    float wind_dir{0.f};
    float precip{0.f};
    float pressure{0.f};  // pressure_msl
    int weather_code{-1};
    bool is_day{true};

    std::vector<WxHour> hourly;
    std::vector<WxDay> daily;
};

// A saved location.
struct WxCity {
    std::string name;
    float lat{0.f};
    float lon{0.f};
};

// A geocoding search match.
struct WxGeoResult {
    std::string name;     // "City, Country" display + storage name
    float lat{0.f};
    float lon{0.f};
};

// Open-Meteo geocoding search. Returns up to 10 matches (small towns included).
// Blocking: call from a worker thread. Empty with `error` set on failure.
std::vector<WxGeoResult> wx_geocode(const std::string& query, std::string& error);

// Offline city search over the bundled GeoNames cities15000 dataset (TSV:
// name<TAB>lat<TAB>lon<TAB>country<TAB>population). Returns up to 10 matches,
// most-populous first. Blocking (file scan): call from a worker thread.
std::vector<WxGeoResult> wx_offline_search(const std::string& dataset_path, const std::string& query,
                                           std::string& error);

// Fetch current + hourly + daily for a coordinate. `celsius` selects the unit
// params. Returns valid=false with `error` set on failure. Blocking: call from a
// worker thread.
Weather fetch_weather(float lat, float lon, bool celsius, std::string& error);

// Parse a raw Open-Meteo forecast JSON body (as fetch_weather received it).
// Exposed so a cached body can be shown instantly without a network round-trip.
Weather wx_parse(const std::string& body, std::string& error);

// WMO weather-code helpers (reproduces the pocket-zero/zero-weather describe()).
const char* wx_condition_text(int code);
// Phosphor-Fill glyph for a code + day/night. Returned as a UTF-8 string.
const char* wx_condition_icon(int code, bool is_day);

// Config persistence (~/.config/kvxputerzero/weather_*). Phase 1 stores a single
// active city + unit; later phases extend the city list.
struct WxConfig {
    std::vector<WxCity> cities;
    int active{0};
    bool celsius{true};
    int refresh_min{15};  // auto-refresh interval in minutes
};
WxConfig wx_load_config();
void wx_save_config(const WxConfig& cfg);

// Cache the last good fetch for a city key so reopening shows data instantly and
// offline. Keyed by rounded lat/lon.
void wx_save_cache(float lat, float lon, const std::string& raw_json);
std::string wx_load_cache(float lat, float lon);

} // namespace platform

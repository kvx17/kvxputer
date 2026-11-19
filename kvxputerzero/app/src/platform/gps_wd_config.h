#pragma once

#include <string>
#include <vector>

namespace platform {

struct WdGeofenceBox {
    float top_lat{0};
    float bottom_lat{0};
    float left_lon{0};
    float right_lon{0};
};

struct WdConfig {
    int min_sats{4};
    float accuracy_threshold_m{500.0f};
    int gmt_offset_hours{0};
    std::string gps_log_mode{"zero_gps"}; // fix_only | zero_gps | last_known

    std::string scan_mode{"active_all"};
    int active_dwell_ms{150};
    int passive_dwell_ms{250};
    int scan_delay_ms{150};
    int mac_randomize_interval{1};
    bool mac_spoof_oui{false};
    std::string wifi_country_code{"US"};
    int wifi_tx_power{80};

    std::vector<std::string> excluded_ssids;
    std::vector<std::string> excluded_bssids;
    std::vector<WdGeofenceBox> geofence_boxes;

    bool beep_on_new{false};
    bool beep_on_blocked{false};
    bool beep_on_geofence{false};
    int low_battery_pct{5};
    bool stats_enabled{false};
    int stats_interval_s{300};

    bool auto_upload_on_launch{false};
    std::string upload_ssid;
    std::string upload_password;
    bool compress{true};
    bool delete_after_upload{false};
    int min_sweeps{20};
};

bool wd_config_load(const std::string& path, WdConfig& c);
bool wd_config_save(const std::string& path, const WdConfig& c);

bool wd_ssid_excluded(const WdConfig& c, const std::string& ssid);
bool wd_bssid_excluded(const WdConfig& c, const std::string& bssid);
// True when lat/lon falls inside any exclusion box (ADV semantics: inside = blocked).
bool wd_in_geofence(const WdConfig& c, double lat, double lon);
bool wd_allow_log(const WdConfig& c, const std::string& ssid, const std::string& bssid, double lat, double lon,
                  bool skip_geofence);

} // namespace platform

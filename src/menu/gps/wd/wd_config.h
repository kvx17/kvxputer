#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

struct WdGeofenceBox {
    float top_lat = 0;
    float bottom_lat = 0;
    float left_lon = 0;
    float right_lon = 0;
};

struct WdConfig {
    // gps
    int min_sats = 4;
    float accuracy_threshold_m = 500.0f;
    int gmt_offset_hours = 0;
    // zero_gps: always log (0,0 until fix). fix_only: wait for GPS (can yield empty CSV).
    String gps_log_mode = "zero_gps"; // fix_only | zero_gps | last_known

    // scan
    String scan_mode = "active_all";
    int active_dwell_ms = 150;
    int passive_dwell_ms = 250;
    int scan_delay_ms = 150;
    int mac_randomize_interval = 1;
    bool mac_spoof_oui = false;
    String wifi_country_code = "US";
    int wifi_tx_power = 80;

    // filter
    std::vector<String> excluded_ssids;
    std::vector<String> excluded_bssids;
    std::vector<WdGeofenceBox> geofence_boxes;

    // session
    bool beep_on_new = false;
    bool beep_on_blocked = false;
    bool beep_on_geofence = false;
    int low_battery_pct = 5;
    bool stats_enabled = false;
    int stats_interval_s = 300;

    // upload
    bool auto_upload_on_launch = false;
    String upload_ssid;
    String upload_password;
    bool compress = true;
    bool delete_after_upload = false;
    int min_sweeps = 20;
};

namespace wd_config {
constexpr const char *FILENAME = "wardriverconfig.txt";

void setDefaults(WdConfig &c);
bool load(FS &fs, const char *dir, WdConfig &c);
bool save(FS &fs, const char *dir, const WdConfig &c);
String configPath(const char *dir);
} // namespace wd_config

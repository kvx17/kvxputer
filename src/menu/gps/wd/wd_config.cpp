#include "wd_config.h"
#include <ArduinoJson.h>

namespace wd_config {

void setDefaults(WdConfig &c) { c = WdConfig{}; }

String configPath(const char *dir) { return String(dir) + "/" + FILENAME; }

bool load(FS &fs, const char *dir, WdConfig &c) {
    setDefaults(c);
    String path = configPath(dir);
    if (!fs.exists(path)) {
        save(fs, dir, c);
        return true;
    }
    File f = fs.open(path, FILE_READ);
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    bool migrated = false;
    if (JsonObject gps = doc["gps"].as<JsonObject>()) {
        if (!gps["min_sats"].isNull()) c.min_sats = gps["min_sats"].as<int>();
        if (!gps["accuracy_threshold_m"].isNull()) c.accuracy_threshold_m = gps["accuracy_threshold_m"].as<float>();
        if (!gps["gmt_offset_hours"].isNull()) c.gmt_offset_hours = gps["gmt_offset_hours"].as<int>();
        if (!gps["gps_log_mode"].isNull()) c.gps_log_mode = gps["gps_log_mode"].as<String>();
        migrated = gps["log_mode_migrated_v2"] | false;
    }
    if (JsonObject scan = doc["scan"].as<JsonObject>()) {
        if (!scan["scan_mode"].isNull()) c.scan_mode = scan["scan_mode"].as<String>();
        if (!scan["active_dwell_ms"].isNull()) c.active_dwell_ms = scan["active_dwell_ms"].as<int>();
        if (!scan["passive_dwell_ms"].isNull()) c.passive_dwell_ms = scan["passive_dwell_ms"].as<int>();
        if (!scan["scan_delay_ms"].isNull()) c.scan_delay_ms = scan["scan_delay_ms"].as<int>();
        if (!scan["mac_randomize_interval"].isNull())
            c.mac_randomize_interval = scan["mac_randomize_interval"].as<int>();
        if (!scan["mac_spoof_oui"].isNull()) c.mac_spoof_oui = scan["mac_spoof_oui"].as<bool>();
        if (!scan["wifi_country_code"].isNull()) c.wifi_country_code = scan["wifi_country_code"].as<String>();
        if (!scan["wifi_tx_power"].isNull()) c.wifi_tx_power = scan["wifi_tx_power"].as<int>();
    }
    if (JsonObject filter = doc["filter"].as<JsonObject>()) {
        c.excluded_ssids.clear();
        c.excluded_bssids.clear();
        c.geofence_boxes.clear();
        if (JsonArray ssids = filter["excluded_ssids"].as<JsonArray>()) {
            for (JsonVariant v : ssids) c.excluded_ssids.push_back(v.as<String>());
        }
        if (JsonArray bssids = filter["excluded_bssids"].as<JsonArray>()) {
            for (JsonVariant v : bssids) c.excluded_bssids.push_back(v.as<String>());
        }
        if (JsonArray boxes = filter["geofence_boxes"].as<JsonArray>()) {
            for (JsonVariant v : boxes) {
                if (c.geofence_boxes.size() >= 10) break;
                JsonObject o = v.as<JsonObject>();
                WdGeofenceBox b;
                b.top_lat = o["top_lat"] | 0.0f;
                b.bottom_lat = o["bottom_lat"] | 0.0f;
                b.left_lon = o["left_lon"] | 0.0f;
                b.right_lon = o["right_lon"] | 0.0f;
                c.geofence_boxes.push_back(b);
            }
        }
    }
    if (JsonObject session = doc["session"].as<JsonObject>()) {
        if (!session["beep_on_new"].isNull()) c.beep_on_new = session["beep_on_new"].as<bool>();
        if (!session["beep_on_blocked"].isNull()) c.beep_on_blocked = session["beep_on_blocked"].as<bool>();
        if (!session["beep_on_geofence"].isNull()) c.beep_on_geofence = session["beep_on_geofence"].as<bool>();
        if (!session["low_battery_pct"].isNull()) c.low_battery_pct = session["low_battery_pct"].as<int>();
        if (!session["stats_enabled"].isNull()) c.stats_enabled = session["stats_enabled"].as<bool>();
        if (!session["stats_interval_s"].isNull()) c.stats_interval_s = session["stats_interval_s"].as<int>();
    }
    if (JsonObject upload = doc["upload"].as<JsonObject>()) {
        if (!upload["auto_upload_on_launch"].isNull())
            c.auto_upload_on_launch = upload["auto_upload_on_launch"].as<bool>();
        if (!upload["upload_ssid"].isNull()) c.upload_ssid = upload["upload_ssid"].as<String>();
        if (!upload["upload_password"].isNull()) c.upload_password = upload["upload_password"].as<String>();
        if (!upload["compress"].isNull()) c.compress = upload["compress"].as<bool>();
        if (!upload["delete_after_upload"].isNull())
            c.delete_after_upload = upload["delete_after_upload"].as<bool>();
        if (!upload["min_sweeps"].isNull()) c.min_sweeps = upload["min_sweeps"].as<int>();
    }

    if (c.min_sats < 1) c.min_sats = 1;
    if (c.active_dwell_ms < 10) c.active_dwell_ms = 10;
    if (c.active_dwell_ms > 2000) c.active_dwell_ms = 2000;
    if (c.passive_dwell_ms < 10) c.passive_dwell_ms = 10;
    if (c.passive_dwell_ms > 2000) c.passive_dwell_ms = 2000;
    if (c.scan_delay_ms < 50) c.scan_delay_ms = 50;
    if (c.wifi_tx_power < 8) c.wifi_tx_power = 8;
    if (c.wifi_tx_power > 84) c.wifi_tx_power = 84;
    if (c.gmt_offset_hours < -12) c.gmt_offset_hours = -12;
    if (c.gmt_offset_hours > 14) c.gmt_offset_hours = 14;

    // Older default fix_only produced header-only CSVs without a GPS fix.
    // Migrate once to zero_gps; user can switch back in WD Config.
    if (!migrated && c.gps_log_mode == "fix_only") {
        c.gps_log_mode = "zero_gps";
        save(fs, dir, c);
    } else if (!migrated) {
        save(fs, dir, c); // stamp log_mode_migrated_v2
    }
    return true;
}

bool save(FS &fs, const char *dir, const WdConfig &c) {
    if (!fs.exists(dir)) fs.mkdir(dir);
    String path = configPath(dir);
    File f = fs.open(path, FILE_WRITE);
    if (!f) return false;

    JsonDocument doc;
    JsonObject gps = doc["gps"].to<JsonObject>();
    gps["min_sats"] = c.min_sats;
    gps["accuracy_threshold_m"] = c.accuracy_threshold_m;
    gps["gmt_offset_hours"] = c.gmt_offset_hours;
    gps["gps_log_mode"] = c.gps_log_mode;
    gps["log_mode_migrated_v2"] = true;

    JsonObject scan = doc["scan"].to<JsonObject>();
    scan["scan_mode"] = c.scan_mode;
    scan["active_dwell_ms"] = c.active_dwell_ms;
    scan["passive_dwell_ms"] = c.passive_dwell_ms;
    scan["scan_delay_ms"] = c.scan_delay_ms;
    scan["mac_randomize_interval"] = c.mac_randomize_interval;
    scan["mac_spoof_oui"] = c.mac_spoof_oui;
    scan["wifi_country_code"] = c.wifi_country_code;
    scan["wifi_tx_power"] = c.wifi_tx_power;

    JsonObject filter = doc["filter"].to<JsonObject>();
    JsonArray ssids = filter["excluded_ssids"].to<JsonArray>();
    for (const auto &s : c.excluded_ssids) ssids.add(s);
    JsonArray bssids = filter["excluded_bssids"].to<JsonArray>();
    for (const auto &s : c.excluded_bssids) bssids.add(s);
    JsonArray boxes = filter["geofence_boxes"].to<JsonArray>();
    for (const auto &b : c.geofence_boxes) {
        JsonObject o = boxes.add<JsonObject>();
        o["top_lat"] = b.top_lat;
        o["bottom_lat"] = b.bottom_lat;
        o["left_lon"] = b.left_lon;
        o["right_lon"] = b.right_lon;
    }

    JsonObject session = doc["session"].to<JsonObject>();
    session["beep_on_new"] = c.beep_on_new;
    session["beep_on_blocked"] = c.beep_on_blocked;
    session["beep_on_geofence"] = c.beep_on_geofence;
    session["low_battery_pct"] = c.low_battery_pct;
    session["stats_enabled"] = c.stats_enabled;
    session["stats_interval_s"] = c.stats_interval_s;

    JsonObject upload = doc["upload"].to<JsonObject>();
    upload["auto_upload_on_launch"] = c.auto_upload_on_launch;
    upload["upload_ssid"] = c.upload_ssid;
    upload["upload_password"] = c.upload_password;
    upload["compress"] = c.compress;
    upload["delete_after_upload"] = c.delete_after_upload;
    upload["min_sweeps"] = c.min_sweeps;

    bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

} // namespace wd_config

#include "gps_wd_config.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace platform {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool extract_section(const std::string& text, const char* key, std::string& body) {
    const std::string needle = std::string("\"") + key + "\"";
    auto p = text.find(needle);
    if (p == std::string::npos) return false;
    p = text.find('{', p);
    if (p == std::string::npos) return false;
    int depth = 0;
    for (size_t i = p; i < text.size(); ++i) {
        if (text[i] == '{') depth++;
        else if (text[i] == '}') {
            depth--;
            if (depth == 0) {
                body = text.substr(p, i - p + 1);
                return true;
            }
        }
    }
    return false;
}

bool grab_string(const std::string& section, const char* key, std::string& out) {
    const std::string needle = std::string("\"") + key + "\"";
    auto p = section.find(needle);
    if (p == std::string::npos) return false;
    p = section.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < section.size() && std::isspace(static_cast<unsigned char>(section[p]))) ++p;
    if (p >= section.size() || section[p] != '"') return false;
    ++p;
    std::string v;
    while (p < section.size() && section[p] != '"') {
        if (section[p] == '\\' && p + 1 < section.size()) {
            v.push_back(section[p + 1]);
            p += 2;
            continue;
        }
        v.push_back(section[p++]);
    }
    out = v;
    return true;
}

bool grab_number(const std::string& section, const char* key, double& out) {
    const std::string needle = std::string("\"") + key + "\"";
    auto p = section.find(needle);
    if (p == std::string::npos) return false;
    p = section.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < section.size() && std::isspace(static_cast<unsigned char>(section[p]))) ++p;
    try {
        out = std::stod(section.substr(p));
        return true;
    } catch (...) {
        return false;
    }
}

bool grab_bool(const std::string& section, const char* key, bool& out) {
    const std::string needle = std::string("\"") + key + "\"";
    auto p = section.find(needle);
    if (p == std::string::npos) return false;
    p = section.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < section.size() && std::isspace(static_cast<unsigned char>(section[p]))) ++p;
    if (section.compare(p, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (section.compare(p, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

void grab_string_array(const std::string& section, const char* key, std::vector<std::string>& out) {
    out.clear();
    const std::string needle = std::string("\"") + key + "\"";
    auto p = section.find(needle);
    if (p == std::string::npos) return;
    p = section.find('[', p);
    if (p == std::string::npos) return;
    const auto end = section.find(']', p);
    if (end == std::string::npos) return;
    const std::string arr = section.substr(p + 1, end - p - 1);
    size_t i = 0;
    while (i < arr.size()) {
        while (i < arr.size() && arr[i] != '"') ++i;
        if (i >= arr.size()) break;
        ++i;
        std::string v;
        while (i < arr.size() && arr[i] != '"') {
            if (arr[i] == '\\' && i + 1 < arr.size()) {
                v.push_back(arr[i + 1]);
                i += 2;
                continue;
            }
            v.push_back(arr[i++]);
        }
        if (i < arr.size() && arr[i] == '"') ++i;
        out.push_back(v);
    }
}

void grab_geofence(const std::string& section, std::vector<WdGeofenceBox>& boxes) {
    boxes.clear();
    const std::string needle = "\"geofence_boxes\"";
    auto p = section.find(needle);
    if (p == std::string::npos) return;
    p = section.find('[', p);
    if (p == std::string::npos) return;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = p; i < section.size(); ++i) {
        if (section[i] == '[') {
            if (depth == 0) start = i;
            depth++;
        } else if (section[i] == ']') {
            depth--;
            if (depth == 0 && start != std::string::npos) {
                const std::string arr = section.substr(start + 1, i - start - 1);
                size_t o = 0;
                while (o < arr.size() && boxes.size() < 10) {
                    auto ob = arr.find('{', o);
                    if (ob == std::string::npos) break;
                    auto oe = arr.find('}', ob);
                    if (oe == std::string::npos) break;
                    const std::string obj = arr.substr(ob, oe - ob + 1);
                    WdGeofenceBox b;
                    double v = 0;
                    if (grab_number(obj, "top_lat", v)) b.top_lat = static_cast<float>(v);
                    if (grab_number(obj, "bottom_lat", v)) b.bottom_lat = static_cast<float>(v);
                    if (grab_number(obj, "left_lon", v)) b.left_lon = static_cast<float>(v);
                    if (grab_number(obj, "right_lon", v)) b.right_lon = static_cast<float>(v);
                    boxes.push_back(b);
                    o = oe + 1;
                }
                return;
            }
        }
    }
}

} // namespace

bool wd_config_load(const std::string& path, WdConfig& c) {
    c = WdConfig{};
    std::ifstream in(path);
    if (!in) return true; // missing file -> defaults
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return true;

    std::string gps, scan, filter, session, upload;
    if (extract_section(text, "gps", gps)) {
        double v = 0;
        if (grab_number(gps, "min_sats", v)) c.min_sats = static_cast<int>(v);
        if (grab_number(gps, "accuracy_threshold_m", v)) c.accuracy_threshold_m = static_cast<float>(v);
        if (grab_number(gps, "gmt_offset_hours", v)) c.gmt_offset_hours = static_cast<int>(v);
        grab_string(gps, "gps_log_mode", c.gps_log_mode);
    }
    if (extract_section(text, "scan", scan)) {
        double v = 0;
        grab_string(scan, "scan_mode", c.scan_mode);
        if (grab_number(scan, "active_dwell_ms", v)) c.active_dwell_ms = static_cast<int>(v);
        if (grab_number(scan, "passive_dwell_ms", v)) c.passive_dwell_ms = static_cast<int>(v);
        if (grab_number(scan, "scan_delay_ms", v)) c.scan_delay_ms = static_cast<int>(v);
        if (grab_number(scan, "mac_randomize_interval", v)) c.mac_randomize_interval = static_cast<int>(v);
        grab_bool(scan, "mac_spoof_oui", c.mac_spoof_oui);
        grab_string(scan, "wifi_country_code", c.wifi_country_code);
        if (grab_number(scan, "wifi_tx_power", v)) c.wifi_tx_power = static_cast<int>(v);
    }
    if (extract_section(text, "filter", filter)) {
        grab_string_array(filter, "excluded_ssids", c.excluded_ssids);
        grab_string_array(filter, "excluded_bssids", c.excluded_bssids);
        grab_geofence(filter, c.geofence_boxes);
    }
    if (extract_section(text, "session", session)) {
        double v = 0;
        grab_bool(session, "beep_on_new", c.beep_on_new);
        grab_bool(session, "beep_on_blocked", c.beep_on_blocked);
        grab_bool(session, "beep_on_geofence", c.beep_on_geofence);
        if (grab_number(session, "low_battery_pct", v)) c.low_battery_pct = static_cast<int>(v);
        grab_bool(session, "stats_enabled", c.stats_enabled);
        if (grab_number(session, "stats_interval_s", v)) c.stats_interval_s = static_cast<int>(v);
    }
    if (extract_section(text, "upload", upload)) {
        double v = 0;
        grab_bool(upload, "auto_upload_on_launch", c.auto_upload_on_launch);
        grab_string(upload, "upload_ssid", c.upload_ssid);
        grab_string(upload, "upload_password", c.upload_password);
        grab_bool(upload, "compress", c.compress);
        grab_bool(upload, "delete_after_upload", c.delete_after_upload);
        if (grab_number(upload, "min_sweeps", v)) c.min_sweeps = static_cast<int>(v);
    }

    if (c.min_sats < 1) c.min_sats = 1;
    if (c.active_dwell_ms < 10) c.active_dwell_ms = 10;
    if (c.passive_dwell_ms < 10) c.passive_dwell_ms = 10;
    if (c.scan_delay_ms < 50) c.scan_delay_ms = 50;
    if (c.gmt_offset_hours < -12) c.gmt_offset_hours = -12;
    if (c.gmt_offset_hours > 14) c.gmt_offset_hours = 14;
    return true;
}

bool wd_config_save(const std::string& path, const WdConfig& c) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"gps\": {\n";
    o << "    \"min_sats\": " << c.min_sats << ",\n";
    o << "    \"accuracy_threshold_m\": " << c.accuracy_threshold_m << ",\n";
    o << "    \"gmt_offset_hours\": " << c.gmt_offset_hours << ",\n";
    o << "    \"gps_log_mode\": \"" << json_escape(c.gps_log_mode) << "\",\n";
    o << "    \"log_mode_migrated_v2\": true\n";
    o << "  },\n";
    o << "  \"scan\": {\n";
    o << "    \"scan_mode\": \"" << json_escape(c.scan_mode) << "\",\n";
    o << "    \"active_dwell_ms\": " << c.active_dwell_ms << ",\n";
    o << "    \"passive_dwell_ms\": " << c.passive_dwell_ms << ",\n";
    o << "    \"scan_delay_ms\": " << c.scan_delay_ms << ",\n";
    o << "    \"mac_randomize_interval\": " << c.mac_randomize_interval << ",\n";
    o << "    \"mac_spoof_oui\": " << (c.mac_spoof_oui ? "true" : "false") << ",\n";
    o << "    \"wifi_country_code\": \"" << json_escape(c.wifi_country_code) << "\",\n";
    o << "    \"wifi_tx_power\": " << c.wifi_tx_power << "\n";
    o << "  },\n";
    o << "  \"filter\": {\n";
    o << "    \"excluded_ssids\": [";
    for (size_t i = 0; i < c.excluded_ssids.size(); ++i) {
        if (i) o << ", ";
        o << "\"" << json_escape(c.excluded_ssids[i]) << "\"";
    }
    o << "],\n";
    o << "    \"excluded_bssids\": [";
    for (size_t i = 0; i < c.excluded_bssids.size(); ++i) {
        if (i) o << ", ";
        o << "\"" << json_escape(c.excluded_bssids[i]) << "\"";
    }
    o << "],\n";
    o << "    \"geofence_boxes\": [";
    for (size_t i = 0; i < c.geofence_boxes.size(); ++i) {
        if (i) o << ", ";
        const auto& b = c.geofence_boxes[i];
        o << "{\"top_lat\":" << b.top_lat << ",\"bottom_lat\":" << b.bottom_lat << ",\"left_lon\":" << b.left_lon
          << ",\"right_lon\":" << b.right_lon << "}";
    }
    o << "]\n";
    o << "  },\n";
    o << "  \"session\": {\n";
    o << "    \"beep_on_new\": " << (c.beep_on_new ? "true" : "false") << ",\n";
    o << "    \"beep_on_blocked\": " << (c.beep_on_blocked ? "true" : "false") << ",\n";
    o << "    \"beep_on_geofence\": " << (c.beep_on_geofence ? "true" : "false") << ",\n";
    o << "    \"low_battery_pct\": " << c.low_battery_pct << ",\n";
    o << "    \"stats_enabled\": " << (c.stats_enabled ? "true" : "false") << ",\n";
    o << "    \"stats_interval_s\": " << c.stats_interval_s << "\n";
    o << "  },\n";
    o << "  \"upload\": {\n";
    o << "    \"auto_upload_on_launch\": " << (c.auto_upload_on_launch ? "true" : "false") << ",\n";
    o << "    \"upload_ssid\": \"" << json_escape(c.upload_ssid) << "\",\n";
    o << "    \"upload_password\": \"" << json_escape(c.upload_password) << "\",\n";
    o << "    \"compress\": " << (c.compress ? "true" : "false") << ",\n";
    o << "    \"delete_after_upload\": " << (c.delete_after_upload ? "true" : "false") << ",\n";
    o << "    \"min_sweeps\": " << c.min_sweeps << "\n";
    o << "  }\n";
    o << "}\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << o.str();
    out.close();
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
    return true;
}

bool wd_ssid_excluded(const WdConfig& c, const std::string& ssid) {
    for (const auto& s : c.excluded_ssids) {
        if (s == ssid) return true;
    }
    return false;
}

bool wd_bssid_excluded(const WdConfig& c, const std::string& bssid) {
    const std::string low = lower(bssid);
    for (const auto& s : c.excluded_bssids) {
        if (lower(s) == low) return true;
    }
    return false;
}

bool wd_in_geofence(const WdConfig& c, double lat, double lon) {
    for (const auto& b : c.geofence_boxes) {
        const float north = b.top_lat >= b.bottom_lat ? b.top_lat : b.bottom_lat;
        const float south = b.top_lat >= b.bottom_lat ? b.bottom_lat : b.top_lat;
        const float west = b.left_lon <= b.right_lon ? b.left_lon : b.right_lon;
        const float east = b.left_lon <= b.right_lon ? b.right_lon : b.left_lon;
        if (lat <= north && lat >= south && lon >= west && lon <= east) return true;
    }
    return false;
}

bool wd_allow_log(const WdConfig& c, const std::string& ssid, const std::string& bssid, double lat, double lon,
                  bool skip_geofence) {
    if (!ssid.empty() && wd_ssid_excluded(c, ssid)) return false;
    if (!bssid.empty() && wd_bssid_excluded(c, bssid)) return false;
    if (!skip_geofence && wd_in_geofence(c, lat, lon)) return false;
    return true;
}

} // namespace platform

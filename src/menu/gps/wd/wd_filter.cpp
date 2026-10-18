#include "wd_filter.h"

void WdFilter::load(const WdConfig &cfg) {
    excludedSsids = cfg.excluded_ssids;
    excludedBssids.clear();
    for (auto s : cfg.excluded_bssids) {
        s.toLowerCase();
        excludedBssids.push_back(s);
    }
    boxes = cfg.geofence_boxes;
}

bool WdFilter::isSsidExcluded(const String &ssid) const {
    for (const auto &s : excludedSsids) {
        if (s == ssid) return true;
    }
    return false;
}

bool WdFilter::isBssidExcluded(const String &bssid) const {
    String lower = bssid;
    lower.toLowerCase();
    for (const auto &s : excludedBssids) {
        if (s == lower) return true;
    }
    return false;
}

bool WdFilter::inGeofence(double lat, double lng) const {
    for (const auto &b : boxes) {
        const float north = b.top_lat >= b.bottom_lat ? b.top_lat : b.bottom_lat;
        const float south = b.top_lat >= b.bottom_lat ? b.bottom_lat : b.top_lat;
        const float west = b.left_lon <= b.right_lon ? b.left_lon : b.right_lon;
        const float east = b.left_lon <= b.right_lon ? b.right_lon : b.left_lon;
        if (lat <= north && lat >= south && lng >= west && lng <= east) return true;
    }
    return false;
}

bool WdFilter::allowLog(
    const String &ssid, const String &bssid, double lat, double lng, bool skipGeofence
) const {
    if (ssid.length() && isSsidExcluded(ssid)) return false;
    if (bssid.length() && isBssidExcluded(bssid)) return false;
    if (!skipGeofence && inGeofence(lat, lng)) return false;
    return true;
}

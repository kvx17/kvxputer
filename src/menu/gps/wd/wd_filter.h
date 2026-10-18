#pragma once

#include "wd_config.h"
#include <Arduino.h>
#include <vector>

class WdFilter {
public:
    void load(const WdConfig &cfg);

    bool isSsidExcluded(const String &ssid) const;
    bool isBssidExcluded(const String &bssid) const;
    bool inGeofence(double lat, double lng) const;
    // Returns true if the sighting should be written to CSV.
    bool allowLog(const String &ssid, const String &bssid, double lat, double lng, bool skipGeofence) const;

    size_t ssidCount() const { return excludedSsids.size(); }
    size_t bssidCount() const { return excludedBssids.size(); }
    size_t geofenceCount() const { return boxes.size(); }

private:
    std::vector<String> excludedSsids;
    std::vector<String> excludedBssids; // lowercase
    std::vector<WdGeofenceBox> boxes;
};

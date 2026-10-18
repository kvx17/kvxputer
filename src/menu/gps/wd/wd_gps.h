#pragma once

#include "wd_config.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

struct WdGpsFix {
    double lat = 0;
    double lng = 0;
    float altitude = 0;
    float accuracy_m = 999.0f;
    int satellites = 0;
    bool location_valid = false;
    bool has_3d_fix = false;
    bool accuracy_ok = false;
    bool date_valid = false;
    bool time_valid = false;
    String datetime; // "YYYY-MM-DD HH:MM:SS" UTC for CSV
    uint32_t age_ms = 0xFFFFFFFF;
};

class WdGps {
public:
    bool begin();
    void end();
    void pump();

    void applyConfig(const WdConfig &cfg);

    WdGpsFix getFix();
    bool resolveLogPosition(WdGpsFix &out);
    bool canLog();

    bool syncSystemTime();
    bool sentencesStale(uint32_t maxGapMs = 15000) const;
    uint32_t msSinceSentence() const;

    double distanceMeters(double lat1, double lng1, double lat2, double lng2) const;

    bool pinConflictReleased() const { return rxPinReleased; }

private:
    TinyGPSPlus gps;
    HardwareSerial serial = HardwareSerial(2);
    bool started = false;
    bool rxPinReleased = false;
    bool everHadFix = false;
    bool timeSynced = false;
    uint32_t lastSentenceMs = 0;
    uint32_t timeLastSyncedMs = 0;
    int minSats = 4;
    float accuracyThresholdM = 500.0f;
    int gmtOffsetHours = 0;
    String logMode = "fix_only";
    WdGpsFix lastKnown;

    void releasePins();
    void restorePins();
    float accuracyMeters();
    String formatUtcDateTime();
};

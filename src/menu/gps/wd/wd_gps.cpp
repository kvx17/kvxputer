#include "wd_gps.h"
#include <globals.h>
#include <sys/time.h>
#include <time.h>

static constexpr float HDOP_TO_METERS = 2.5f;

void WdGps::applyConfig(const WdConfig &cfg) {
    minSats = cfg.min_sats;
    accuracyThresholdM = cfg.accuracy_threshold_m;
    gmtOffsetHours = cfg.gmt_offset_hours;
    logMode = cfg.gps_log_mode;
}

void WdGps::releasePins() {
    rxPinReleased = false;
    if (kvxConfigPins.CC1101_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
        kvxConfigPins.W5500_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.LoRa_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#endif
        kvxConfigPins.SDCARD_bus.checkConflict(kvxConfigPins.gps_bus.rx)) {
        pinMode(kvxConfigPins.gps_bus.rx, INPUT);
        rxPinReleased = true;
    }
}

void WdGps::restorePins() {
    if (!rxPinReleased) return;
    if (kvxConfigPins.CC1101_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.NRF24_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
        kvxConfigPins.W5500_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
        kvxConfigPins.LoRa_bus.checkConflict(kvxConfigPins.gps_bus.rx) ||
#endif
        kvxConfigPins.SDCARD_bus.checkConflict(kvxConfigPins.gps_bus.rx)) {
        pinMode(kvxConfigPins.gps_bus.rx, OUTPUT);
        if (kvxConfigPins.gps_bus.rx == kvxConfigPins.CC1101_bus.cs ||
            kvxConfigPins.gps_bus.rx == kvxConfigPins.NRF24_bus.cs ||
#if !defined(LITE_VERSION)
            kvxConfigPins.gps_bus.rx == kvxConfigPins.W5500_bus.cs ||
#endif
            kvxConfigPins.gps_bus.rx == kvxConfigPins.SDCARD_bus.cs) {
            digitalWrite(kvxConfigPins.gps_bus.rx, HIGH);
        } else {
            digitalWrite(kvxConfigPins.gps_bus.rx, LOW);
        }
    }
    rxPinReleased = false;
}

bool WdGps::begin() {
    releasePins();
    pinMode(kvxConfigPins.gps_bus.rx, INPUT);
    serial.begin(kvxConfigPins.gpsBaudrate, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    started = true;
    lastSentenceMs = 0;
    return true;
}

void WdGps::end() {
    if (!started) return;
    serial.end();
    restorePins();
    started = false;
}

void WdGps::pump() {
    if (!started) return;
    while (serial.available() > 0) {
        if (gps.encode(serial.read())) lastSentenceMs = millis();
    }

    WdGpsFix cur = getFix();
    if (cur.has_3d_fix && cur.accuracy_ok) {
        lastKnown = cur;
        everHadFix = true;
    }
    if (!timeSynced && cur.date_valid && cur.time_valid) syncSystemTime();
}

float WdGps::accuracyMeters() {
    if (!gps.hdop.isValid()) return 999.0f;
    return (float)gps.hdop.hdop() * HDOP_TO_METERS;
}

String WdGps::formatUtcDateTime() {
    if (!gps.date.isValid() || !gps.time.isValid()) return "0000-00-00 00:00:00";
    char buf[24];
    snprintf(
        buf,
        sizeof(buf),
        "%04d-%02d-%02d %02d:%02d:%02d",
        gps.date.year(),
        gps.date.month(),
        gps.date.day(),
        gps.time.hour(),
        gps.time.minute(),
        gps.time.second()
    );
    return String(buf);
}

WdGpsFix WdGps::getFix() {
    WdGpsFix f;
    f.location_valid = gps.location.isValid();
    f.lat = f.location_valid ? gps.location.lat() : 0.0;
    f.lng = f.location_valid ? gps.location.lng() : 0.0;
    f.altitude = gps.altitude.isValid() ? (float)gps.altitude.meters() : 0.0f;
    f.accuracy_m = accuracyMeters();
    f.satellites = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
    f.has_3d_fix = f.location_valid && f.satellites >= minSats;
    f.accuracy_ok = f.accuracy_m <= accuracyThresholdM;
    f.date_valid = gps.date.isValid();
    f.time_valid = gps.time.isValid();
    f.datetime = formatUtcDateTime();
    f.age_ms = gps.location.isValid() ? gps.location.age() : 0xFFFFFFFF;
    return f;
}

bool WdGps::canLog() {
    WdGpsFix dummy;
    return resolveLogPosition(dummy);
}

bool WdGps::resolveLogPosition(WdGpsFix &out) {
    WdGpsFix live = getFix();
    const bool good = live.has_3d_fix && live.accuracy_ok;

    if (logMode == "zero_gps") {
        if (good) {
            out = live;
            return true;
        }
        out = live;
        out.lat = 0;
        out.lng = 0;
        out.altitude = 0;
        out.location_valid = true;
        out.has_3d_fix = false;
        out.accuracy_ok = true;
        if (out.datetime == "0000-00-00 00:00:00" && everHadFix) out.datetime = lastKnown.datetime;
        return true;
    }

    if (logMode == "last_known") {
        if (good) {
            out = live;
            return true;
        }
        if (!everHadFix) return false;
        out = lastKnown;
        return true;
    }

    // fix_only
    if (!good) return false;
    out = live;
    return true;
}

bool WdGps::syncSystemTime() {
    if (!gps.date.isValid() || !gps.time.isValid()) return false;

    struct tm t = {};
    t.tm_year = gps.date.year() - 1900;
    t.tm_mon = gps.date.month() - 1;
    t.tm_mday = gps.date.day();
    t.tm_hour = gps.time.hour();
    t.tm_min = gps.time.minute();
    t.tm_sec = gps.time.second();
    t.tm_isdst = 0;

    time_t epoch = mktime(&t);
    epoch += (long)gmtOffsetHours * 3600L;

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    timeSynced = true;
    timeLastSyncedMs = millis();
    return true;
}

uint32_t WdGps::msSinceSentence() const {
    if (lastSentenceMs == 0) return 0xFFFFFFFF;
    return millis() - lastSentenceMs;
}

bool WdGps::sentencesStale(uint32_t maxGapMs) const {
    if (lastSentenceMs == 0) return true;
    return (millis() - lastSentenceMs) > maxGapMs;
}

double WdGps::distanceMeters(double lat1, double lng1, double lat2, double lng2) const {
    return TinyGPSPlus::distanceBetween(lat1, lng1, lat2, lng2);
}

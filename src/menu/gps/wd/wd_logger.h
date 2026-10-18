#pragma once

#include "wd_gps.h"
#include "wd_scanner.h"
#include <Arduino.h>
#include <FS.h>
#include <unordered_set>
#include <vector>

class WdLogger {
public:
    /// Reserve next wd_NNN.csv path; file is created on first write.
    bool openSession(FS &fs, const char *dir);
    void close();
    bool isOpen() const { return (bool)file; }
    bool hasPath() const { return path.length() > 0; }
    void flush();
    bool maybeFlush(uint32_t intervalMs = 60000);

    bool writeWifi(const WdWifiHit &hit, const WdGpsFix &fix);
    bool writeBle(
        const String &addr, const String &name, int rssi, uint16_t mfgId, const WdGpsFix &fix
    );
    bool writeFlag(const WdGpsFix &fix);

    String filename() const { return name; }
    String fullPath() const { return path; }
    uint32_t rowsWritten() const { return rows; }
    uint32_t sweeps() const { return sweepCount; }
    void noteSweep() { sweepCount++; }

    // Unique tracking for UI / beeps only — does not gate CSV writes.
    bool noteUnique(uint64_t macKey);
    size_t uniqueCount() const { return unique.size(); }

    static int nextSessionIndex(FS &fs, const char *dir);
    static String authModeString(wifi_auth_mode_t auth);

private:
    File file;
    FS *fsPtr = nullptr;
    String path;
    String name;
    uint32_t rows = 0;
    uint32_t sweepCount = 0;
    uint32_t lastFlushMs = 0;
    std::unordered_set<uint64_t> unique;
    static constexpr size_t MAX_UNIQUE = 4096;

    bool ensureOpen();
    void writeHeader();
    static String escapeSsid(const String &ssid);
};

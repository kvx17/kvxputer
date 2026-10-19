#pragma once

#include "wd_gps.h"
#include "wd_scanner.h"
#include <Arduino.h>
#include <FS.h>
#include <unordered_set>
#include <vector>

class WdLogger {
public:
    /// Create wd_NNN.csv immediately (header written + flushed).
    bool openSession(FS *fs, const char *dir);
    /// Re-open the current session path for append (after WiFi stop/start).
    bool reopenCurrent();
    void close();
    bool isOpen() const { return (bool)file; }
    bool hasPath() const { return path.length() > 0; }
    void flush();
    bool maybeFlush(uint32_t intervalMs = 60000);
    /// Bytes on disk after a flush (0 if unavailable).
    size_t fileSize() const;

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
    uint32_t writeErrors() const { return writeFailCount; }

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
    uint32_t writeFailCount = 0;
    std::unordered_set<uint64_t> unique;
    static constexpr size_t MAX_UNIQUE = 4096;

    bool reopenForAppend();
    void writeHeader();
    static String escapeSsid(const String &ssid);
};

#include "wd_logger.h"
#include "root/storage/paths.h"
#include <globals.h>
#include <cctype>

String WdLogger::authModeString(wifi_auth_mode_t authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
        default: return "UNKNOWN";
    }
}

String WdLogger::escapeSsid(const String &ssid) {
    String out;
    out.reserve(ssid.length());
    for (size_t i = 0; i < ssid.length(); i++) {
        char c = ssid[i];
        if (c == '"') out += '"';
        out += c;
    }
    return out;
}

int WdLogger::nextSessionIndex(FS &fs, const char *dir) {
    int maxIdx = 0;
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) return 1;
    while (true) {
        bool isDir = false;
        String full = root.getNextFileName(&isDir);
        if (full.isEmpty()) break;
        if (isDir) continue;
        String nameOnly = full.substring(full.lastIndexOf('/') + 1);
        if (!nameOnly.startsWith("wd_")) continue;
        String lower = nameOnly;
        lower.toLowerCase();
        if (!lower.endsWith(".csv")) continue;
        int n = nameOnly.substring(3, nameOnly.length() - 4).toInt();
        if (n > maxIdx) maxIdx = n;
    }
    root.close();
    return maxIdx + 1;
}

bool WdLogger::openSession(FS *fs, const char *dir) {
    close();
    if (!fs || !dir) return false;

    kvx::paths::ensureDir(*fs, dir);
    int idx = nextSessionIndex(*fs, dir);
    char buf[32];
    snprintf(buf, sizeof(buf), "wd_%03d.csv", idx);
    name = buf;
    path = String(dir) + "/" + name;
    fsPtr = fs;
    rows = 0;
    sweepCount = 0;
    writeFailCount = 0;
    unique.clear();

    // Create immediately (before WiFi scans) so SD open is not racing the radio.
    file = fsPtr->open(path, FILE_WRITE);
    if (!file) {
        fsPtr = nullptr;
        path = "";
        name = "";
        return false;
    }
    writeHeader();
    file.flush();
    lastFlushMs = millis();

    // Verify the file landed on storage.
    size_t sz = file.size();
    if (sz < 32) {
        file.close();
        fsPtr->remove(path);
        fsPtr = nullptr;
        path = "";
        name = "";
        return false;
    }
    return true;
}

bool WdLogger::reopenForAppend() {
    if (!fsPtr || path.isEmpty()) return false;
    if (file) {
        file.flush();
        file.close();
    }
    file = fsPtr->open(path, FILE_APPEND);
    return (bool)file;
}

bool WdLogger::reopenCurrent() {
    return reopenForAppend();
}

size_t WdLogger::fileSize() const {
    if (!file) return 0;
    return file.size();
}

void WdLogger::writeHeader() {
    file.printf(
        "WigleWifi-1.6,appRelease=v%s,model=kvxputer,release=v%s,"
        "device=ESP32,display=SPI TFT,board=ESP32,brand=kvxputer,star=Sol,body=4,subBody=1\n",
        KVXPUTER_VERSION,
        KVXPUTER_VERSION
    );
    file.println(
        "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,"
        "AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type"
    );
}

void WdLogger::close() {
    if (file) {
        file.flush();
        file.close();
    }
    // Keep fsPtr + path so reopenCurrent() can append to the same session.
}

void WdLogger::flush() {
    if (file) {
        file.flush();
        lastFlushMs = millis();
    }
}

bool WdLogger::maybeFlush(uint32_t intervalMs) {
    if (!file) return false;
    if (millis() - lastFlushMs < intervalMs) return false;
    flush();
    return true;
}

bool WdLogger::noteUnique(uint64_t macKey) {
    if (unique.size() >= MAX_UNIQUE) return unique.find(macKey) == unique.end();
    auto r = unique.insert(macKey);
    return r.second;
}

bool WdLogger::writeWifi(const WdWifiHit &hit, const WdGpsFix &fix) {
    if (!file && !reopenForAppend()) {
        writeFailCount++;
        return false;
    }
    String ssid = escapeSsid(hit.ssid);
    char line[512];
    int n = snprintf(
        line,
        sizeof(line),
        "%s,\"%s\",[%s],\"%s\",%d,%d,%d,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
        hit.bssid.c_str(),
        ssid.c_str(),
        authModeString(hit.auth).c_str(),
        fix.datetime.c_str(),
        hit.channel,
        hit.frequency,
        hit.rssi,
        fix.lat,
        fix.lng,
        fix.altitude,
        fix.accuracy_m
    );
    if (n <= 0 || n >= (int)sizeof(line)) {
        writeFailCount++;
        return false;
    }
    size_t wrote = file.print(line);
    if (wrote == 0) {
        // Retry once after reopen (SD can drop the handle under WiFi load).
        if (!reopenForAppend() || file.print(line) == 0) {
            writeFailCount++;
            return false;
        }
    }
    rows++;
    // Periodic flush keeps SD consistent if power/radio drops the handle.
    if ((rows % 8) == 0) flush();
    return true;
}

bool WdLogger::writeBle(
    const String &addr, const String &devName, int rssi, uint16_t mfgId, const WdGpsFix &fix
) {
    if (!file && !reopenForAppend()) {
        writeFailCount++;
        return false;
    }
    char mfg[8] = "";
    if (mfgId != 0) snprintf(mfg, sizeof(mfg), "%04X", mfgId);
    String ssid = escapeSsid(devName);
    char line[512];
    int n = snprintf(
        line,
        sizeof(line),
        "%s,\"%s\",Misc [BLE],\"%s\",0,,%d,%.6f,%.6f,%.1f,%.1f,,%s,BLE\n",
        addr.c_str(),
        ssid.c_str(),
        fix.datetime.c_str(),
        rssi,
        fix.lat,
        fix.lng,
        fix.altitude,
        fix.accuracy_m,
        mfg
    );
    if (n <= 0 || n >= (int)sizeof(line)) {
        writeFailCount++;
        return false;
    }
    if (file.print(line) == 0) {
        if (!reopenForAppend() || file.print(line) == 0) {
            writeFailCount++;
            return false;
        }
    }
    rows++;
    if ((rows % 8) == 0) flush();
    return true;
}

bool WdLogger::writeFlag(const WdGpsFix &fix) {
    if (!file && !reopenForAppend()) {
        writeFailCount++;
        return false;
    }
    char line[256];
    snprintf(
        line,
        sizeof(line),
        "00:00:00:00:00:00,\"FLAG\",[FLAG],\"%s\",0,,,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
        fix.datetime.c_str(),
        fix.lat,
        fix.lng,
        fix.altitude,
        fix.accuracy_m
    );
    if (file.print(line) == 0) {
        if (!reopenForAppend() || file.print(line) == 0) {
            writeFailCount++;
            return false;
        }
    }
    rows++;
    flush();
    return true;
}

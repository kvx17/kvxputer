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
        if (c == '"') out += '"'; // CSV escape: double the quote
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

bool WdLogger::openSession(FS &fs, const char *dir) {
    close();
    kvx::paths::ensureDir(fs, dir);
    int idx = nextSessionIndex(fs, dir);
    char buf[32];
    snprintf(buf, sizeof(buf), "wd_%03d.csv", idx);
    name = buf;
    path = String(dir) + "/" + name;
    fsPtr = &fs;
    rows = 0;
    sweepCount = 0;
    unique.clear();
    lastFlushMs = millis();
    // Defer creating the file until the first logged row so View Captures
    // does not list empty header-only sessions.
    return true;
}

bool WdLogger::ensureOpen() {
    if (file) return true;
    if (!fsPtr || path.isEmpty()) return false;
    file = fsPtr->open(path, FILE_WRITE);
    if (!file) return false;
    writeHeader();
    lastFlushMs = millis();
    return true;
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
    file.flush();
}

void WdLogger::close() {
    if (file) {
        file.flush();
        file.close();
    }
    fsPtr = nullptr;
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
    if (!ensureOpen()) return false;
    String ssid = escapeSsid(hit.ssid);
    char line[512];
    snprintf(
        line,
        sizeof(line),
        "%s,\"%s\",[%s],%s,%d,%d,%d,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
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
    file.print(line);
    rows++;
    return true;
}

bool WdLogger::writeBle(
    const String &addr, const String &devName, int rssi, uint16_t mfgId, const WdGpsFix &fix
) {
    if (!ensureOpen()) return false;
    char mfg[8] = "";
    if (mfgId != 0) snprintf(mfg, sizeof(mfg), "%04X", mfgId);
    String ssid = escapeSsid(devName);
    char line[512];
    snprintf(
        line,
        sizeof(line),
        "%s,\"%s\",Misc [BLE],%s,0,,%d,%.6f,%.6f,%.1f,%.1f,,%s,BLE\n",
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
    file.print(line);
    rows++;
    return true;
}

bool WdLogger::writeFlag(const WdGpsFix &fix) {
    if (!ensureOpen()) return false;
    char line[256];
    snprintf(
        line,
        sizeof(line),
        "00:00:00:00:00:00,\"FLAG\",[FLAG],%s,0,,,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
        fix.datetime.c_str(),
        fix.lat,
        fix.lng,
        fix.altitude,
        fix.accuracy_m
    );
    file.print(line);
    rows++;
    flush();
    return true;
}

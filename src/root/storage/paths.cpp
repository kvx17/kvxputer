#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include <SD.h>
#include <LittleFS.h>

namespace kvx {
namespace paths {

bool exists(FS &fs, const char *path) { return fs.exists(path); }

bool resolveReadable(FS &fs, const char *canonical, const char *legacy, String &out) {
    if (canonical && fs.exists(canonical)) {
        out = canonical;
        return true;
    }
    if (legacy && fs.exists(legacy)) {
        out = legacy;
        return true;
    }
    out = canonical ? canonical : "";
    return false;
}

bool resolveDir(FS &fs, const char *canonical, const char *legacy, String &out) {
    if (canonical && fs.exists(canonical)) {
        out = canonical;
        return true;
    }
    if (legacy && fs.exists(legacy)) {
        out = legacy;
        return true;
    }
    out = canonical ? canonical : "";
    return false;
}

void ensureDir(FS &fs, const char *path) {
    if (!path || !*path) return;
    if (fs.exists(path)) return;
    // Recursive: ESP32 FAT mkdir is not recursive.
    String p = path;
    String built = "";
    int start = 0;
    if (p.startsWith("/")) start = 1;
    while (start <= (int)p.length()) {
        int next = p.indexOf('/', start);
        String segment;
        if (next < 0) {
            segment = p.substring(start);
            start = p.length() + 1;
        } else {
            segment = p.substring(start, next);
            start = next + 1;
        }
        if (segment.length() == 0) continue;
        if (built.length() == 0) built = "/" + segment;
        else built += "/" + segment;
        if (!fs.exists(built)) fs.mkdir(built);
    }
}

void ensureParentDirs(FS &fs, const char *filePath) {
    if (!filePath || !*filePath) return;
    String p = filePath;
    int slash = p.lastIndexOf('/');
    if (slash <= 0) return;
    String dir = p.substring(0, slash);
    // ensureDir is recursive — one call creates the full parent chain.
    ensureDir(fs, dir.c_str());
}

const char *configPath(FS &fs) {
    if (fs.exists(CONF)) return CONF;
    if (fs.exists(CONF_LEGACY)) return CONF_LEGACY;
    return CONF;
}

const char *userSettingsPath() { return USER_SETTINGS; }

const char *bootSoundPath(FS &fs) {
    if (fs.exists(BOOT_SOUND)) return BOOT_SOUND;
    if (fs.exists(BOOT_SOUND_LEGACY)) return BOOT_SOUND_LEGACY;
    return BOOT_SOUND;
}

const char *mifareKeysPath(FS &fs) {
    if (fs.exists(MIFARE_KEYS)) return MIFARE_KEYS;
    if (fs.exists(MIFARE_KEYS_LEGACY)) return MIFARE_KEYS_LEGACY;
    return MIFARE_KEYS;
}

String scriptsFolder(FS *&fs) {
    if (SD.exists(MENU_SCRIPTS)) {
        fs = &SD;
        return MENU_SCRIPTS;
    }
    if (LittleFS.exists(MENU_SCRIPTS)) {
        fs = &LittleFS;
        return MENU_SCRIPTS;
    }
    for (size_t i = 0; i < MENU_SCRIPTS_LEGACY_COUNT; i++) {
        const char *legacy = MENU_SCRIPTS_LEGACY[i];
        if (SD.exists(legacy)) {
            fs = &SD;
            return legacy;
        }
        if (LittleFS.exists(legacy)) {
            fs = &LittleFS;
            return legacy;
        }
    }
    fs = &LittleFS;
    return MENU_SCRIPTS;
}

} // namespace paths
} // namespace kvx

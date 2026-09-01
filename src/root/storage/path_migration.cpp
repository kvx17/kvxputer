#include "root/storage/path_migration.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include <Preferences.h>
#include <SD.h>
#include <LittleFS.h>

namespace {

constexpr const char *NVS_NS   = "kvx_paths";
constexpr const char *NVS_KEY  = "migrated_v1";

struct DirMigration {
    const char *legacy;
    const char *canonical;
};

struct FileMigration {
    const char *legacy;
    const char *canonical;
};

const DirMigration kDirMigrations[] = {
    {"/BruceIR",                kvx::paths::IR_PROFILES},
    {"/BruceRFID",              kvx::paths::RFID},
    {"/rfid/hf",                kvx::paths::RFID_HF},
    {"/BrucePCAP",              kvx::paths::WIFI_CAPTURES},
    {"/BruceEvilCreds",         kvx::paths::WIFI_PORTAL_CREDS},
    {"/BruceResponder",         kvx::paths::NETOPS_RESPONDER},
    {"/Bruce/Terminal",         kvx::paths::NETOPS_TERMINAL},
    {"/BruceRF",                kvx::paths::RF_PRESETS},
    {"/BruceMIC",               kvx::paths::MEDIA_RECORDINGS},
    {"/BruceGPS",               kvx::paths::GPS_TRACKS},
    {"/BruceWardriving",        kvx::paths::GPS_WARDRIVING},
    {"/BruceSniffer",           kvx::paths::BLE_CAPTURES},
    {"/BruceIButton",           kvx::paths::IBUTTON},
    {"/scripts",                kvx::paths::MENU_SCRIPTS},
    {"/BruceScripts",           kvx::paths::MENU_SCRIPTS},
    {"/BruceJS",                kvx::paths::MENU_SCRIPTS},
};

const FileMigration kFileMigrations[] = {
    {"/bruce.conf",             kvx::paths::CONF},
    {"/boot.wav",               kvx::paths::BOOT_SOUND},
    {"/lora_settings.json",     kvx::paths::LORA_SETTINGS},
    {"/BruceRFID/keys.conf",    kvx::paths::MIFARE_KEYS},
};

bool isDirEmpty(FS &fs, const char *path) {
    File dir = fs.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return true;
    }
    bool empty = !dir.openNextFile();
    dir.close();
    return empty;
}

bool copyFile(FS &fs, const char *src, const char *dst) {
    File in = fs.open(src, FILE_READ);
    if (!in) return false;
    kvx::paths::ensureParentDirs(fs, dst);
    File out = fs.open(dst, FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }
    uint8_t buf[512];
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        if (n) out.write(buf, n);
    }
    in.close();
    out.close();
    return true;
}

bool copyTree(FS &fs, const char *srcDir, const char *dstDir) {
    File dir = fs.open(srcDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }
    kvx::paths::ensureDir(fs, dstDir);
    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        entry.close();
        if (name.endsWith("/")) name.remove(name.length() - 1);
        int slash = name.lastIndexOf('/');
        String base = slash >= 0 ? name.substring(slash + 1) : name;
        String srcPath = String(srcDir);
        if (!srcPath.endsWith("/")) srcPath += "/";
        srcPath += base;
        String dstPath = String(dstDir);
        if (!dstPath.endsWith("/")) dstPath += "/";
        dstPath += base;

        File test = fs.open(srcPath);
        if (!test) continue;
        bool isDir = test.isDirectory();
        test.close();

        if (isDir) {
            copyTree(fs, srcPath.c_str(), dstPath.c_str());
        } else if (!fs.exists(dstPath.c_str())) {
            copyFile(fs, srcPath.c_str(), dstPath.c_str());
            Serial.printf("[kvx] migrated file %s -> %s\n", srcPath.c_str(), dstPath.c_str());
        }
    }
    dir.close();
    return true;
}

void migrateOnFs(FS &fs, const char *label) {
    for (const auto &m : kFileMigrations) {
        if (fs.exists(m.legacy) && !fs.exists(m.canonical)) {
            kvx::paths::ensureParentDirs(fs, m.canonical);
            if (copyFile(fs, m.legacy, m.canonical)) {
                Serial.printf("[kvx][%s] migrated %s -> %s\n", label, m.legacy, m.canonical);
            }
        }
    }
    for (const auto &m : kDirMigrations) {
        if (!fs.exists(m.legacy)) continue;
        if (fs.exists(m.canonical) && !isDirEmpty(fs, m.canonical)) continue;
        Serial.printf("[kvx][%s] migrating dir %s -> %s\n", label, m.legacy, m.canonical);
        copyTree(fs, m.legacy, m.canonical);
    }
    // Ensure canonical top-level dirs exist
    kvx::paths::ensureDir(fs, kvx::paths::ROOT);
    kvx::paths::ensureDir(fs, "/menu");
    kvx::paths::ensureDir(fs, "/support_files");
}

void ensureAddonDirs(FS &fs) {
    kvx::paths::ensureDir(fs, kvx::paths::WIFI_PROBES);
    kvx::paths::ensureDir(fs, kvx::paths::WIFI_WORDLISTS);
    kvx::paths::ensureDir(fs, kvx::paths::WIFI_DEADDROP);
    kvx::paths::ensureDir(fs, kvx::paths::WIFI_WOF);
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_CRAWLER);
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_PRINTER);
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_CCTV);
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_CIW);
    kvx::paths::ensureDir(fs, kvx::paths::BLE_AIRTAGS);
    kvx::paths::ensureDir(fs, kvx::paths::IR_ESL);
    kvx::paths::ensureDir(fs, kvx::paths::IR_REMOTES);
}

} // namespace

void kvxRunPathMigration() {
    Preferences prefs;
    if (prefs.begin(NVS_NS, true)) {
        bool done = prefs.getBool(NVS_KEY, false);
        prefs.end();
        if (!done) {
            migrateOnFs(LittleFS, "LittleFS");
            if (setupSdCard()) migrateOnFs(SD, "SD");
            prefs.begin(NVS_NS, false);
            prefs.putBool(NVS_KEY, true);
            prefs.end();
            Serial.println("[kvx] path migration complete");
        }
    }

    ensureAddonDirs(LittleFS);
    if (setupSdCard()) ensureAddonDirs(SD);
}

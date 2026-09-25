#ifndef __KVX_PATHS_H__
#define __KVX_PATHS_H__

#include <FS.h>
#include <Arduino.h>

namespace kvx {
namespace paths {

// --- root/ (system) ---
constexpr const char *ROOT              = "/root";
constexpr const char *CONF              = "/root/kvxputer.conf";
constexpr const char *CONF_LEGACY       = "/bruce.conf";
// User-facing settings copy on SD (timezone, shortcuts, LED, PDA binds, …).
constexpr const char *USER_SETTINGS     = "/kvxputer/userSettings.json";
constexpr const char *BOOT_SOUND        = "/root/boot.wav";
constexpr const char *BOOT_SOUND_LEGACY = "/boot.wav";
constexpr const char *THEMES            = "/root/themes";
constexpr const char *MIFARE_KEYS       = "/root/keys/mifare.keys";
constexpr const char *MIFARE_KEYS_LEGACY = "/BruceRFID/keys.conf";

// --- menu/ (user scripts) ---
constexpr const char *MENU_SCRIPTS           = "/menu/scripts";
constexpr const char *MENU_SCRIPTS_TOOLS     = "/menu/scripts/Tools";
constexpr const char *MENU_SCRIPTS_LEGACY[]  = {"/scripts", "/BruceScripts", "/BruceJS"};
constexpr const size_t MENU_SCRIPTS_LEGACY_COUNT = 3;

// BadUSB payloads (SD only)
constexpr const char *BADUSB_SCRIPTS         = "/kvxputer/scripts/badUSB";

// --- support_files/ (runtime assets) ---

// WiFi
constexpr const char *WIFI_CAPTURES              = "/support_files/wifi/captures";
constexpr const char *WIFI_CAPTURES_HANDSHAKES   = "/support_files/wifi/captures/handshakes";
constexpr const char *WIFI_CAPTURES_LEGACY       = "/BrucePCAP";
constexpr const char *WIFI_PORTAL_CREDS          = "/support_files/wifi/portals/creds";
constexpr const char *WIFI_PORTAL_CREDS_LEGACY     = "/BruceEvilCreds";
constexpr const char *WIFI_PORTALS               = "/support_files/wifi/portals/default";
constexpr const char *WIFI_PROBES                = "/support_files/wifi/probes";
constexpr const char *WIFI_WORDLISTS             = "/support_files/wifi/wordlists";
constexpr const char *WIFI_DEADDROP              = "/support_files/wifi/deaddrop";
constexpr const char *WIFI_WOF                   = "/support_files/wifi/wof";

// NetOps
constexpr const char *NETOPS_RESPONDER       = "/support_files/netops/responder";
constexpr const char *NETOPS_RESPONDER_LEGACY = "/BruceResponder";
constexpr const char *NETOPS_RESPONDER_NTLM  = "/support_files/netops/responder/ntlm_hashes.txt";
constexpr const char *NETOPS_TERMINAL        = "/support_files/netops/terminal";
constexpr const char *NETOPS_TERMINAL_LEGACY  = "/Bruce/Terminal";
constexpr const char *NETOPS_CRAWLER         = "/support_files/netops/crawler";
constexpr const char *NETOPS_PRINTER         = "/support_files/netops/printer";
constexpr const char *NETOPS_CCTV            = "/support_files/netops/cctv";
constexpr const char *NETOPS_CIW             = "/support_files/netops/ciw";

// RFID
constexpr const char *RFID                 = "/support_files/rfid";
constexpr const char *RFID_HF              = "/support_files/rfid/hf";
constexpr const char *RFID_HF_LEGACY         = "/rfid/hf";
constexpr const char *RFID_LF              = "/support_files/rfid/lf";
constexpr const char *RFID_SCANS           = "/support_files/rfid/scans";
constexpr const char *RFID_SRIX            = "/support_files/rfid/srix";
constexpr const char *RFID_AMIIBO          = "/support_files/rfid/amiibo";
constexpr const char *RFID_LEGACY          = "/BruceRFID";
constexpr const char *RFID2                = "/support_files/rfid2";

// Infrared (user captures under SD/kvxputer; packs may still live under support_files)
constexpr const char *IR_PROFILES          = "/kvxputer/infrared/profiles";
constexpr const char *IR_PROFILES_LEGACY   = "/BruceIR";
constexpr const char *IR_PROFILES_LEGACY2  = "/support_files/infrared/profiles";
constexpr const char *IR_REMOTES           = "/kvxputer/infrared/remotes";
constexpr const char *IR_REMOTES_LEGACY    = "/support_files/infrared/remotes";
constexpr const char *IR_TVBG              = "/support_files/infrared/tvbg";
constexpr const char *IR_ESL               = "/support_files/infrared/esl";
// Universal remote app settings (favorites) — SD only
constexpr const char *KREMOTE_DIR          = "/kvxputer/kvxuniversalremote";
constexpr const char *KREMOTE_USER_SETTINGS = "/kvxputer/kvxuniversalremote/userSettings.json";

// RF
constexpr const char *RF_PRESETS           = "/kvxputer/rf/presets";
constexpr const char *RF_PRESETS_LEGACY    = "/BruceRF";
constexpr const char *RF_PRESETS_LEGACY2   = "/support_files/rf/presets";

// LoRa
constexpr const char *LORA_SETTINGS        = "/support_files/lora/lora_settings.json";
constexpr const char *LORA_SETTINGS_LEGACY = "/lora_settings.json";

// Media
constexpr const char *MEDIA_AUDIO          = "/support_files/media/audio";
constexpr const char *MEDIA_RECORDINGS     = "/support_files/media/recordings";
constexpr const char *MEDIA_RECORDINGS_LEGACY = "/BruceMIC";
constexpr const char *MEDIA_IMAGES         = "/support_files/media/images";

// GPS
constexpr const char *GPS_TRACKS           = "/support_files/gps/tracks";
constexpr const char *GPS_WARDRIVING       = "/support_files/gps/wardriving";
constexpr const char *GPS_WDBOTH           = "/kvxputer/app/WdBoth";
constexpr const char *GPS_TRACKS_LEGACY    = "/BruceGPS";
constexpr const char *GPS_WARDRIVING_LEGACY = "/BruceWardriving";

// BLE
constexpr const char *BLE_CAPTURES         = "/support_files/ble/captures";
constexpr const char *BLE_CAPTURES_LEGACY  = "/BruceSniffer";
constexpr const char *BLE_AIRTAGS          = "/support_files/ble/airtags";
constexpr const char *BLE_FINDMY_KEYS      = "/support_files/ble/FindMyEvil_keys.txt";
constexpr const char *BLE_NAMES            = "/support_files/ble/names.txt";

// Companion firmware images (SD; too large for LittleFS)
constexpr const char *COMPANIONS           = "/support_files/companions";

// Others / misc support
constexpr const char *IBUTTON              = "/support_files/others/ibutton";
constexpr const char *IBUTTON_LEGACY       = "/BruceIButton";

// PDA (Pocket Device Assistant)
constexpr const char *PDA                  = "/support_files/pda";
constexpr const char *PDA_NOTES            = "/support_files/pda/notes";
constexpr const char *PDA_MEMOS            = "/support_files/pda/memos";
constexpr const char *PDA_TODO             = "/support_files/pda/todo";
constexpr const char *PDA_CALENDAR         = "/support_files/pda/calendar";
constexpr const char *PDA_CONTACTS         = "/support_files/pda/contacts";
constexpr const char *PDA_ALARMS           = "/support_files/pda/alarms";

// --- helpers ---
bool exists(FS &fs, const char *path);
bool resolveReadable(FS &fs, const char *canonical, const char *legacy, String &out);
bool resolveDir(FS &fs, const char *canonical, const char *legacy, String &out);
void ensureDir(FS &fs, const char *path);
void ensureParentDirs(FS &fs, const char *filePath);
const char *configPath(FS &fs);
const char *userSettingsPath();
const char *bootSoundPath(FS &fs);
const char *mifareKeysPath(FS &fs);
String scriptsFolder(FS *&fs);

} // namespace paths
} // namespace kvx

#endif

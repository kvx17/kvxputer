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

// --- support_files/ (runtime assets) ---

// WiFi
constexpr const char *WIFI_CAPTURES              = "/support_files/wifi/captures";
constexpr const char *WIFI_CAPTURES_HANDSHAKES   = "/support_files/wifi/captures/handshakes";
constexpr const char *WIFI_CAPTURES_LEGACY       = "/BrucePCAP";
constexpr const char *WIFI_PORTAL_CREDS          = "/support_files/wifi/portals/creds";
constexpr const char *WIFI_PORTAL_CREDS_LEGACY     = "/BruceEvilCreds";
constexpr const char *WIFI_PORTALS               = "/support_files/wifi/portals/default";

// NetOps
constexpr const char *NETOPS_RESPONDER       = "/support_files/netops/responder";
constexpr const char *NETOPS_RESPONDER_LEGACY = "/BruceResponder";
constexpr const char *NETOPS_RESPONDER_NTLM  = "/support_files/netops/responder/ntlm_hashes.txt";
constexpr const char *NETOPS_TERMINAL        = "/support_files/netops/terminal";
constexpr const char *NETOPS_TERMINAL_LEGACY  = "/Bruce/Terminal";

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

// Infrared
constexpr const char *IR_PROFILES        = "/support_files/infrared/profiles";
constexpr const char *IR_PROFILES_LEGACY   = "/BruceIR";
constexpr const char *IR_TVBG              = "/support_files/infrared/tvbg";

// RF
constexpr const char *RF_PRESETS           = "/support_files/rf/presets";
constexpr const char *RF_PRESETS_LEGACY    = "/BruceRF";

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
constexpr const char *GPS_TRACKS_LEGACY    = "/BruceGPS";
constexpr const char *GPS_WARDRIVING_LEGACY = "/BruceWardriving";

// BLE
constexpr const char *BLE_CAPTURES         = "/support_files/ble/captures";
constexpr const char *BLE_CAPTURES_LEGACY  = "/BruceSniffer";

// Others / misc support
constexpr const char *IBUTTON              = "/support_files/others/ibutton";
constexpr const char *IBUTTON_LEGACY       = "/BruceIButton";

// --- helpers ---
bool exists(FS &fs, const char *path);
bool resolveReadable(FS &fs, const char *canonical, const char *legacy, String &out);
bool resolveDir(FS &fs, const char *canonical, const char *legacy, String &out);
void ensureDir(FS &fs, const char *path);
void ensureParentDirs(FS &fs, const char *filePath);
const char *configPath(FS &fs);
const char *bootSoundPath(FS &fs);
const char *mifareKeysPath(FS &fs);
String scriptsFolder(FS *&fs);

} // namespace paths
} // namespace kvx

#endif

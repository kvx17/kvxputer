#ifndef __KVXPUTER_CONFIG_H__
#define __KVXPUTER_CONFIG_H__

#include "root/ui/theme.h"
#include "root/storage/paths.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <precompiler_flags.h>
#include <set>
#include <vector>

enum EvilPortalPasswordMode { FULL_PASSWORD = 0, FIRST_LAST_CHAR = 1, HIDE_PASSWORD = 2, SAVE_LENGTH = 3 };

enum PahubDevice : uint8_t {
    PahubDevNone = 0,
    PahubDevRFID2 = 1,
    PahubDevNFC = 2,
    PahubDevScroll = 3,
    PahubDevJoystick2 = 4,
    PahubDevRF433R = 5,
};

class KvxputerConfig : public KvxputerTheme {
public:
    struct WiFiCredential {
        String ssid;
        String pwd;
    };
    struct Credential {
        String user;
        String pwd;
    };
    struct QrCodeEntry {
        String menuName;
        String content;
    };
    struct EvilPortalEndpoints {
        String getCredsEndpoint;
        String setSsidEndpoint;
        bool showEndpoints;
        bool allowSetSsid;
        bool allowGetCreds;
    };

    const char *filepath = kvx::paths::CONF;

    //  Settings
    int dimmerSet = 60;
    int bright = 100;
    bool automaticTimeUpdateViaNTP = true;
    float tmz = 0;
    bool dst = false;
    bool clock24hr = true;
    int soundEnabled = 1;
    int soundVolume = 100;
    int wifiAtStartup = 0;
    int instantBoot = 0;
    String keyboardLang = "QWERTY"; // "QWERTY" | "AZERTY" | "QWERTZ"

#ifdef HAS_RGB_LED
    // Led
    int ledBright = 50;
    uint32_t ledColor = 0x960064;
    int ledBlinkEnabled = 1;
    int ledEffect = 0;
    int ledEffectSpeed = 5;
    int ledEffectDirection = 1;
#endif

    // Wifi
    Credential webUI = {"admin", "bruce"};
    std::vector<String> webUISessions = {}; // FIFO queue of session tokens
    WiFiCredential wifiAp = {"KvxputerNet", "kvxputernet"};
    std::map<String, String> wifi = {};
    std::set<String> evilWifiNames = {};
    String wifiMAC = ""; //@IncursioHack
    bool TerminalLog = true;

    // EvilPortal
    EvilPortalEndpoints evilPortalEndpoints = {"/creds", "/ssid", true, true, true};
    EvilPortalPasswordMode evilPortalPasswordMode = FULL_PASSWORD;
    String evilPortalGatewayIp = "172.0.0.1";

    void setWifiMAC(const String &mac) {
        wifiMAC = mac;
        saveFile(); // opcional, para salvar imediatamente
    }

    // RFID
    std::set<String> mifareKeys = {};

    // Misc
    String startupApp = "";
    String startupAppJSInterpreterFile = "";
    String wigleBasicToken = "";
    String wdgwarsApiKey = "your 64-char hex key from wdgwars.pl/profile";
    int devMode = 0;
    int colorInverted = 1;
    int badUSBBLEKeyboardLayout = 0;
    uint16_t badUSBBLEKeyDelay = 10;
    bool badUSBBLEShowOutput = true;

    // Optional Unit Scroll (Grove PORT.A)
    bool unitScrollEnabled = true;
    bool unitScrollInvert = false;
    uint8_t unitScrollAxis = 0; // 0=vertical (RotaryNetSteps), 1=horizontal (Prev/Next)
    bool unitJoyInvertX = false;
    bool unitJoyInvertY = false;

    // Optional Unit PaHub v2.1 (Grove PORT.A I2C mux)
    bool pahubEnabled = false;
    uint8_t pahubAddr = 0x70;
    uint8_t pahubChannels[6] = {};

    // HID Remote — 6 UI slots; NimBLE MAX_BONDS stays 8 for other BLE tools
    static const int HID_REMOTE_HOST_SLOT_COUNT = 6;
    int hidRemoteTransport = 0; // 0=USB, 1=BLE
    String hidRemoteBleName = "kvxKeyboard";
    String hidRemoteHostName = "";
    String hidRemotePreferredHost = ""; // bonded BLE address; empty = any bonded
    String hidRemoteHostSlots[HID_REMOTE_HOST_SLOT_COUNT]; // 1-based UI maps to index 0..5
    std::map<String, String> hidRemoteHostAliases = {}; // addr -> display name
    int hidRemoteLastMode = 0;
    int hidRemoteMouseSensitivity = 5;
    bool hidRemoteJoyInvertY = false;
    int hidRemoteJigglerInterval = 30;
    int hidRemoteStealthMin = 45;
    int hidRemoteStealthMax = 120;
    int hidRemoteClickerDelay = 100;
    int hidRemoteClickerButton = 0;
    int hidRemotePttPreset = 0;
    uint8_t hidRemoteShortsUp = ';';
    uint8_t hidRemoteShortsDown = '.';
    bool hidRemoteLedEnabled = true;

    // kvxputer universal remote (kremote)
    bool kremotePortrait = false;
    bool kremoteButtonsSwapped = false;

    // G0 long-press returns to main menu (Cardputer); sticky Esc until home clears it.
    bool g0HoldHome = true;

    // PDA hub: key '1'..'8' → channel index 0..7 (default identity).
    uint8_t pdaKeyBind[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    // World Clock face: 0=cities, 1=charge calendar, 2=digital clock.
    int pdaWcFace = 0;

    std::vector<String> disabledMenus = {};
    std::map<String, String> mainscreenShortcuts = {};

    std::vector<QrCodeEntry> qrCodes = {
        {"kvxputer GitHub", "https://github.com/kvx17/kvxputer"},
        {"kvxputer AP",     "WIFI:T:WPA;S:KvxputerNet;P:kvxputernet;;"},
        {"Rickroll",        "https://youtu.be/dQw4w9WgXcQ"          }
    };

    /////////////////////////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////////////////////////
    KvxputerConfig() {};
    // ~KvxputerConfig();

private:
    bool _mifareKeysLoaded = false;

public:

    /////////////////////////////////////////////////////////////////////////////////////
    // Operations
    /////////////////////////////////////////////////////////////////////////////////////
    void saveFile();
    void fromFile(bool checkFS = true);
    void factoryReset();
    void validateConfig();
    JsonDocument toJson() const;

    // UI Color
    void setUiColor(uint16_t primary, uint16_t *secondary = nullptr, uint16_t *background = nullptr);

    // Settings
    void setDimmer(int value);
    void validateDimmerValue();
    void setBright(uint8_t value);
    void validateBrightValue();
    void setAutomaticTimeUpdateViaNTP(bool value);
    void setTmz(float value);
    void validateTmzValue();
    void setDST(bool value);
    void setClock24Hr(bool value);
    void setSoundEnabled(int value);
    void setSoundVolume(int value);
    void validateSoundEnabledValue();
    void validateSoundVolumeValue();
    void setWifiAtStartup(int value);
    void validateWifiAtStartupValue();

#ifdef HAS_RGB_LED
    // Led
    void setLedBright(int value);
    void validateLedBrightValue();
    void setLedColor(uint32_t value);
    void validateLedColorValue();
    void setLedBlinkEnabled(int value);
    void validateLedBlinkEnabledValue();
    void setLedEffect(int value);
    void validateLedEffectValue();
    void setLedEffectSpeed(int value);
    void validateLedEffectSpeedValue();
    void setLedEffectDirection(int value);
    void validateLedEffectDirectionValue();
#endif

    // Wifi
    void setWebUICreds(const String &usr, const String &pwd);
    void setWifiApCreds(const String &ssid, const String &pwd);
    void setTerminalLog(bool value);
    void addWifiCredential(const String &ssid, const String &pwd);
    void addQrCodeEntry(const String &menuName, const String &content);
    void removeQrCodeEntry(const String &menuName);
    void validateQrCodes();
    String getWifiPassword(const String &ssid) const;
    void addEvilWifiName(String value);
    void removeEvilWifiName(String value);
    void setEvilEndpointCreds(String value);
    void setEvilEndpointSsid(String value);
    void setEvilAllowEndpointDisplay(bool value);
    void setEvilAllowGetCreds(bool value);
    void setEvilAllowSetSsid(bool value);
    void setEvilPasswordMode(EvilPortalPasswordMode value);
    void setEvilGatewayIp(String value);
    void validateEvilEndpointCreds();
    void validateEvilEndpointSsid();
    void validateEvilPasswordMode();
    void validateEvilGatewayIp();

    // RFID
    void ensureMifareKeysLoaded();
    void addMifareKey(String value);
    void validateMifareKeysItems();

    // Misc
    void setStartupApp(String value);
    void setStartupAppJSInterpreterFile(String value);
    void setWigleBasicToken(String value);
    void setWdgwarsApiKey(String value);
    void setDevMode(int value);
    void validateDevModeValue();
    void setColorInverted(int value);
    void validateColorInverted();
    void setBadUSBBLEKeyboardLayout(int value);
    void validateBadUSBBLEKeyboardLayout();
    void setBadUSBBLEKeyDelay(uint16_t value);
    void validateBadUSBBLEKeyDelay();
    void setBadUSBBLEShowOutput(bool value);

    void setUnitScrollEnabled(bool value);
    void setUnitScrollInvert(bool value);
    void setUnitScrollAxis(uint8_t value);
    void setUnitJoyInvertX(bool value);
    void setUnitJoyInvertY(bool value);

    void setPahubEnabled(bool value);
    void setPahubAddr(uint8_t value);
    bool setPahubChannel(uint8_t ch, PahubDevice dev); // false if duplicate type
    void validatePahub();

    void setHidRemoteTransport(int value);
    void setHidRemoteBleName(const String &value);
    void setHidRemoteHostName(const String &value);
    void setHidRemotePreferredHost(const String &value);
    void setHidRemoteHostAlias(const String &addr, const String &name);
    void clearHidRemoteHostAlias(const String &addr);
    String getHidRemoteHostAlias(const String &addr) const;
    String getHidRemoteHostSlot(int index1to8) const;
    void setHidRemoteHostSlot(int index1to8, const String &addr);
    void clearHidRemoteHostSlot(int index1to8);
    void clearAllHidRemoteHostSlots();
    int findHidRemoteHostSlotForAddr(const String &addr) const; // 1..8 or 0
    int findEmptyHidRemoteHostSlot() const;                     // 1..6 or 0
    void setHidRemoteLastMode(int value);
    void setHidRemoteMouseSensitivity(int value);
    void setHidRemoteJoyInvertY(bool value);
    void setHidRemoteJigglerInterval(int value);
    void setHidRemoteStealthMin(int value);
    void setHidRemoteStealthMax(int value);
    void setHidRemoteClickerDelay(int value);
    void setHidRemoteClickerButton(int value);
    void setHidRemotePttPreset(int value);
    void setHidRemoteShortsKeys(uint8_t up, uint8_t down);
    void setHidRemoteLedEnabled(bool value);

    void setKremotePortrait(bool value);
    void setKremoteButtonsSwapped(bool value);

    void setG0HoldHome(bool value);
    void setPdaKeyBind(int key1to8, uint8_t channelIndex);
    void setPdaWcFace(int value);

    void addDisabledMenu(String value);
    void removeDisabledMenu(String value);

    void addWebUISession(const String &token);
    void removeWebUISession(const String &token);
    bool isValidWebUISession(const String &token);
};

inline String hidRemoteAddrCore(const String &addr) {
    String s = addr;
    int slash = s.lastIndexOf('/');
    if (slash >= 0 && slash == (int)s.length() - 2) s = s.substring(0, slash);
    s.trim();
    s.toLowerCase();
    return s;
}

inline bool hidRemoteAddrEqual(const String &a, const String &b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    return hidRemoteAddrCore(a) == hidRemoteAddrCore(b);
}

#endif

#include "root/storage/paths.h"
#include "root/config/config.h"
#include "root/config/mifare_keys_manager.h"
#include "root/storage/sd_functions.h"
#include <algorithm>

JsonDocument KvxputerConfig::toJson() const {
    JsonDocument jsonDoc;
    JsonObject setting = jsonDoc.to<JsonObject>();

    setting["priColor"] = String(priColor, HEX);
    setting["secColor"] = String(secColor, HEX);
    setting["bgColor"] = String(bgColor, HEX);
    setting["themeFile"] = themePath;
    setting["themeOnSd"] = theme.fs;

    setting["dimmerSet"] = dimmerSet;
    setting["bright"] = bright;
    setting["automaticTimeUpdateViaNTP"] = automaticTimeUpdateViaNTP;
    setting["tmz"] = tmz;
    setting["dst"] = dst;
    setting["clock24hr"] = clock24hr;
    setting["soundEnabled"] = soundEnabled;
    setting["soundVolume"] = soundVolume;
    setting["wifiAtStartup"] = wifiAtStartup;
    setting["instantBoot"] = instantBoot;
    setting["keyboardLang"] = keyboardLang;

#ifdef HAS_RGB_LED
    setting["ledBright"] = ledBright;
    setting["ledColor"] = String(ledColor, HEX);
    setting["ledBlinkEnabled"] = ledBlinkEnabled;
    setting["ledEffect"] = ledEffect;
    setting["ledEffectSpeed"] = ledEffectSpeed;
    setting["ledEffectDirection"] = ledEffectDirection;
#endif

    JsonObject _webUI = setting["webUI"].to<JsonObject>();
    _webUI["user"] = webUI.user;
    _webUI["pwd"] = webUI.pwd;
    JsonObject _webUISessions = setting["webUISessions"].to<JsonObject>();
    for (size_t i = 0; i < webUISessions.size(); i++) { _webUISessions[String(i + 1)] = webUISessions[i]; }

    JsonObject _wifiAp = setting["wifiAp"].to<JsonObject>();
    _wifiAp["ssid"] = wifiAp.ssid;
    _wifiAp["pwd"] = wifiAp.pwd;
    setting["wifiMAC"] = wifiMAC; //@IncursioHack
    setting["TerminalLog"] = TerminalLog;

    JsonArray _evilWifiNames = setting["evilWifiNames"].to<JsonArray>();
    for (auto key : evilWifiNames) _evilWifiNames.add(key);

    JsonObject _evilWifiEndpoints = setting["evilWifiEndpoints"].to<JsonObject>();
    _evilWifiEndpoints["getCredsEndpoint"] = evilPortalEndpoints.getCredsEndpoint;
    _evilWifiEndpoints["setSsidEndpoint"] = evilPortalEndpoints.setSsidEndpoint;
    _evilWifiEndpoints["showEndpoints"] = evilPortalEndpoints.showEndpoints;
    _evilWifiEndpoints["allowSetSsid"] = evilPortalEndpoints.allowSetSsid;
    _evilWifiEndpoints["allowGetCreds"] = evilPortalEndpoints.allowGetCreds;
    _evilWifiEndpoints["gatewayIp"] = evilPortalGatewayIp;

    setting["evilWifiPasswordMode"] = evilPortalPasswordMode;

    JsonObject _wifi = setting["wifi"].to<JsonObject>();
    for (const auto &pair : wifi) { _wifi[pair.first] = pair.second; }

    setting["startupApp"] = startupApp;
    setting["startupAppJSInterpreterFile"] = startupAppJSInterpreterFile;
    setting["wigleBasicToken"] = wigleBasicToken;
    setting["wdgwarsApiKey"] = wdgwarsApiKey;
    setting["devMode"] = devMode;
    setting["colorInverted"] = colorInverted;

    setting["badUSBBLEKeyboardLayout"] = badUSBBLEKeyboardLayout;
    setting["badUSBBLEKeyDelay"] = badUSBBLEKeyDelay;
    setting["badUSBBLEShowOutput"] = badUSBBLEShowOutput;
    setting["unitScrollEnabled"] = unitScrollEnabled;
    setting["unitScrollInvert"] = unitScrollInvert;

    setting["hidRemoteTransport"] = hidRemoteTransport;
    setting["hidRemoteBleName"] = hidRemoteBleName;
    setting["hidRemoteLastMode"] = hidRemoteLastMode;
    setting["hidRemoteMouseSensitivity"] = hidRemoteMouseSensitivity;
    setting["hidRemoteJigglerInterval"] = hidRemoteJigglerInterval;
    setting["hidRemoteStealthMin"] = hidRemoteStealthMin;
    setting["hidRemoteStealthMax"] = hidRemoteStealthMax;
    setting["hidRemoteClickerDelay"] = hidRemoteClickerDelay;
    setting["hidRemoteClickerButton"] = hidRemoteClickerButton;
    setting["hidRemotePttPreset"] = hidRemotePttPreset;

    JsonArray dm = setting["disabledMenus"].to<JsonArray>();
    for (int i = 0; i < disabledMenus.size(); i++) { dm.add(disabledMenus[i]); }

    JsonArray qrArray = setting["qrCodes"].to<JsonArray>();
    for (const auto &entry : qrCodes) {
        JsonObject qrEntry = qrArray.add<JsonObject>();
        qrEntry["menuName"] = entry.menuName;
        qrEntry["content"] = entry.content;
    }

    return jsonDoc;
}

void KvxputerConfig::fromFile(bool checkFS) {
    FS *fs;
    if (checkFS) {
        if (!getFsStorage(fs)) {
            log_i("Fail getting filesystem for config");
            return;
        }
    } else {
        if (checkLittleFsSize()) fs = &LittleFS;
        else return;
    }

    const char *loadPath = kvx::paths::configPath(*fs);
    if (!fs->exists(loadPath)) {
        log_i("Config file not found. Creating default config");
        return saveFile();
    }

    File file;
    file = fs->open(loadPath, FILE_READ);
    if (!file) {
        log_i("Config file not found. Using default values");
        return;
    }

    // Deserialize the JSON document
    JsonDocument jsonDoc;
    if (deserializeJson(jsonDoc, file)) {
        Serial.println("Failed to read config file, using default configuration");
        return;
    }
    file.close();

    JsonObject setting = jsonDoc.as<JsonObject>();
    int count = 0;

    if (!setting["priColor"].isNull()) {
        priColor = strtoul(setting["priColor"], nullptr, 16);
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["secColor"].isNull()) {
        secColor = strtoul(setting["secColor"], nullptr, 16);
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["bgColor"].isNull()) {
        bgColor = strtoul(setting["bgColor"], nullptr, 16);
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["themeFile"].isNull()) {
        themePath = setting["themeFile"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["themeOnSd"].isNull()) {
        theme.fs = setting["themeOnSd"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["dimmerSet"].isNull()) {
        dimmerSet = setting["dimmerSet"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["bright"].isNull()) {
        bright = setting["bright"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["automaticTimeUpdateViaNTP"].isNull()) {
        automaticTimeUpdateViaNTP = setting["automaticTimeUpdateViaNTP"].as<bool>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["tmz"].isNull()) {
        tmz = setting["tmz"].as<float>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["dst"].isNull()) {
        dst = setting["dst"].as<bool>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["clock24hr"].isNull()) {
        clock24hr = setting["clock24hr"].as<bool>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["soundEnabled"].isNull()) {
        soundEnabled = setting["soundEnabled"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["soundVolume"].isNull()) {
        soundVolume = setting["soundVolume"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["wifiAtStartup"].isNull()) {
        wifiAtStartup = setting["wifiAtStartup"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["instantBoot"].isNull()) {
        instantBoot = setting["instantBoot"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["keyboardLang"].isNull()) {
        keyboardLang = setting["keyboardLang"].as<String>();
    } else {
        keyboardLang = "QWERTY";
    }

#ifdef HAS_RGB_LED
    if (!setting["ledBright"].isNull()) {
        ledBright = setting["ledBright"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["ledColor"].isNull()) {
        ledColor = strtoul(setting["ledColor"], nullptr, 16);
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["ledBlinkEnabled"].isNull()) {
        ledBlinkEnabled = setting["ledBlinkEnabled"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["ledEffect"].isNull()) {
        ledEffect = setting["ledEffect"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["ledEffectSpeed"].isNull()) {
        ledEffectSpeed = setting["ledEffectSpeed"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["ledEffectDirection"].isNull()) {
        ledEffectDirection = setting["ledEffectDirection"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
#endif

    if (!setting["webUI"].isNull()) {
        JsonObject webUIObj = setting["webUI"].as<JsonObject>();
        webUI.user = webUIObj["user"].as<String>();
        webUI.pwd = webUIObj["pwd"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["webUISessions"].isNull()) {
        webUISessions.clear();
        JsonObject webUISessionsObj = setting["webUISessions"].as<JsonObject>();
        for (JsonPair kv : webUISessionsObj) { webUISessions.push_back(kv.value().as<String>()); }
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["wifiAp"].isNull()) {
        JsonObject wifiApObj = setting["wifiAp"].as<JsonObject>();
        wifiAp.ssid = wifiApObj["ssid"].as<String>();
        wifiAp.pwd = wifiApObj["pwd"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }

    //@IncursioHack
    if (!setting["wifiMAC"].isNull()) {
        wifiMAC = setting["wifiMAC"].as<String>();
    } else {
        wifiMAC = "";
        count++;
        log_e("wifiMAC not found, using default");
    }
    if (!setting["TerminalLog"].isNull()) {
        TerminalLog = setting["TerminalLog"].as<bool>();
    } else {
        count++;
        log_e("TerminalLog not found, using default");
    }

    // Wifi List
    if (!setting["wifi"].isNull()) {
        wifi.clear();
        JsonObject wifiObj = setting["wifi"].as<JsonObject>();
        for (JsonPair kv : wifiObj) wifi[kv.key().c_str()] = kv.value().as<String>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["evilWifiNames"].isNull()) {
        evilWifiNames.clear();
        JsonArray _evilWifiNames = setting["evilWifiNames"].as<JsonArray>();
        for (JsonVariant key : _evilWifiNames) evilWifiNames.insert(key.as<String>());
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["evilWifiEndpoints"].isNull()) {
        JsonObject evilPortalEndpointsObj = setting["evilWifiEndpoints"].as<JsonObject>();
        evilPortalEndpoints.getCredsEndpoint = evilPortalEndpointsObj["getCredsEndpoint"].as<String>();
        evilPortalEndpoints.setSsidEndpoint = evilPortalEndpointsObj["setSsidEndpoint"].as<String>();
        evilPortalEndpoints.showEndpoints = evilPortalEndpointsObj["showEndpoints"].as<bool>();
        evilPortalEndpoints.allowSetSsid = evilPortalEndpointsObj["allowSetSsid"].as<bool>();
        evilPortalEndpoints.allowGetCreds = evilPortalEndpointsObj["allowGetCreds"].as<bool>();
        if (!evilPortalEndpointsObj["gatewayIp"].isNull()) {
            evilPortalGatewayIp = evilPortalEndpointsObj["gatewayIp"].as<String>();
        } else {
            evilPortalGatewayIp = "172.0.0.1";
        }
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["evilWifiPasswordMode"].isNull()) {
        int mode = setting["evilWifiPasswordMode"].as<int>();
        if (mode >= 0 && mode <= 2) {
            evilPortalPasswordMode = static_cast<EvilPortalPasswordMode>(mode);
        } else {
            evilPortalPasswordMode = FULL_PASSWORD;
            log_w("Invalid evilWifiPasswordMode, using FULL_PASSWORD");
        }
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["startupApp"].isNull()) {
        startupApp = setting["startupApp"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["startupAppJSInterpreterFile"].isNull()) {
        startupAppJSInterpreterFile = setting["startupAppJSInterpreterFile"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["wigleBasicToken"].isNull()) {
        wigleBasicToken = setting["wigleBasicToken"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["wdgwarsApiKey"].isNull()) {
        wdgwarsApiKey = setting["wdgwarsApiKey"].as<String>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["devMode"].isNull()) {
        devMode = setting["devMode"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }
    if (!setting["colorInverted"].isNull()) {
        colorInverted = setting["colorInverted"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["badUSBBLEKeyboardLayout"].isNull()) {
        badUSBBLEKeyboardLayout = setting["badUSBBLEKeyboardLayout"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["badUSBBLEKeyDelay"].isNull()) {
        badUSBBLEKeyDelay = setting["badUSBBLEKeyDelay"].as<int>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["badUSBBLEShowOutput"].isNull()) {
        badUSBBLEShowOutput = setting["badUSBBLEShowOutput"].as<bool>();
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["unitScrollEnabled"].isNull()) {
        unitScrollEnabled = setting["unitScrollEnabled"].as<bool>();
    } else {
        unitScrollEnabled = true;
    }
    if (!setting["unitScrollInvert"].isNull()) {
        unitScrollInvert = setting["unitScrollInvert"].as<bool>();
    } else {
        unitScrollInvert = false;
    }

    if (!setting["hidRemoteTransport"].isNull()) {
        hidRemoteTransport = setting["hidRemoteTransport"].as<int>();
    }
    if (!setting["hidRemoteBleName"].isNull()) {
        hidRemoteBleName = setting["hidRemoteBleName"].as<String>();
    }
    if (!setting["hidRemoteLastMode"].isNull()) {
        hidRemoteLastMode = setting["hidRemoteLastMode"].as<int>();
    }
    if (!setting["hidRemoteMouseSensitivity"].isNull()) {
        hidRemoteMouseSensitivity = setting["hidRemoteMouseSensitivity"].as<int>();
    }
    if (!setting["hidRemoteJigglerInterval"].isNull()) {
        hidRemoteJigglerInterval = setting["hidRemoteJigglerInterval"].as<int>();
    }
    if (!setting["hidRemoteStealthMin"].isNull()) {
        hidRemoteStealthMin = setting["hidRemoteStealthMin"].as<int>();
    }
    if (!setting["hidRemoteStealthMax"].isNull()) {
        hidRemoteStealthMax = setting["hidRemoteStealthMax"].as<int>();
    }
    if (!setting["hidRemoteClickerDelay"].isNull()) {
        hidRemoteClickerDelay = setting["hidRemoteClickerDelay"].as<int>();
    }
    if (!setting["hidRemoteClickerButton"].isNull()) {
        hidRemoteClickerButton = setting["hidRemoteClickerButton"].as<int>();
    }
    if (!setting["hidRemotePttPreset"].isNull()) {
        hidRemotePttPreset = setting["hidRemotePttPreset"].as<int>();
    }

    if (!setting["disabledMenus"].isNull()) {
        disabledMenus.clear();
        JsonArray dm = setting["disabledMenus"].as<JsonArray>();
        for (JsonVariant e : dm) { disabledMenus.push_back(e.as<String>()); }
    } else {
        count++;
        log_e("Fail");
    }

    if (!setting["qrCodes"].isNull()) {
        qrCodes.clear();
        JsonArray qrArray = setting["qrCodes"].as<JsonArray>();
        for (JsonObject qrEntry : qrArray) {
            String menuName = qrEntry["menuName"].as<String>();
            String content = qrEntry["content"].as<String>();
            qrCodes.push_back({menuName, content});
        }
    } else {
        count++;
        log_e("Fail to load qrCodes");
    }

    validateConfig();
    if (count > 0) saveFile();

    log_i("Using config from file");
}

void KvxputerConfig::saveFile() {
    FS *fs = &LittleFS;
    JsonDocument jsonDoc = toJson();

    kvx::paths::ensureParentDirs(*fs, filepath);
    // Open file for writing
    File file = fs->open(filepath, FILE_WRITE);
    if (!file) {
        log_e("Failed to open config file");
        file.close();
        return;
    };

    // Serialize JSON to file
    serializeJsonPretty(jsonDoc, Serial);
    if (serializeJsonPretty(jsonDoc, file) < 5) log_e("Failed to write config file");
    else log_i("config file written successfully");

    file.close();

    if (setupSdCard()) copyToFs(LittleFS, SD, filepath, false);
}

void KvxputerConfig::factoryReset() {
    FS *fs = &LittleFS;
    fs->rename(String(filepath), "/bak." + String(filepath).substring(1));
    if (setupSdCard()) SD.rename(String(filepath), "/bak." + String(filepath).substring(1));
    ESP.restart();
}

void KvxputerConfig::validateConfig() {
    validateDimmerValue();
    validateBrightValue();
    validateTmzValue();
    validateSoundEnabledValue();
    validateSoundVolumeValue();
    validateWifiAtStartupValue();
#ifdef HAS_RGB_LED
    validateLedBrightValue();
    validateLedColorValue();
    validateLedBlinkEnabledValue();
    validateLedEffectValue();
    validateLedEffectSpeedValue();
    validateLedEffectDirectionValue();
#endif
    validateMifareKeysItems();
    validateDevModeValue();
    validateColorInverted();
    validateBadUSBBLEKeyboardLayout();
    validateBadUSBBLEKeyDelay();
    validateEvilEndpointCreds();
    validateEvilEndpointSsid();
    validateEvilPasswordMode();
    validateEvilGatewayIp();
}

void KvxputerConfig::setUiColor(uint16_t primary, uint16_t *secondary, uint16_t *background) {
    KvxputerTheme::_setUiColor(primary, secondary, background);
    saveFile();
}

void KvxputerConfig::setDimmer(int value) {
    dimmerSet = value;
    validateDimmerValue();
    saveFile();
}

void KvxputerConfig::validateDimmerValue() {
    if (dimmerSet < 0) dimmerSet = 10;
    if (dimmerSet > 60) dimmerSet = 0;
}

void KvxputerConfig::setBright(uint8_t value) {
    bright = value;
    validateBrightValue();
    saveFile();
}

void KvxputerConfig::validateBrightValue() {
    if (bright > 100) bright = 100;
}

void KvxputerConfig::setAutomaticTimeUpdateViaNTP(bool value) {
    automaticTimeUpdateViaNTP = value;
    saveFile();
}

void KvxputerConfig::setTmz(float value) {
    tmz = value;
    validateTmzValue();
    saveFile();
}

void KvxputerConfig::validateTmzValue() {
    if (tmz < -12 || tmz > 14) tmz = 0;
}

void KvxputerConfig::setDST(bool value) {
    dst = value;
    saveFile();
}

void KvxputerConfig::setClock24Hr(bool value) {
    clock24hr = value;
    saveFile();
}

void KvxputerConfig::setSoundEnabled(int value) {
    soundEnabled = value;
    validateSoundEnabledValue();
    saveFile();
}

void KvxputerConfig::setSoundVolume(int value) {
    soundVolume = value;
    validateSoundVolumeValue();
    saveFile();
}

void KvxputerConfig::validateSoundEnabledValue() {
    if (soundEnabled > 1) soundEnabled = 1;
}

void KvxputerConfig::validateSoundVolumeValue() {
    if (soundVolume > 100) soundVolume = 100;
}

void KvxputerConfig::setWifiAtStartup(int value) {
    wifiAtStartup = value;
    validateWifiAtStartupValue();
    saveFile();
}

void KvxputerConfig::validateWifiAtStartupValue() {
    if (wifiAtStartup > 1) wifiAtStartup = 1;
}

#ifdef HAS_RGB_LED
void KvxputerConfig::setLedBright(int value) {
    ledBright = value;
    validateLedBrightValue();
    saveFile();
}

void KvxputerConfig::validateLedBrightValue() { ledBright = max(0, min(100, ledBright)); }

void KvxputerConfig::setLedColor(uint32_t value) {
    ledColor = value;
    validateLedColorValue();
    saveFile();
}

void KvxputerConfig::validateLedColorValue() {
    ledColor = max<uint32_t>(0, min<uint32_t>(0xFFFFFFFF, ledColor));
}

void KvxputerConfig::setLedBlinkEnabled(int value) {
    ledBlinkEnabled = value;
    validateLedBlinkEnabledValue();
    saveFile();
}

void KvxputerConfig::validateLedBlinkEnabledValue() {
    if (ledBlinkEnabled > 1) ledBlinkEnabled = 1;
}

void KvxputerConfig::setLedEffect(int value) {
    ledEffect = value;
    validateLedEffectValue();
    saveFile();
}

void KvxputerConfig::validateLedEffectValue() {
    if (ledEffect < 0 || ledEffect > 9) ledEffect = 0;
}

void KvxputerConfig::setLedEffectSpeed(int value) {
    ledEffectSpeed = value;
    validateLedEffectSpeedValue();
    saveFile();
}

void KvxputerConfig::validateLedEffectSpeedValue() {
#ifdef HAS_ENCODER_LED
    if (ledEffectSpeed > 11) ledEffectSpeed = 11;
#else
    if (ledEffectSpeed > 10) ledEffectSpeed = 10;
#endif
    if (ledEffectSpeed < 0) ledEffectSpeed = 1;
}

void KvxputerConfig::setLedEffectDirection(int value) {
    ledEffectDirection = value;
    validateLedEffectDirectionValue();
    saveFile();
}

void KvxputerConfig::validateLedEffectDirectionValue() {
    if (ledEffectDirection > 1 || ledEffectDirection == 0) ledEffectDirection = 1;
    if (ledEffectDirection < -1) ledEffectDirection = -1;
}
#endif

void KvxputerConfig::setWebUICreds(const String &usr, const String &pwd) {
    webUI.user = usr;
    webUI.pwd = pwd;
    saveFile();
}

void KvxputerConfig::setWifiApCreds(const String &ssid, const String &pwd) {
    wifiAp.ssid = ssid;
    wifiAp.pwd = pwd;
    saveFile();
}

void KvxputerConfig::setTerminalLog(bool value) {
    TerminalLog = value;
    saveFile();
}

void KvxputerConfig::addWifiCredential(const String &ssid, const String &pwd) {
    wifi[ssid] = pwd;
    saveFile();
}

String KvxputerConfig::getWifiPassword(const String &ssid) const {
    auto it = wifi.find(ssid);
    if (it != wifi.end()) return it->second;
    return "";
}

void KvxputerConfig::addEvilWifiName(String value) {
    evilWifiNames.insert(value);
    saveFile();
}

void KvxputerConfig::removeEvilWifiName(String value) {
    evilWifiNames.erase(value);
    saveFile();
}

void KvxputerConfig::setEvilEndpointCreds(String value) {
    evilPortalEndpoints.getCredsEndpoint = value;
    validateEvilEndpointCreds();
    saveFile();
}

void KvxputerConfig::validateEvilEndpointCreds() {
    if (evilPortalEndpoints.getCredsEndpoint == evilPortalEndpoints.setSsidEndpoint) {
        // on collision reset to defaults
        evilPortalEndpoints.getCredsEndpoint = "/creds";
    }
    if (evilPortalEndpoints.getCredsEndpoint[0] != '/') {
        evilPortalEndpoints.getCredsEndpoint = '/' + evilPortalEndpoints.getCredsEndpoint;
    }
}

void KvxputerConfig::setEvilEndpointSsid(String value) {
    evilPortalEndpoints.setSsidEndpoint = value;
    validateEvilEndpointCreds();
    saveFile();
}

void KvxputerConfig::validateEvilEndpointSsid() {
    if (evilPortalEndpoints.getCredsEndpoint == evilPortalEndpoints.setSsidEndpoint) {
        // on collision reset to defaults
        evilPortalEndpoints.setSsidEndpoint = "/ssid";
    }
    if (evilPortalEndpoints.setSsidEndpoint[0] != '/') {
        evilPortalEndpoints.setSsidEndpoint = '/' + evilPortalEndpoints.setSsidEndpoint;
    }
}

void KvxputerConfig::setEvilAllowEndpointDisplay(bool value) {
    evilPortalEndpoints.showEndpoints = value;
    saveFile();
}

void KvxputerConfig::setEvilAllowGetCreds(bool value) {
    evilPortalEndpoints.allowGetCreds = value;
    saveFile();
}

void KvxputerConfig::setEvilAllowSetSsid(bool value) {
    evilPortalEndpoints.allowSetSsid = value;
    saveFile();
}

void KvxputerConfig::setEvilPasswordMode(EvilPortalPasswordMode value) {
    evilPortalPasswordMode = value;
    saveFile();
}

void KvxputerConfig::validateEvilPasswordMode() {
    if (evilPortalPasswordMode < 0 || evilPortalPasswordMode > 2) evilPortalPasswordMode = FULL_PASSWORD;
}

void KvxputerConfig::setEvilGatewayIp(String value) {
    evilPortalGatewayIp = value;
    validateEvilGatewayIp();
    saveFile();
}

void KvxputerConfig::validateEvilGatewayIp() {
    IPAddress gatewayIp;
    if (!gatewayIp.fromString(evilPortalGatewayIp)) evilPortalGatewayIp = "172.0.0.1";
}

void KvxputerConfig::setStartupApp(String value) {
    startupApp = value;
    saveFile();
}

void KvxputerConfig::setStartupAppJSInterpreterFile(String value) {
    startupAppJSInterpreterFile = value;
    saveFile();
}

void KvxputerConfig::setWigleBasicToken(String value) {
    wigleBasicToken = value;
    saveFile();
}

void KvxputerConfig::setWdgwarsApiKey(String value) {
    wdgwarsApiKey = value;
    saveFile();
}

void KvxputerConfig::setDevMode(int value) {
    devMode = value;
    validateDevModeValue();
    saveFile();
}

void KvxputerConfig::validateDevModeValue() {
    if (devMode > 1) devMode = 1;
}

void KvxputerConfig::setColorInverted(int value) {
    colorInverted = value;
    validateColorInverted();
    saveFile();
}

void KvxputerConfig::validateColorInverted() {
    if (colorInverted > 1) colorInverted = 1;
}

void KvxputerConfig::setBadUSBBLEKeyboardLayout(int value) {
    badUSBBLEKeyboardLayout = value;
    validateBadUSBBLEKeyboardLayout();
    saveFile();
}

void KvxputerConfig::validateBadUSBBLEKeyboardLayout() {
    if (badUSBBLEKeyboardLayout < 0 || badUSBBLEKeyboardLayout > 13) badUSBBLEKeyboardLayout = 0;
}

void KvxputerConfig::setBadUSBBLEKeyDelay(uint16_t value) {
    badUSBBLEKeyDelay = value;
    validateBadUSBBLEKeyDelay();
    saveFile();
}

void KvxputerConfig::validateBadUSBBLEKeyDelay() {
    if (badUSBBLEKeyDelay < 0) badUSBBLEKeyDelay = 0;
    if (badUSBBLEKeyDelay > 500) badUSBBLEKeyDelay = 500;
}

void KvxputerConfig::setBadUSBBLEShowOutput(bool value) {
    badUSBBLEShowOutput = value;
    saveFile();
}

void KvxputerConfig::setUnitScrollEnabled(bool value) {
    unitScrollEnabled = value;
    saveFile();
}

void KvxputerConfig::setUnitScrollInvert(bool value) {
    unitScrollInvert = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteTransport(int value) {
    hidRemoteTransport = value & 1;
    saveFile();
}

void KvxputerConfig::setHidRemoteBleName(const String &value) {
    hidRemoteBleName = value.substring(0, 20);
    saveFile();
}

void KvxputerConfig::setHidRemoteLastMode(int value) {
    if (value < 0) value = 0;
    if (value > 11) value = 11;
    hidRemoteLastMode = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteMouseSensitivity(int value) {
    if (value < 1) value = 1;
    if (value > 10) value = 10;
    hidRemoteMouseSensitivity = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteJigglerInterval(int value) {
    if (value < 5) value = 5;
    if (value > 600) value = 600;
    hidRemoteJigglerInterval = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteStealthMin(int value) {
    if (value < 10) value = 10;
    hidRemoteStealthMin = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteStealthMax(int value) {
    if (value < hidRemoteStealthMin) value = hidRemoteStealthMin + 10;
    if (value > 900) value = 900;
    hidRemoteStealthMax = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteClickerDelay(int value) {
    if (value < 10) value = 10;
    if (value > 10000) value = 10000;
    hidRemoteClickerDelay = value;
    saveFile();
}

void KvxputerConfig::setHidRemoteClickerButton(int value) {
    if (value < 0) value = 0;
    if (value > 2) value = 2;
    hidRemoteClickerButton = value;
    saveFile();
}

void KvxputerConfig::setHidRemotePttPreset(int value) {
    if (value < 0) value = 0;
    if (value > 4) value = 4;
    hidRemotePttPreset = value;
    saveFile();
}

void KvxputerConfig::ensureMifareKeysLoaded() {
    if (!_mifareKeysLoaded) {
        MifareKeysManager::ensureLoaded(mifareKeys);
        _mifareKeysLoaded = true;
    }
}

void KvxputerConfig::addMifareKey(String value) {
    ensureMifareKeysLoaded();
    MifareKeysManager::addKey(mifareKeys, value);
}

void KvxputerConfig::validateMifareKeysItems() {
    if (_mifareKeysLoaded) MifareKeysManager::validateKeys(mifareKeys);
}

void KvxputerConfig::addDisabledMenu(String value) {
    if (std::find(disabledMenus.begin(), disabledMenus.end(), value) != disabledMenus.end()) return;
    disabledMenus.push_back(value);
    saveFile();
}

void KvxputerConfig::removeDisabledMenu(String value) {
    auto it = std::find(disabledMenus.begin(), disabledMenus.end(), value);
    if (it == disabledMenus.end()) return;
    disabledMenus.erase(it);
    saveFile();
}

void KvxputerConfig::addQrCodeEntry(const String &menuName, const String &content) {
    qrCodes.push_back({menuName, content});
    saveFile();
}

void KvxputerConfig::removeQrCodeEntry(const String &menuName) {
    size_t writeIndex = 0;

    for (size_t readIndex = 0; readIndex < qrCodes.size(); ++readIndex) {
        const QrCodeEntry &entry = qrCodes[readIndex];

        if (entry.menuName != menuName) {
            if (writeIndex != readIndex) { qrCodes[writeIndex] = std::move(qrCodes[readIndex]); }
            ++writeIndex;
        }
    }

    if (writeIndex < qrCodes.size()) { qrCodes.erase(qrCodes.begin() + writeIndex, qrCodes.end()); }

    saveFile();
}

void KvxputerConfig::addWebUISession(const String &token) {
    webUISessions.push_back(token);
    // Limit to maximum 5 sessions - remove oldest (first element) if exceeded
    if (webUISessions.size() > 5) { webUISessions.erase(webUISessions.begin()); }
    saveFile();
}

void KvxputerConfig::removeWebUISession(const String &token) {
    for (auto it = webUISessions.begin(); it != webUISessions.end(); ++it) {
        if (*it == token) {
            webUISessions.erase(it);
            break;
        }
    }
    saveFile();
}

bool KvxputerConfig::isValidWebUISession(const String &token) {
    auto it = std::find(webUISessions.begin(), webUISessions.end(), token);

    if (it == webUISessions.end()) {
        return false; // Token not found
    }

    // Check if token is already at the end (most recent position)
    if (it == webUISessions.end() - 1) {
        return true; // Already most recent, no changes needed
    }

    // Move token to end and save
    webUISessions.erase(it);
    webUISessions.push_back(token);

    // Limit to maximum 10 sessions
    if (webUISessions.size() > 10) { webUISessions.erase(webUISessions.begin()); }

    saveFile();
    return true;
}

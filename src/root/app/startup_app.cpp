/**
 * @file startup_app.cpp
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Bruce startup apps
 * @version 0.1
 * @date 2024-11-20
 */

#include "root/app/startup_app.h"

#include "menu_registry.h"
#include "menu/scripts/scripts_menu.h"
#include "root/net/webInterface.h"
#include "root/net/wifi_common.h"
#include "root/scripting/bjs_interpreter/interpreter.h"
#include "menu/gps/gps_tracker.h"
#include "menu/gps/wardriving.h"
#include "menu/wifi/pwnagotchi/pwnagotchi.h"
#include "menu/rf/rf_send.h"
#include "menu/rfid/PN532KillerTools.h"
#include "menu/rfid/pn532ble.h"
#include "menu/wifi/sniffer.h"
#ifdef SOC_USB_OTG_SUPPORTED
#include "root/storage/massStorage.h"
#endif

StartupApp::StartupApp() {
    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (!menuDescriptorAvailable(desc)) continue;
        MenuItemInterface *item = desc.item;
        _startupApps[String(desc.label)] = [item]() { item->optionsMenu(); };
    }

#ifndef LITE_VERSION
    _startupApps["Kvxgotchi"] = []() { kvxgotchi_start(); };
    _startupApps["Sniffer"] = []() { sniffer_setup(); };
    _startupApps["GPS Tracker"] = []() { GPSTracker(); };
    _startupApps["PN532 BLE"] = []() { Pn532ble(); };
    _startupApps["PN532 UART"] = []() { PN532KillerTools(); };
#endif
    _startupApps["Custom SubGHz"] = []() { sendCustomRF(); };
#if defined(SOC_USB_OTG_SUPPORTED)
    _startupApps["Mass Storage"] = []() { MassStorage(); };
#endif
    _startupApps["Wardriving"] = []() { Wardriving(true, true); };
    _startupApps["WardrivingNoRadio"] = []() { Wardriving(); };
    _startupApps["WardrivingBTEOnly"] = []() { Wardriving(false, true); };
    _startupApps["WardrivingWifiOnly"] = []() { Wardriving(true, false); };
    _startupApps["WebUI"] = []() { startWebUi(!wifiConnecttoKnownNet()); };
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    _startupApps["JS Interpreter"] = []() {
        FS *fs = nullptr;
        getScriptsFolder(fs);
        if (fs == nullptr) return;
        run_bjs_script_headless(*fs, kvxConfig.startupAppJSInterpreterFile);
    };
#endif
}

bool StartupApp::startApp(const String &appName) const {
    auto it = _startupApps.find(appName);
    if (it == _startupApps.end()) {
        Serial.println("Invalid startup app: " + appName);
        return false;
    }

    it->second();

    delay(200);
    tft.fillScreen(kvxConfig.bgColor);

    return true;
}

std::vector<String> StartupApp::getAppNames() const {
    std::vector<String> keys;
    for (const auto &pair : _startupApps) { keys.push_back(pair.first); }
    return keys;
}

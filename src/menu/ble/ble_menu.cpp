#include "ble_menu.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "menu/ble/ble_common.h"
#include "menu/ble/ble_ninebot.h"
#include "menu/ble/ble_spam.h"
#if !defined(LITE_VERSION)
#include "menu/ble/BLE_Suite.h"
#else
#include "menu/ble/ble_sniffer.h"
#endif
#if defined(EVIL_EXTENSIONS)
#include "menu/ble/name_flood/name_flood.h"
#include "menu/ble/wall_of_airtag/wall_of_airtag.h"
#include "menu/ble/findmy/findmy.h"
#endif
#include "menu/ble/hid_remote/hid_remote.h"
#include <globals.h>

void BleMenu::optionsMenu() {
    options.clear();
#if !defined(LITE_VERSION)
    if (BLEConnected) {
        options.push_back({"Disconnect", [=]() {
                               BLEDevice::deinit();
                               BLEConnected = false;
                               delete hid_ble;
                               hid_ble = nullptr;
                           }});
    }
#endif
#if !defined(LITE_VERSION)
    options.push_back({"HID Remote", [=]() { hidRemoteMenu(HID_REMOTE_LAUNCH_BLE); }});
    options.push_back({"Media Cmds (legacy)", [=]() { MediaCommands(hid_ble, true); }});
    options.push_back({"BLE Scan", ble_scan});
    options.push_back({"iBeacon", [=]() {
                           ibeacon("Bruce", "e4c159a0-8c82-11e6-bdf4-0800200c9a66", 0x004C);
                       }});
    options.push_back({"Bad BLE", [=]() { ducky_setup(hid_ble, true); }});
    options.push_back({"BLE Keyboard (legacy)", [=]() { ducky_keyboard(hid_ble, true); }});
#endif
    options.push_back({"BLE Spam", [=]() { spamMenu(); }});

#if !defined(LITE_VERSION)
    options.push_back({"BLE Suite", [=]() { BleSuiteMenu(); }});
    options.push_back({"Ninebot", [=]() { BLENinebot(); }});
    options.push_back({"Presenter (legacy)", [=]() { PresenterMode(hid_ble, true); }});
#else
    options.push_back({"BLE Sniffer", [=]() { BLE_SnifferMenu(); }});
#endif
#if defined(EVIL_EXTENSIONS)
    options.push_back({"BLE Name Flood", nameFloodMenu});
    options.push_back({"Wall Of Airtag", wallOfAirtagMenu});
    options.push_back({"FindMyEvil", findMyMenu});
#endif
    addOptionToMainMenu();

    loopOptions(options, MENU_TYPE_SUBMENU, "Bluetooth", 0, false);
}

void BleMenu::drawIcon(float scale) {
    clearIconArea();

    int lineWidth = scale * 5;
    int iconW = scale * 36;
    int iconH = scale * 60;
    int radius = scale * 5;
    int deltaRadius = scale * 10;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 4 != 0) iconH += 4 - (iconH % 4);

    tft.drawWideLine(
        iconCenterX,
        iconCenterY + iconH / 4,
        iconCenterX - iconW,
        iconCenterY - iconH / 4,
        lineWidth,
        kvxConfig.priColor,
        kvxConfig.priColor
    );
    tft.drawWideLine(
        iconCenterX,
        iconCenterY - iconH / 4,
        iconCenterX - iconW,
        iconCenterY + iconH / 4,
        lineWidth,
        kvxConfig.priColor,
        kvxConfig.priColor
    );
    tft.drawWideLine(
        iconCenterX,
        iconCenterY + iconH / 4,
        iconCenterX - iconW / 2,
        iconCenterY + iconH / 2,
        lineWidth,
        kvxConfig.priColor,
        kvxConfig.priColor
    );
    tft.drawWideLine(
        iconCenterX,
        iconCenterY - iconH / 4,
        iconCenterX - iconW / 2,
        iconCenterY - iconH / 2,
        lineWidth,
        kvxConfig.priColor,
        kvxConfig.priColor
    );

    tft.drawWideLine(
        iconCenterX - iconW / 2,
        iconCenterY - iconH / 2,
        iconCenterX - iconW / 2,
        iconCenterY + iconH / 2,
        lineWidth,
        kvxConfig.priColor,
        kvxConfig.priColor
    );

    tft.drawArc(
        iconCenterX,
        iconCenterY,
        2.5 * radius,
        2 * radius,
        210,
        330,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        210,
        330,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        210,
        330,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

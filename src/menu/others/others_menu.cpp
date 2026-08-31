#include "others_menu.h"

#include "root/ui/display.h"
#include "root/app/utils.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "root/scripting/bjs_interpreter/interpreter.h"
#include "menu/others/clicker.h"
#include "menu/others/ibutton.h"
#include "menu/others/mic.h"
#include "menu/others/qrcode_menu.h"
#include "menu/others/u2f.h"
#include "menu/ble/hid_remote/hid_remote.h"
#if defined(EVIL_EXTENSIONS)
#include "menu/others/llm_chat/llm_chat.h"
#endif

void OthersMenu::optionsMenu() {
    options = {
        {"QRCodes",      qrcode_menu                  },

#if defined(MIC_SPM1423) || defined(MIC_INMP441)
        {"Microphone",   [this]() { micMenu(); }      },
#endif

#if !defined(LITE_VERSION)
        {"BadUSB & HID", [this]() { badUsbHidMenu(); }},
#endif

#ifndef LITE_VERSION
        {"iButton",      setup_ibutton                },
#endif
#if defined(EVIL_EXTENSIONS)
        {"LLM Chat",     llmChatMenu                  },
#endif
    };

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Others");
}

void OthersMenu::badUsbHidMenu() {
    options = {
#ifndef LITE_VERSION
        {"HID Remote",   [=]() { hidRemoteMenu(HID_REMOTE_LAUNCH_USB); }},
        {"BadUSB",       [=]() { ducky_setup(hid_usb, false); }   },
        {"USB Keyboard (legacy)", [=]() { ducky_keyboard(hid_usb, false); }},
#endif

#ifdef USB_as_HID
        {"USB Clicker (legacy)",  clicker_setup                            },
        {"USB U2F",      u2f_setup                                },
#endif

        {"Back",         [this]() { optionsMenu(); }              },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "BadUSB & HID");
}

void OthersMenu::micMenu() {
    options = {
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
        {"Spectrum", mic_test                   },
        {"Record",   mic_record_app             },
#endif
        {"Back",     [this]() { optionsMenu(); }},
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "Microphone");
}

void OthersMenu::drawIcon(float scale) {
    clearIconArea();

    // Dynamic radius calculation based on scale for responsive rendering
    int radius = scale * 7;

    // Center circle
    tft.fillCircle(iconCenterX, iconCenterY, radius, kvxConfig.priColor);

    // Concentric arcs - dynamically scaled for different screen sizes
    tft.drawArc(
        iconCenterX, iconCenterY, 2.5 * radius, 2 * radius, 0, 340, kvxConfig.priColor, kvxConfig.bgColor
    );

    tft.drawArc(
        iconCenterX, iconCenterY, 3.5 * radius, 3 * radius, 20, 360, kvxConfig.priColor, kvxConfig.bgColor
    );

    tft.drawArc(
        iconCenterX, iconCenterY, 4.5 * radius, 4 * radius, 0, 200, kvxConfig.priColor, kvxConfig.bgColor
    );

    tft.drawArc(
        iconCenterX,
        iconCenterY,
        4.5 * radius,
        4 * radius,
        240,
        360,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

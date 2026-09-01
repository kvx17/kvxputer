#include "gps_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "menu/gps/gps_tracker.h"
#include "menu/gps/wardriving.h"
#if defined(EVIL_EXTENSIONS)
#include "menu/gps/wardriving_master/wardriving_master.h"
#endif
#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)
#include "menu/lora/LoRaRF.h"
#endif
#include <math.h>

void GpsMenu::optionsMenu() {
    options = {
        {"Wardriving",  [this]() { wardrivingMenu(); }},
#if !defined(LITE_VERSION)
        {"GPS Tracker", [=]() { GPSTracker(); }       },
#endif
        {"Config",      [this]() { configMenu(); }    },
    };
    addOptionToMainMenu();

    String txt = "GPS (" + String(kvxConfigPins.gpsBaudrate) + " bps)";
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void GpsMenu::wardrivingMenu() {
    options = {
        {"Scan WiFi Networks", []() { Wardriving(true, false); }},
        {"Scan BLE Devices",   []() { Wardriving(false, true); }},
        {"Scan Both",          []() { Wardriving(true, true); } },
#if defined(EVIL_EXTENSIONS)
        {"Wardriving Master",  wardrivingMasterMenu             },
#endif
        {"Back",               [this]() { optionsMenu(); }      },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "Wardriving");
}
void GpsMenu::configMenu() {
    options = {
        {"Baudrate", setGpsBaudrateMenu                                 },
        {"GPS Pins", [=]() { setUARTPinsMenu(kvxConfigPins.gps_bus); }},
#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)
        {"Configure LoRa Cap", []() { configureLoraCap(); }            },
#endif
        {"Back",     [this]() { optionsMenu(); }                        },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "GPS Config");
}

void GpsMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 18;
    if (radius % 2 != 0) radius++;

    int tangentX = sqrt(radius * radius - (radius / 2 * radius / 2));
    int32_t tangentY = radius / 2;

    tft.fillCircle(iconCenterX, iconCenterY - radius / 2, radius, kvxConfig.priColor);
    tft.fillTriangle(
        iconCenterX - tangentX,
        iconCenterY - radius / 2 + tangentY,
        iconCenterX + tangentX,
        iconCenterY - radius / 2 + tangentY,
        iconCenterX,
        iconCenterY + 1.5 * radius,
        kvxConfig.priColor
    );
    tft.fillCircle(iconCenterX, iconCenterY - radius / 2, radius / 2, kvxConfig.bgColor);

    tft.drawEllipse(iconCenterX, iconCenterY + 1.5 * radius, 1.5 * radius, radius / 2, kvxConfig.priColor);
}

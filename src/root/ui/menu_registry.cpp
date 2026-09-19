#include "menu_registry.h"
#include "root/ui/main_menu.h"
#include <globals.h>
#include <vector>

const MenuDescriptor kMenus[] = {
    {"wifi",       "WiFi",      &mainMenu.wifiMenu,      MENU_FLAG_NONE},
    {"discovery",  "Discovery", &mainMenu.discoveryMenu, MENU_FLAG_NONE},
    {"netops",     "NetOps",    &mainMenu.netOpsMenu,    MENU_FLAG_NONE},
    {"ble",       "BLE",       &mainMenu.bleMenu,      MENU_FLAG_NONE},
    {"rf",        "RF",        &mainMenu.rfMenu,       MENU_FLAG_NONE},
    {"nrf24",     "NRF24",     &mainMenu.nrf24Menu,    MENU_FLAG_NONE},
#if !defined(LITE_VERSION)
    {"lora",      "LoRa",      &mainMenu.loraMenu,     MENU_FLAG_LITE_EXCLUDED | MENU_FLAG_BOARD_LORA},
#endif
    {"fm",        "FM",        &mainMenu.fmMenu,       MENU_FLAG_LITE_EXCLUDED | MENU_FLAG_BOARD_FM},
    {"infrared",  "Infrared",  &mainMenu.irMenu,       MENU_FLAG_NONE},
#if !defined(LITE_VERSION)
    {"ethernet",  "Ethernet",  &mainMenu.ethernetMenu, MENU_FLAG_LITE_EXCLUDED | MENU_FLAG_BOARD_ETHERNET},
#endif
    {"usb",       "USB",       &mainMenu.usbMenu,      MENU_FLAG_LITE_EXCLUDED},
    {"gps",       "GPS",       &mainMenu.gpsMenu,      MENU_FLAG_NONE},
    {"rfid",      "RFID",      &mainMenu.rfidMenu,     MENU_FLAG_NONE},
    {"files",     "Files",     &mainMenu.fileMenu,     MENU_FLAG_NONE},
    {"scripts",   "Scripts",   &mainMenu.scriptsMenu,  MENU_FLAG_LITE_EXCLUDED | MENU_FLAG_SCRIPTS},
    {"clock",     "Clock",     &mainMenu.clockMenu,    MENU_FLAG_NONE},
    {"charge",    "Charge",    &mainMenu.chargeMenu,   MENU_FLAG_NONE},
    {"others",    "Tools",     &mainMenu.othersMenu,   MENU_FLAG_NONE},
    {"modules",   "Modules",   &mainMenu.modulesMenu,  MENU_FLAG_NONE},
    {"config",    "Config",    &mainMenu.configMenu,   MENU_FLAG_NONE},
};

const size_t kMenuCount = sizeof(kMenus) / sizeof(kMenus[0]);

std::vector<MenuItemInterface *> buildVisibleMenus() {
    std::vector<MenuItemInterface *> visible;
    std::vector<String> disabled = kvxConfig.disabledMenus;

    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (!menuDescriptorAvailable(desc)) continue;

        bool enabled = true;
        for (const String &d : disabled) {
            if (d == desc.id || d == desc.label) {
                enabled = false;
                break;
            }
        }
        if (enabled) visible.push_back(desc.item);
    }
    return visible;
}

#ifndef __MAIN_MENU_H__
#define __MAIN_MENU_H__

#include <MenuItemInterface.h>

#include "menu/wifi/wifi_menu.h"
#include "menu/netops/netops_menu.h"
#include "menu/ble/ble_menu.h"
#include "menu/rf/rf_menu.h"
#include "menu/rfid/rfid_menu.h"
#include "menu/lora/lora_menu.h"
#include "menu/infrared/infrared_menu.h"
#include "menu/gps/gps_menu.h"
#include "menu/files/files_menu.h"
#include "menu/scripts/scripts_menu.h"
#include "menu/clock/clock_menu.h"
#include "menu/charge/charge_menu.h"
#include "menu/others/others_menu.h"
#include "menu/config/config_menu.h"
#include "menu/modules/modules_menu.h"
#include "menu/ethernet/ethernet_menu.h"
#include "menu/usb/usb_menu.h"
#include "menu/fm/fm_menu.h"
#include "menu/nrf24/nrf24_menu.h"
#include "menu/connect/connect_menu.h"
#include "menu/discovery/discovery_menu.h"

class MainMenu {
public:
    FileMenu fileMenu;
    BleMenu bleMenu;
    ClockMenu clockMenu;
    ChargeMenu chargeMenu;
    ConnectMenu connectMenu;
    DiscoveryMenu discoveryMenu;
    ModulesMenu modulesMenu;
    ConfigMenu configMenu;
    FMMenu fmMenu;
    GpsMenu gpsMenu;
    IRMenu irMenu;
    NRF24Menu nrf24Menu;
    NetOpsMenu netOpsMenu;
    OthersMenu othersMenu;
    RFIDMenu rfidMenu;
    RFMenu rfMenu;
    ScriptsMenu scriptsMenu;
    UsbMenu usbMenu;
    WifiMenu wifiMenu;
#if !defined(LITE_VERSION)
    LoRaMenu loraMenu;
    EthernetMenu ethernetMenu;
#endif

    MainMenu();
    ~MainMenu();

    void begin(void);
    std::vector<MenuItemInterface *> getItems(void) { return _menuItems; }
    void hideAppsMenu();

private:
    int _currentIndex = 0;
    int _totalItems = 0;
    std::vector<MenuItemInterface *> _menuItems;
};

extern MainMenu mainMenu;

#endif

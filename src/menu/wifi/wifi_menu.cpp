#include "wifi_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "root/net/webInterface.h"
#include "root/net/wg.h"
#include "root/net/wifi_common.h"
#include "root/net/wifi_mac.h"
#include "menu/ethernet/ARPScanner.h"
#include "menu/wifi/ap_info.h"
#include "menu/wifi/clients.h"
#include "menu/wifi/evil_portal.h"
#include "menu/wifi/karma_attack.h"
#include "menu/wifi/netcut.h"
#include "menu/wifi/responder.h"
#include "menu/wifi/scan_hosts.h"
#include "menu/wifi/sniffer.h"
#include "menu/wifi/wifi_atks.h"

#ifndef LITE_VERSION
#include "menu/wifi/pwnagotchi/pwnagotchi.h"
#include "menu/wifi/channel_analyzer.h"
#include "menu/wifi/jam_detect.h"
#include "menu/wifi/wifi_recover.h"
#endif

// #include "menu/netops/reverseShell/reverseShell.h"
//  Developed by Fourier (github.com/9dl)
//  Use BruceC2 to interact with the reverse shell server
//  BruceC2: https://github.com/9dl/Bruce-C2
//  To use BruceC2:
//  1. Start Reverse Shell Mode in Bruce
//  2. Start BruceC2 and wait.
//  3. Visit 192.168.4.1 in your browser to access the web interface for shell executing.

// 32bit: https://github.com/9dl/Bruce-C2/releases/download/v1.0/BruceC2_windows_386.exe
// 64bit: https://github.com/9dl/Bruce-C2/releases/download/v1.0/BruceC2_windows_amd64.exe
#include "menu/wifi/socks4_proxy.h"
#include "menu/wifi/tcp_utils.h"

#if defined(EVIL_EXTENSIONS)
#include "menu/wifi/probe/probe.h"
#include "menu/wifi/handshake_master/handshake_master.h"
#include "menu/wifi/wall_of_flipper/wall_of_flipper.h"
#include "menu/wifi/dead_drop/dead_drop.h"
#include "menu/wifi/open_wifi/open_wifi.h"
#include "menu/wifi/aircrack/aircrack.h"
#include "menu/wifi/csi_radar/csi_radar.h"
#include "menu/wifi/c5_serial/c5_serial.h"
#endif

// global toggle - controls whether scanNetworks includes hidden SSIDs
bool showHiddenNetworks = false;

void WifiMenu::optionsMenu() {
    if (!forceHome) returnToMenu = false;
    options.clear();
    // Note: WiFi features will cleanly stop WebUI automatically when they start
    // User can navigate menu normally even with WebUI active
    if (!WiFi.isConnected() && !WiFi.AP.started()) {
        options = {
            {"Connect to Wifi", lambdaHelper(wifiConnectMenu, WIFI_STA)},
            {"Start WiFi AP", [=]() {
                 wifiConnectMenu(WIFI_AP);
                 displayInfo("pwd: " + kvxConfig.wifiAp.pwd, true);
             }},
        };
    }
    if (WiFi.getMode() != WIFI_MODE_NULL) { options.push_back({"Turn Off WiFi", wifiDisconnect}); }
    if (WiFi.getMode() & WIFI_MODE_STA && WiFi.isConnected()) {
        options.push_back({"AP info", displayAPInfo});
    }
    options.push_back({"Wifi Atks", wifi_atk_menu});
    options.push_back({"Evil Portal", [=]() {
                           // WebUI cleanup now handled automatically inside EvilPortal constructor
                           EvilPortal();
                       }});
    options.push_back({"NetCut", [=]() { netcutMenu(); }});
    // options.push_back({"ReverseShell", [=]()       { ReverseShell(); }});
#ifndef LITE_VERSION
    options.push_back({"Listen TCP", listenTcpPort});
    options.push_back({"Client TCP", clientTCP});
    options.push_back({"SOCKS4 Proxy", []() { socks4Proxy(1080); }});
    options.push_back({"TelNET", telnet_setup});
    options.push_back({"SSH", lambdaHelper(ssh_setup, String(""))});
    options.push_back({"Sniffer", sniffer_setup});
    options.push_back({"Channel Analyzer", channel_analyzer_setup});
    options.push_back({"Jam Detect", jam_detect_setup});
    options.push_back({"Scan Hosts", [=]() {
                           bool doScan = true;
                           if (!WiFi.isConnected()) doScan = wifiConnectMenu();

                           if (doScan) {
                               esp_netif_t *esp_netinterface =
                                   esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                               if (esp_netinterface == nullptr) {
                                   Serial.println("Failed to get netif handle");
                                   return;
                               }
                               ARPScanner{esp_netinterface};
                           }
                       }});
    options.push_back({"Wireguard", wg_setup});
    options.push_back({"Responder", responder});
    options.push_back({"Kvxgotchi", kvxgotchi_start});
    options.push_back({"WiFi Pass Recovery", wifi_recover_menu});
#endif

    options.push_back({"Config", [this]() { configMenu(); }});

#if defined(EVIL_EXTENSIONS)
    options.push_back({"Probes", probeMenu});
    options.push_back({"Handshakes", handshakeMasterMenu});
    options.push_back({"Wall Of Flipper", wallOfFlipperMenu});
    options.push_back({"WiFi Dead Drop", deadDropMenu});
    options.push_back({"Open Wifi Checker", openWifiMenu});
    options.push_back({"Aircrack", aircrackMenu});
    options.push_back({"CSI Radar", csiRadarMenu});
    options.push_back({"ESP32C5 Serial", c5SerialMenu});
#endif

    addOptionToMainMenu();

    loopOptions(options, MENU_TYPE_SUBMENU, "WiFi");

    options.clear();
}

void WifiMenu::configMenu() {
    std::vector<Option> wifiOptions;

    wifiOptions.push_back({"Change MAC", wifiMACMenu});
    wifiOptions.push_back({"Add Evil Wifi", addEvilWifiMenu});
    wifiOptions.push_back({"Remove Evil Wifi", removeEvilWifiMenu});
    wifiOptions.push_back({kvxConfig.TerminalLog ? "SSH/Telnet Log OFF" : "SSH/Telnet Log ON", [this]() {
                               kvxConfig.setTerminalLog(!kvxConfig.TerminalLog);
                               configMenu();
                           }});

    // Evil Wifi Settings submenu (unchanged)
    wifiOptions.push_back({"Evil Wifi Settings", [this]() {
                               std::vector<Option> evilOptions;

                               evilOptions.push_back({"Set Gateway IP", setEvilGatewayIp});
                               evilOptions.push_back({"Password Mode", setEvilPasswordMode});
                               evilOptions.push_back({"Rename /creds", setEvilEndpointCreds});
                               evilOptions.push_back({"Allow /creds access", setEvilAllowGetCreds});
                               evilOptions.push_back({"Rename /ssid", setEvilEndpointSsid});
                               evilOptions.push_back({"Allow /ssid access", setEvilAllowSetSsid});
                               evilOptions.push_back({"Display endpoints", setEvilAllowEndpointDisplay});
                               evilOptions.push_back({"Back", [this]() { configMenu(); }});
                               loopOptions(evilOptions, MENU_TYPE_SUBMENU, "Evil Wifi Settings");
                           }});

    {

        String hidden__wifi_option = String("Hidden Networks:") + (showHiddenNetworks ? "ON" : "OFF");

        // construct Option explicitly using char* label
        Option opt(hidden__wifi_option.c_str(), [this]() {
            showHiddenNetworks = !showHiddenNetworks;
            displayInfo(String("Hidden Networks:") + (showHiddenNetworks ? "ON" : "OFF"), true);
            configMenu();
        });

        wifiOptions.push_back(opt);
    }
    wifiOptions.push_back({"Back", [this]() { optionsMenu(); }});
    loopOptions(wifiOptions, MENU_TYPE_SUBMENU, "WiFi Config");
}

void WifiMenu::drawIcon(float scale) {
    clearIconArea();
    int deltaY = scale * 20;
    int radius = scale * 6;

    tft.fillCircle(iconCenterX, iconCenterY + deltaY, radius, kvxConfig.priColor);
    tft.drawArc(
        iconCenterX,
        iconCenterY + deltaY,
        deltaY + radius,
        deltaY,
        130,
        230,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY + deltaY,
        2 * deltaY + radius,
        2 * deltaY,
        130,
        230,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

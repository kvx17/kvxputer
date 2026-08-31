/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "open_wifi.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <globals.h>

extern bool showHiddenNetworks;

void openWifiMenu() {
    drawMainBorderWithTitle("Open Wifi");
    displayTextLine("Scanning...");
    int n = WiFi.scanNetworks(false, showHiddenNetworks);
    std::vector<Option> opts;
    int openCount = 0;
    for (int i = 0; i < n; i++) {
        if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) continue;
        openCount++;
        String label = WiFi.SSID(i) + " ch" + String(WiFi.channel(i)) + " " + String(WiFi.RSSI(i));
        String ssid = WiFi.SSID(i);
        opts.push_back({label.c_str(), [ssid]() {
                            WiFi.mode(WIFI_STA);
                            WiFi.begin(ssid.c_str());
                            displayInfo("Connecting to\n" + ssid, true);
                        }});
    }
    if (opts.empty()) {
        displayError("No open APs", true);
        return;
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, ("Open APs (" + String(openCount) + ")").c_str());
}
#endif

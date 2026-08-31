/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * SkyJack: find WiFi SSIDs commonly used by consumer drones.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "skyjack.h"
#if defined(EVIL_EXTENSIONS)
#include "root/ui/display.h"
#include <WiFi.h>
#include <globals.h>

void skyjackMenu() {
    drawMainBorderWithTitle("SkyJack");
    displayTextLine("Scanning...");
    int n = WiFi.scanNetworks();
    std::vector<Option> opts;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        String low = ssid;
        low.toLowerCase();
        bool hit = low.indexOf("parrot") >= 0 || low.indexOf("ardrone") >= 0 || low.indexOf("bebop") >= 0 ||
                   low.indexOf("dji") >= 0 || low.indexOf("drone") >= 0 || low.indexOf("tello") >= 0;
        if (!hit) continue;
        String label = ssid + " ch" + String(WiFi.channel(i));
        opts.push_back({label.c_str(), [ssid]() {
                            displayInfo("Drone AP:\n" + ssid + "\nConnect via WiFi menu if open.", true);
                        }});
    }
    if (opts.empty()) {
        displayError("No drone SSIDs", true);
        return;
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "SkyJack");
}
#endif

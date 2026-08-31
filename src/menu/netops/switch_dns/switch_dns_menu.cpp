/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Switch DNS: DNSServer wildcard to AP or STA IP.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "switch_dns.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <globals.h>

void switchDnsMenu() {
    IPAddress ap = WiFi.softAPIP();
    IPAddress sta = WiFi.localIP();
    bool useAp = WiFi.AP.started() || (sta.toString() == "0.0.0.0");
    std::vector<Option> opts = {
        {"DNS -> AP " + ap.toString(), [&]() { useAp = true; }},
        {"DNS -> STA " + sta.toString(), [&]() { useAp = false; }},
        {"Cancel", []() {}},
    };
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Switch DNS");
    if (sel < 0 || sel == 2) return;
    IPAddress ip = useAp ? ap : sta;
    if (ip.toString() == "0.0.0.0") {
        displayError("Interface has no IP", true);
        return;
    }
    static DNSServer dns;
    dns.stop();
    if (!dns.start(53, "*", ip)) {
        displayError("DNS start failed", true);
        return;
    }
    drawMainBorderWithTitle("Switch DNS");
    tft.drawString("Wildcard -> " + ip.toString(), 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        dns.processNextRequest();
        delay(10);
    }
    dns.stop();
}
#endif

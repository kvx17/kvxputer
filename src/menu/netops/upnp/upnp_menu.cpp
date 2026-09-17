/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "upnp.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include <vector>

namespace {

std::vector<String> ssdpSearch() {
    std::vector<String> locs;
    WiFiUDP udp;
    udp.begin(1901);
    const char *msearch =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\nST: upnp:rootdevice\r\n\r\n";
    udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    udp.write((const uint8_t *)msearch, strlen(msearch));
    udp.endPacket();
    unsigned long until = millis() + 2500;
    char buf[512];
    while (millis() < until) {
        int n = udp.parsePacket();
        if (n > 0) {
            n = udp.read((uint8_t *)buf, sizeof(buf) - 1);
            buf[n] = 0;
            String s(buf);
            int i = s.indexOf("LOCATION:");
            if (i < 0) i = s.indexOf("Location:");
            if (i >= 0) {
                int e = s.indexOf("\r", i);
                String loc = s.substring(i + 9, e);
                loc.trim();
                bool dup = false;
                for (auto &x : locs)
                    if (x == loc) dup = true;
                if (!dup) locs.push_back(loc);
            }
        }
        delay(20);
    }
    udp.stop();
    return locs;
}

} // namespace

void upnpMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    while (true) {
        std::vector<Option> opts = {
            {"List UPnP Mapping",
             []() {
                 auto locs = ssdpSearch();
                 if (locs.empty()) {
                     displayError("No UPnP devices", true);
                     return;
                 }
                 std::vector<Option> items;
                 for (const auto &l : locs) {
                     String loc = l;
                     items.push_back({loc.substring(0, 28).c_str(), [loc]() { displayInfo(loc, true); }});
                 }
                 items.push_back({"Back", []() {}});
                 loopOptions(items, MENU_TYPE_SUBMENU, "UPnP devices");
             }},
            {"UPnP NAT",
             []() {
                 displayInfo("AddPortMapping SOAP is host-specific.\nList devices first, then use a PC "
                             "SOAP client against the LOCATION URL.",
                             true);
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "UPnP Tools");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

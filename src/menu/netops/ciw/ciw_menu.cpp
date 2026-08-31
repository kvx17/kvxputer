/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ciw.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <globals.h>

void ciwMenu() {
    FS *fs = nullptr;
    getFsStorage(fs);
    String payload = "{\"payloads\":[]}";
    if (fs) {
        File f = fs->open(String(kvx::paths::NETOPS_CIW) + "/payloads.json", FILE_READ);
        if (f) {
            payload = f.readString();
            f.close();
        }
    }
    String ssid = kvxConfig.wifiAp.ssid.length() ? kvxConfig.wifiAp.ssid : "kvx-ciw";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 4);
    IPAddress ip = WiFi.softAPIP();
    WiFiServer http(80);
    http.begin();
    int hits = 0;
    drawMainBorderWithTitle("CIW Zeroclick");
    tft.drawString(ssid + " " + ip.toString(), 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        WiFiClient c = http.available();
        if (c) {
            hits++;
            while (c.connected() && !c.available()) delay(1);
            while (c.available()) c.read();
            c.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ");
            c.print(payload.length());
            c.print("\r\nConnection: close\r\n\r\n");
            c.print(payload);
            c.stop();
            tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Hits: " + String(hits), 10, 56);
        }
        delay(10);
    }
    http.end();
    WiFi.softAPdisconnect(true);
}
#endif

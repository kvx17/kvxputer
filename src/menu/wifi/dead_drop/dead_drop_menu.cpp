/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "dead_drop.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <globals.h>

void deadDropMenu() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_DEADDROP);
    String ssid = kvxConfig.wifiAp.ssid.length() ? kvxConfig.wifiAp.ssid : "kvx-drop";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid.c_str(), nullptr, 6, 0, 4);
    IPAddress ip = WiFi.softAPIP();
    WiFiServer http(80);
    http.begin();
    int hits = 0;
    drawMainBorderWithTitle("WiFi Dead Drop");
    tft.drawString(ssid + "  " + ip.toString(), 10, uiStatusY(0));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        WiFiClient c = http.available();
        if (c) {
            hits++;
            while (c.connected() && !c.available()) delay(1);
            String req = c.readStringUntil('\n');
            while (c.available()) c.read();
            String body = "<html><body><h1>kvxputer dead drop</h1><ul>";
            File dir = fs->open(kvx::paths::WIFI_DEADDROP);
            if (dir && dir.isDirectory()) {
                File f;
                while ((f = dir.openNextFile())) {
                    String n = f.name();
                    f.close();
                    int slash = n.lastIndexOf('/');
                    String base = slash >= 0 ? n.substring(slash + 1) : n;
                    body += "<li>" + base + "</li>";
                }
            }
            if (dir) dir.close();
            body += "</ul></body></html>";
            c.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: ");
            c.print(body.length());
            c.print("\r\nConnection: close\r\n\r\n");
            c.print(body);
            c.stop();
            tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Hits: " + String(hits), 10, uiStatusY(1));
            (void)req;
        }
        delay(10);
    }
    http.end();
    WiFi.softAPdisconnect(true);
}
#endif

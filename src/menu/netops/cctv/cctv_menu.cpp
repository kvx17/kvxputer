/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "cctv.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <globals.h>
#include <vector>

void cctvMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    std::vector<String> paths = {"/", "/login", "/web/", "/cgi-bin/", "/ISAPI/System/deviceInfo"};
    FS *fs = nullptr;
    if (getFsStorage(fs) && fs) {
        File f = fs->open(String(kvx::paths::NETOPS_CCTV) + "/CCTV_live.txt", FILE_READ);
        if (f) {
            while (f.available() && paths.size() < 20) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.startsWith("/")) paths.push_back(line);
            }
            f.close();
        }
    }
    IPAddress gw = WiFi.gatewayIP();
    int hits = 0;
    HTTPClient http;
    http.setTimeout(800);
    drawMainBorderWithTitle("CCTV Toolkit");
    EscPress = false;
    File log;
    if (fs) log = fs->open(String(kvx::paths::NETOPS_CCTV) + "/CCTV_scan.txt", FILE_WRITE);
    for (int i = 1; i < 30 && !check(EscPress); i++) {
        IPAddress ip(gw[0], gw[1], gw[2], i);
        String url = "http://" + ip.toString();
        tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString(url, 10, 40);
        if (!http.begin(url + "/")) continue;
        int code = http.GET();
        http.end();
        if (code > 0 && code < 500) {
            hits++;
            if (log) log.println(ip.toString() + " " + String(code));
        }
        delay(10);
    }
    if (log) log.close();
    displayInfo("HTTP hosts: " + String(hits), true);
}
#endif

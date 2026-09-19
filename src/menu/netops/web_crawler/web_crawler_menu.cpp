/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Web crawler: HTTP GET paths from crawler_wordlist.txt.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "web_crawler.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>

void webCrawlerMenu() {
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA)) return;
    }
    String base = keyboard("", 64, "IP/host[:port] or URL");
    if (base.length() == 0 || base == "\x1B") return;
    if (!base.startsWith("http://") && !base.startsWith("https://")) base = "http://" + base;
    while (base.endsWith("/")) base.remove(base.length() - 1);

    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    String listPath = String(kvx::paths::NETOPS_CRAWLER) + "/crawler_wordlist.txt";
    File list = fs->open(listPath, FILE_READ);
    if (!list) {
        displayError("No wordlist", true);
        return;
    }

    int hits = 0, tried = 0;
    drawMainBorderWithTitle("Web Crawler");
    tft.drawString(base, 10, uiStatusY(0));
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    HTTPClient http;
    http.setTimeout(2500);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    while (list.available() && !check(EscPress) && !returnToMenu) {
        String path = list.readStringUntil('\n');
        path.trim();
        if (path.length() == 0 || path.startsWith("#")) continue;
        if (!path.startsWith("/")) path = "/" + path;
        String url = base + path;
        tried++;
        if (!http.begin(url)) continue;
        int code = http.GET();
        http.end();
        if (code > 0 && code < 400) {
            hits++;
            Serial.printf("[crawler] %d %s\n", code, url.c_str());
        }
        if (tried % 3 == 0) {
            tft.fillRect(10, 56, tftWidth - 20, 32, kvxConfig.bgColor);
            tft.drawString("Tried " + String(tried) + "  hits " + String(hits), 10, uiStatusY(1));
            tft.drawString(path, 10, uiStatusY(2));
        }
        delay(20);
    }
    list.close();
    displayInfo("Tried " + String(tried) + "\nHits: " + String(hits), true);
}
#endif

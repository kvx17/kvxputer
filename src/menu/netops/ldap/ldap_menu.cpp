/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ldap.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>

void ldapMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String host = keyboard(WiFi.gatewayIP().toString(), 48, "LDAP host");
    if (host.length() == 0 || host == "\x1B") return;
    WiFiClient c;
    drawMainBorderWithTitle("LDAP Dump");
    tft.drawString("Connecting " + host + ":389", 10, 40);
    c.setTimeout(3);
    if (!c.connect(host.c_str(), 389)) {
        displayError("LDAP connect failed", true);
        return;
    }
    // LDAP BindRequest (anonymous) BER
    const uint8_t bind[] = {
        0x30, 0x0c, 0x02, 0x01, 0x01, 0x60, 0x07, 0x02, 0x01, 0x03, 0x04, 0x00, 0x80, 0x00
    };
    c.write(bind, sizeof(bind));
    unsigned long t = millis();
    String resp;
    while (millis() - t < 2000) {
        while (c.available()) resp += (char)c.read();
        if (resp.length() > 0 && millis() - t > 300) break;
        delay(20);
    }
    c.stop();
    displayInfo("LDAP :" + host + "\n" + String(resp.length()) + " bytes\n(anonymous bind)", true);
}
#endif

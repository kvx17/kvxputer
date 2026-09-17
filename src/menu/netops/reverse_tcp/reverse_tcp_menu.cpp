/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Reverse TCP tunnel: outbound TCP to a control host.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "reverse_tcp.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>

void reverseTcpMenu() {
    if (!WiFi.isConnected()) {
        if (!wifiConnectMenu(WIFI_STA)) return;
    }
    String host = keyboard("", 48, "TCP host");
    if (host.length() == 0 || host == "\x1B") return;
    String portStr = num_keyboard("4444", 5, "TCP port");
    if (portStr.length() == 0 || portStr == "\x1B") return;
    int port = portStr.toInt();
    if (port <= 0 || port > 65535) port = 4444;

    drawMainBorderWithTitle("Reverse TCP");
    tft.drawString(host + ":" + String(port), 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    WiFiClient client;
    unsigned long lastTry = 0;

    while (!check(EscPress) && !returnToMenu) {
        if (!client.connected()) {
            if (millis() - lastTry > 4000) {
                lastTry = millis();
                tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
                tft.drawString("Connecting...", 10, 56);
                client.connect(host.c_str(), port);
                if (client.connected()) tft.drawString("Connected", 10, 56);
            }
        } else {
            while (client.available()) {
                Serial.write(client.read());
            }
            while (Serial.available()) {
                client.write((uint8_t)Serial.read());
            }
        }
        delay(20);
    }
    client.stop();
    displayInfo("Tunnel closed", true);
}
#endif

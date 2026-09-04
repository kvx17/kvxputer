/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * CIW Zeroclick: rotate SSIDs from payloads.json (SSID injection test).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ciw.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <globals.h>
#include <vector>

namespace {

struct Pay {
    String t, c, d;
};

bool loadPayloads(std::vector<Pay> &out, String &err) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        err = "No storage";
        return false;
    }
    String path = String(kvx::paths::NETOPS_CIW) + "/payloads.json";
    File f = fs->open(path, FILE_READ);
    if (!f) {
        err = String("Missing\n") + path;
        return false;
    }
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) {
        err = e.c_str();
        return false;
    }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull() && doc["payloads"].is<JsonArray>()) arr = doc["payloads"].as<JsonArray>();
    for (JsonObject o : arr) {
        Pay p;
        p.t = o["t"] | "";
        p.c = o["c"] | "";
        p.d = o["d"] | "";
        if (p.t.length()) out.push_back(p);
    }
    return !out.empty();
}

void broadcastLoop(std::vector<Pay> &all, uint32_t rotMs) {
    if (all.empty()) return;
    size_t i = 0;
    unsigned long last = 0;
    drawMainBorderWithTitle("CIW Broadcast");
    tft.drawString("ESC stop", 10, tftHeight - 20);
    EscPress = false;
    WiFi.mode(WIFI_AP);
    while (!check(EscPress) && !returnToMenu) {
        if (millis() - last > rotMs) {
            last = millis();
            String ssid = all[i].t;
            if (ssid.length() > 32) ssid = ssid.substring(0, 32);
            WiFi.softAPdisconnect(true);
            delay(20);
            WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 4);
            tft.fillRect(8, 40, tftWidth - 16, 40, kvxConfig.bgColor);
            tft.drawString(all[i].c, 8, 40);
            tft.drawString(ssid.substring(0, 28), 8, 56);
            i = (i + 1) % all.size();
        }
        delay(20);
    }
    WiFi.softAPdisconnect(true);
}

} // namespace

void ciwMenu() {
    std::vector<Pay> all;
    String err;
    if (!loadPayloads(all, err)) {
        displayError(err, true);
        return;
    }
    uint32_t rotMs = 3000;
    while (true) {
        std::vector<Option> opts = {
            {"Start broadcast", [&]() { broadcastLoop(all, rotMs); }},
            {"Rotation " + String(rotMs / 1000) + "s",
             [&]() {
                 String s = keyboard(String(rotMs / 1000), 4, "Seconds");
                 if (s.length() && s != "\x1B") rotMs = constrain((int)s.toInt(), 1, 60) * 1000;
             }},
            {"Count " + String((int)all.size()), []() {}},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "CIW Zeroclick");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

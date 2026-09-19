/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "printer.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>

namespace {

bool probePort(IPAddress ip, uint16_t port, uint32_t timeoutMs = 250) {
    WiFiClient c;
    c.setTimeout(timeoutMs / 1000 + 1);
    bool ok = c.connect(ip, port);
    c.stop();
    return ok;
}

IPAddress detectPrinter() {
    IPAddress gw = WiFi.gatewayIP();
    IPAddress found(0, 0, 0, 0);
    drawMainBorderWithTitle("Detect Printer");
    for (int i = 1; i < 40 && !check(EscPress); i++) {
        IPAddress ip(gw[0], gw[1], gw[2], i);
        tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString("Probe " + ip.toString(), 10, uiStatusY(0));
        if (probePort(ip, 9100) || probePort(ip, 631)) {
            found = ip;
            break;
        }
        delay(5);
    }
    return found;
}

} // namespace

void printerMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    while (true) {
        std::vector<Option> opts = {
            {"Detect Printer",
             []() {
                 IPAddress ip = detectPrinter();
                 if (ip[0] == 0) displayError("None found", true);
                 else displayInfo("Printer?\n" + ip.toString(), true);
             }},
            {"File Print",
             []() {
                 String ipStr = keyboard(WiFi.gatewayIP().toString(), 16, "Printer IP");
                 if (ipStr.length() == 0 || ipStr == "\x1B") return;
                 IPAddress ip;
                 if (!ip.fromString(ipStr)) {
                     displayError("Bad IP", true);
                     return;
                 }
                 FS *fs = nullptr;
                 if (!getFsStorage(fs) || !fs) return;
                 String path = String(kvx::paths::NETOPS_PRINTER) + "/File-To-Print.txt";
                 File f = fs->open(path, FILE_READ);
                 if (!f) {
                     displayError("No File-To-Print.txt", true);
                     return;
                 }
                 WiFiClient c;
                 c.setTimeout(2);
                 if (!c.connect(ip, 9100)) {
                     f.close();
                     displayError("Connect :9100 failed", true);
                     return;
                 }
                 while (f.available()) c.write((uint8_t)f.read());
                 f.close();
                 c.stop();
                 displayInfo("Sent to " + ipStr, true);
             }},
            {"Check printer status",
             []() {
                 String ipStr = keyboard(WiFi.gatewayIP().toString(), 16, "Printer IP");
                 if (ipStr.length() == 0 || ipStr == "\x1B") return;
                 IPAddress ip;
                 if (!ip.fromString(ipStr)) return;
                 bool pjl = probePort(ip, 9100);
                 bool ipp = probePort(ip, 631);
                 displayInfo(ipStr + "\n9100: " + String(pjl ? "open" : "closed") + "\n631: " +
                                 String(ipp ? "open" : "closed"),
                             true);
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Printer Tools");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

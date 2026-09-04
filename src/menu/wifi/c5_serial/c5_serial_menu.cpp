/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * UART host for ESP32-C5 slave firmware (companion .bin on SD).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "c5_serial.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <HardwareSerial.h>
#include <ctype.h>
#include <globals.h>

namespace {

constexpr uint32_t C5_BAUD = 115200;
constexpr uint16_t C5_LINE_MAX = 240;
constexpr uint32_t C5_GLOBAL_TIMEOUT_MS = 12000;
constexpr uint32_t C5_IDLE_DONE_MS = 700;
constexpr uint8_t C5_MAX_AP = 50;

struct C5AP {
    uint16_t idx;
    uint8_t ch;
    int8_t rssi;
    char auth[16];
    char bssid[18];
    char ssid[33];
};

HardwareSerial c5uart(1);
bool gOn = false;
bool gDeauth = true;
bool gWard = false;
bool gSniff = false;
char gBand[6] = "ALL";
C5AP gAp[C5_MAX_AP];
uint8_t gApCount = 0;
int16_t gApCursor = 0;
bool gApSel[C5_MAX_AP];
char gRxline[C5_LINE_MAX + 4];
uint16_t gRxpos = 0;
uint32_t gLastAny = 0;
uint32_t gLastUseful = 0;
bool gCapList = false;
bool gSeenHeader = false;

void trimInplace(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

bool isUint(const char *s) {
    if (!s || !*s) return false;
    for (int i = 0; s[i]; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

void storeAp(uint16_t idx, uint8_t ch, int8_t rssi, const char *auth, const char *bssid, const char *ssid) {
    if (gApCount >= C5_MAX_AP) return;
    C5AP *a = &gAp[gApCount++];
    a->idx = idx;
    a->ch = ch;
    a->rssi = rssi;
    strncpy(a->auth, auth ? auth : "", sizeof(a->auth) - 1);
    a->auth[sizeof(a->auth) - 1] = 0;
    strncpy(a->bssid, bssid ? bssid : "", sizeof(a->bssid) - 1);
    a->bssid[sizeof(a->bssid) - 1] = 0;
    strncpy(a->ssid, ssid ? ssid : "", sizeof(a->ssid) - 1);
    a->ssid[sizeof(a->ssid) - 1] = 0;
}

void uartFlush() {
    if (!gOn) return;
    uint32_t t0 = millis();
    while (millis() - t0 < 50) {
        while (c5uart.available()) c5uart.read();
        delay(1);
    }
}

void sendLine(const char *cmd) {
    if (!gOn || !cmd) return;
    c5uart.print(cmd);
    c5uart.print("\n");
}

bool parseApline(const char *line) {
    if (!line || !line[0] || strchr(line, '|') == nullptr) return false;
    char buf[C5_LINE_MAX + 4];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *tok[10];
    int nt = 0;
    char *p = buf;
    while (p && nt < 10) {
        tok[nt++] = p;
        char *bar = strchr(p, '|');
        if (!bar) break;
        *bar = 0;
        p = bar + 1;
    }
    if (nt < 6) return false;
    for (int i = 0; i < nt; ++i) trimInplace(tok[i]);
    if (!strcmp(tok[0], "idx") && !strcmp(tok[1], "ch")) {
        gSeenHeader = true;
        return false;
    }
    if (!isUint(tok[0])) return false;
    int idx = atoi(tok[0]);
    int ch = atoi(tok[1]);
    int rssi = atoi(tok[2]);
    if (idx < 0 || idx > 9999 || ch < 0 || ch > 255) return false;
    storeAp((uint16_t)idx, (uint8_t)ch, (int8_t)rssi, tok[3], tok[4], tok[5]);
    gLastUseful = millis();
    return true;
}

void parseLine(const char *line) {
    gLastAny = millis();
    if (gCapList) parseApline(line);
}

void pollUart() {
    if (!gOn) return;
    while (c5uart.available()) {
        char c = (char)c5uart.read();
        if (c == '\r') continue;
        if (c == '\n') {
            gRxline[gRxpos] = 0;
            if (gRxpos > 0) parseLine(gRxline);
            gRxpos = 0;
            continue;
        }
        if (isprint((uint8_t)c) || c == '\t') {
            if (gRxpos + 1 < sizeof(gRxline)) gRxline[gRxpos++] = c;
            else gRxpos = 0;
        }
    }
}

void uartBegin() {
    if (gOn) return;
    c5uart.setRxBufferSize(2048);
    c5uart.begin(C5_BAUD, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    gOn = true;
    gRxpos = 0;
    uartFlush();
}

void uartEnd() {
    if (!gOn) return;
    c5uart.end();
    gOn = false;
}

bool checkPresent() {
    uartFlush();
    gLastAny = 0;
    sendLine("STOP");
    delay(100);
    sendLine("HELP");
    uint32_t t0 = millis();
    while (millis() - t0 < 700) {
        pollUart();
        if (gLastAny > 0) return true;
        delay(5);
    }
    return false;
}

void promptSend(const char *title, const char *prefix) {
    String v = keyboard("", 40, title);
    v.trim();
    if (!v.length() || v == "\x1B") return;
    if (prefix && *prefix) {
        char cmd[180];
        snprintf(cmd, sizeof(cmd), "%s %s", prefix, v.c_str());
        sendLine(cmd);
    } else sendLine(v.c_str());
}

void rawMonitor() {
    const uint8_t MAX_LINES = 10;
    const uint8_t LINE_LEN = 40;
    char lines[MAX_LINES][LINE_LEN];
    uint8_t head = 0;
    for (int i = 0; i < MAX_LINES; ++i) lines[i][0] = 0;
    gRxpos = 0;
    drawMainBorderWithTitle("C5 RAW MONITOR");
    tft.drawString("ESC to exit", 10, tftHeight - 20);
    EscPress = false;
    uint32_t lastDraw = 0;
    while (!check(EscPress) && !returnToMenu) {
        while (c5uart.available()) {
            char c = (char)c5uart.read();
            if (c == '\r') continue;
            if (c == '\n') {
                gRxline[gRxpos] = 0;
                if (gRxpos > 0) {
                    strncpy(lines[head], gRxline, LINE_LEN - 1);
                    lines[head][LINE_LEN - 1] = 0;
                    head = (head + 1) % MAX_LINES;
                }
                gRxpos = 0;
            } else if (isprint((uint8_t)c) || c == '\t') {
                if (gRxpos + 1 < sizeof(gRxline)) gRxline[gRxpos++] = c;
                else gRxpos = 0;
            }
        }
        if (millis() - lastDraw > 120) {
            lastDraw = millis();
            tft.fillRect(8, 36, tftWidth - 16, tftHeight - 60, kvxConfig.bgColor);
            int y = 36;
            for (int i = 0; i < MAX_LINES; ++i) {
                uint8_t idx = (head + i) % MAX_LINES;
                if (lines[idx][0]) {
                    tft.drawString(lines[idx], 8, y);
                    y += 10;
                }
            }
        }
        delay(5);
    }
}

void viewApList() {
    if (!gApCount) {
        displayInfo("No APs", true);
        return;
    }
    std::vector<Option> opts;
    for (int i = 0; i < gApCount; i++) {
        String label = String(gAp[i].idx) + " ch" + String(gAp[i].ch) + " " +
                       (gAp[i].ssid[0] ? gAp[i].ssid : "(hidden)");
        uint16_t idx = gAp[i].idx;
        opts.push_back({label.c_str(), [idx]() {
                            char cmd[32];
                            snprintf(cmd, sizeof(cmd), "TARGET INDEX %u", (unsigned)idx);
                            sendLine(cmd);
                            displayInfo("TARGET INDEX " + String(idx), true);
                        }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "C5 AP list");
}

bool runListCommand(const char *cmdLine) {
    gApCount = 0;
    gApCursor = 0;
    memset(gApSel, 0, sizeof(gApSel));
    gCapList = true;
    gSeenHeader = false;
    gLastUseful = 0;
    gLastAny = 0;
    uartFlush();
    drawMainBorderWithTitle(cmdLine);
    sendLine(cmdLine);
    uint32_t t0 = millis();
    EscPress = false;
    while (millis() - t0 < C5_GLOBAL_TIMEOUT_MS) {
        pollUart();
        if (check(EscPress) || returnToMenu) break;
        tft.fillRect(10, 40, tftWidth - 20, 24, kvxConfig.bgColor);
        tft.drawString("AP:" + String(gApCount), 10, 40);
        tft.drawString(gSeenHeader ? "Parsing OK" : "Parsing...", 10, 52);
        if (gApCount > 0 && gLastUseful > 0 && millis() - gLastUseful > C5_IDLE_DONE_MS) break;
        delay(5);
    }
    gCapList = false;
    displayInfo("APs: " + String(gApCount), true);
    return gApCount > 0;
}

void listCompanions() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    File root = fs->open(kvx::paths::COMPANIONS);
    if (!root || !root.isDirectory()) {
        displayError(String("Put .bin in\n") + kvx::paths::COMPANIONS, true);
        return;
    }
    std::vector<Option> opts;
    File f;
    while ((f = root.openNextFile())) {
        String n = f.name();
        f.close();
        if (n.endsWith(".bin")) opts.push_back({n.c_str(), [n]() { displayInfo(n + "\nFlash from PC\npio companion-*", true); }});
    }
    root.close();
    if (opts.empty()) {
        displayError("No companion .bin", true);
        return;
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Companion bins");
}

} // namespace

void c5SerialMenu() {
    uartBegin();
    drawMainBorderWithTitle("ESP32C5 Serial");
    tft.drawString("UART RX" + String(kvxConfigPins.gps_bus.rx) + " TX" + String(kvxConfigPins.gps_bus.tx), 10, 40);
    bool present = checkPresent();
    if (!present) displayInfo("No UART reply\nCheck C5 wiring", true);

    while (true) {
        String deauth = gDeauth ? "Deauth ON" : "Deauth OFF";
        String ward = gWard ? "Ward ON" : "Ward OFF";
        String sniff = gSniff ? "Sniff ON" : "Sniff OFF";
        std::vector<Option> opts = {
            {"Start + monitor", []() { sendLine("START"); rawMonitor(); }},
            {"Stop", []() { sendLine("STOP"); displayInfo("Stopped", true); }},
            {"Scan ALL", []() { if (runListCommand("SCAN ALL")) viewApList(); }},
            {"Scan (single)", []() { if (runListCommand("SCAN")) viewApList(); }},
            {"List", []() { if (runListCommand("LIST")) viewApList(); }},
            {"Band ALL", []() { sendLine("BAND ALL"); strncpy(gBand, "ALL", sizeof(gBand)); }},
            {"Band 2G", []() { sendLine("BAND 2G"); strncpy(gBand, "2G", sizeof(gBand)); }},
            {"Band 5G", []() { sendLine("BAND 5G"); strncpy(gBand, "5G", sizeof(gBand)); }},
            {deauth.c_str(),
             []() {
                 gDeauth = !gDeauth;
                 sendLine(gDeauth ? "DEAUTH ON" : "DEAUTH OFF");
             }},
            {ward.c_str(),
             []() {
                 gWard = !gWard;
                 sendLine(gWard ? "WARD ON" : "WARD OFF");
                 if (gWard) {
                     gDeauth = false;
                     gSniff = false;
                 }
             }},
            {"Ward history ms", []() { promptSend("WARD HISTORY ms", "WARD HISTORY"); }},
            {sniff.c_str(),
             []() {
                 gSniff = !gSniff;
                 sendLine(gSniff ? "SNIFF ON" : "SNIFF OFF");
             }},
            {"Sniff timeout", []() { promptSend("SNIFF_TIMEOUT ms", "SNIFF_TIMEOUT"); }},
            {"Target SSID", []() { promptSend("TARGET SSID", "TARGET SSID"); }},
            {"Target BSSID", []() { promptSend("TARGET BSSID", "TARGET BSSID"); }},
            {"Target INDEX", []() { promptSend("TARGET INDEX", "TARGET INDEX"); }},
            {"Target clear", []() { sendLine("TARGET CLEAR"); }},
            {"Chan SET", []() { promptSend("CHAN SET", "CHAN SET"); }},
            {"Chan ADD", []() { promptSend("CHAN ADD", "CHAN ADD"); }},
            {"Chan RESET", []() { sendLine("CHAN RESET"); }},
            {"Chan CLEAR", []() { sendLine("CHAN CLEAR"); }},
            {"Chan LIST", []() { sendLine("CHAN LIST"); rawMonitor(); }},
            {"Interval ms", []() { promptSend("INTERVAL ms", "INTERVAL"); }},
            {"History ms", []() { promptSend("HISTORY ms", "HISTORY"); }},
            {"Info", []() { sendLine("INFO"); rawMonitor(); }},
            {"Config", []() { sendLine("CONFIG"); rawMonitor(); }},
            {"Help", []() { sendLine("HELP"); rawMonitor(); }},
            {"Custom CMD", []() { promptSend("Custom CMD", ""); }},
            {"Raw monitor", rawMonitor},
            {"Companion bins", listCompanions},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "ESP32C5 Serial");
        if (sel < 0 || sel == (int)opts.size() - 1) break;
        pollUart();
    }
    uartEnd();
}
#endif

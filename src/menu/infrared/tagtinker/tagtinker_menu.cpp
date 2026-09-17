/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "tagtinker.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <globals.h>

namespace {

void ledSend(const uint8_t plid[4], uint8_t mode, uint16_t duration) {
    uint8_t ping[TT_MAX_FRAME];
    size_t pingLen = ttMakePing(ping, plid);
    uint8_t ledPayload[6] = {
        0x06, mode, 0x00, 0x00, (uint8_t)((duration >> 8) & 0xFF), (uint8_t)(duration & 0xFF)
    };
    uint8_t led[TT_MAX_FRAME];
    size_t ledLen = ttMakeAddressed(led, plid, ledPayload, 6);
    EscPress = false;
    ttTransmitPp4(ping, pingLen, 160, 2);
    ttTransmitPp4(led, ledLen, 80, 2);
}

bool askPlid(uint8_t plid[4]) {
    String bc = keyboard("", TT_BC_LEN, "Barcode 17 chars");
    if (bc.length() == TT_BC_LEN && ttBarcodeToPlid(bc.c_str(), plid)) return true;
    memset(plid, 0, 4);
    displayInfo("Using broadcast PLID", true);
    return true;
}

void showEslDir() {
    displayInfo(String("ESL assets:\n") + kvx::paths::IR_ESL, true);
}

} // namespace

void tagTinkerMenu() {
    ttIrInit(kvxConfigPins.irTx);
    while (true) {
        std::vector<Option> opts = {
            {"LED Test (tag)",
             []() {
                 uint8_t plid[4];
                 askPlid(plid);
                 ledSend(plid, 0xC9, 5);
                 displayInfo("LED ON sent", true);
             }},
            {"LED Test (broadcast)",
             []() {
                 uint8_t z[4] = {0, 0, 0, 0};
                 ledSend(z, 0xC9, 5);
                 displayInfo("BC LED sent", true);
             }},
            {"LED OFF",
             []() {
                 uint8_t z[4] = {0, 0, 0, 0};
                 ledSend(z, 0x49, 1);
                 displayInfo("LED OFF", true);
             }},
            {"Broadcast page flip",
             []() {
                 uint8_t buf[TT_MAX_FRAME];
                 size_t n = ttMakeBroadcastPage(buf, 0, false, 10);
                 EscPress = false;
                 ttTransmitPp4(buf, n, 100, 2);
                 displayInfo("Page-flip sent", true);
             }},
            {"Broadcast debug",
             []() {
                 uint8_t buf[TT_MAX_FRAME];
                 size_t n = ttMakeBroadcastDebug(buf);
                 EscPress = false;
                 ttTransmitPp4(buf, n, 200, 2);
                 displayInfo("Debug frame sent", true);
             }},
            {"Ping tag",
             []() {
                 uint8_t plid[4];
                 askPlid(plid);
                 uint8_t buf[TT_MAX_FRAME];
                 size_t n = ttMakePing(buf, plid);
                 EscPress = false;
                 ttTransmitPp4(buf, n, 160, 2);
                 displayInfo("Ping sent", true);
             }},
            {"Refresh tag",
             []() {
                 uint8_t plid[4];
                 askPlid(plid);
                 uint8_t buf[TT_MAX_FRAME];
                 size_t n = ttMakeRefresh(buf, plid);
                 EscPress = false;
                 ttTransmitPp4(buf, n, 20, 2);
                 displayInfo("Refresh sent", true);
             }},
            {"Raw hex frame",
             []() {
                 String hex = hex_keyboard("", 32, "Frame hex");
                 if (!hex.length() || hex == "\x1B") return;
                 uint8_t buf[TT_MAX_FRAME];
                 size_t n = 0;
                 for (int i = 0; i + 1 < (int)hex.length() && n < TT_MAX_FRAME; i += 2) {
                     char tmp[3] = {hex[i], hex[i + 1], 0};
                     buf[n++] = (uint8_t)strtoul(tmp, nullptr, 16);
                 }
                 if (n) {
                     EscPress = false;
                     ttTransmitPp4(buf, n, 40, 2);
                     displayInfo("Raw sent " + String((int)n) + " B", true);
                 }
             }},
            {"ESL assets (SD)", showEslDir},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "TagTinker ESL");
        if (sel < 0 || sel == (int)opts.size() - 1) break;
    }
    ttIrDeinit();
}
#endif

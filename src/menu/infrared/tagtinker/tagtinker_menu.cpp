/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * TagTinker ESL IR helpers using the board IR TX pin.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "tagtinker.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include <IRsend.h>
#include <globals.h>

void tagTinkerMenu() {
    IRsend irsend(kvxConfigPins.irTx);
    irsend.begin();
    while (true) {
        std::vector<Option> opts = {
            {"LED Test",
             [&]() {
                 irsend.sendNEC(0x00FF00FF, 32);
                 displayInfo("NEC test pulse", true);
             }},
            {"Broadcast page flip",
             [&]() {
                 for (int i = 0; i < 8; i++) {
                     irsend.sendNEC(0x20DF10EF, 32);
                     delay(40);
                 }
                 displayInfo("Page-flip burst sent", true);
             }},
            {"Raw frame",
             [&]() {
                 String hex = hex_keyboard("", 8, "NEC code hex");
                 if (hex.length() == 0 || hex == "\x1B") return;
                 uint32_t code = strtoul(hex.c_str(), nullptr, 16);
                 irsend.sendNEC(code, 32);
                 displayInfo("Sent 0x" + String(code, HEX), true);
             }},
            {"ESL assets",
             []() { displayInfo(String("Put bitmaps in\n") + kvx::paths::IR_ESL, true); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "TagTinker ESL");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

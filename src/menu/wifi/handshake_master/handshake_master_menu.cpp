/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Reference: resources/Evil-Cardputer-v1-5-4.ino (not compiled).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "handshake_master.h"
#if defined(EVIL_EXTENSIONS)
#include "root/ui/display.h"
#include <globals.h>

void handshakeMasterMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Handshake Master", handshakeMasterRun},
            {"Check Handshakes", checkHandshakes},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Handshakes");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

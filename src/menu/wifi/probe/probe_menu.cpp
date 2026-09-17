/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Reference: resources/Evil-Cardputer-v1-5-4.ino (not compiled).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "probe.h"
#if defined(EVIL_EXTENSIONS)
#include "root/ui/display.h"
#include <globals.h>

void probeMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Probe Attack", probeAttack},
            {"Probe Sniffing", probeSniff},
            {"Karma Spear", karmaSpear},
            {"Select Probe", selectProbe},
            {"Delete Probe", deleteProbe},
            {"Delete All Probes", deleteAllProbes},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Probes");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

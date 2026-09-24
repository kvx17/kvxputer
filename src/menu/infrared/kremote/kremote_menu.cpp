#include "kremote.h"
#include "kremote_learn.h"
#include "kremote_use.h"
#include "kremote_ui.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <globals.h>

static void kremoteSettingsMenu() {
    while (true) {
        std::vector<Option> opts = {
            {String("Orientation: ") + (kvxConfig.kremotePortrait ? "Portrait" : "Landscape"),
             []() { kvxConfig.setKremotePortrait(!kvxConfig.kremotePortrait); }},
            {String("Buttons: ") + (kvxConfig.kremoteButtonsSwapped ? "Swapped" : "Normal"),
             []() { kvxConfig.setKremoteButtonsSwapped(!kvxConfig.kremoteButtonsSwapped); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Universal Remote");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

static void kremoteAbout() {
    drawMainBorderWithTitle("About");
    tft.setTextSize(FP);
    tft.setCursor(10, 32);
    padprintln("kvxputer universal remote");
    padprintln("Learn + replay IR TV");
    padprintln("profiles (Flipper .ir)");
    padprintln("");
    padprintln("AGPL-3.0-or-later");
    padprintln("See NOTICE / LICENSE");
    padprintln("");
    padprintln("Press any key...");
    while (!check(AnyKeyPress) && !check(EscPress) && !check(SelPress) && !forceHome) delay(20);
    delay(150);
    while (!forceHome && (check(AnyKeyPress) || check(EscPress) || check(SelPress))) delay(10);
}

// Cardputer ADV has TX only; Learn needs a Grove / M5 IR receiver module.
// OK continues; Esc cancels (displayWarning AnyKeyPress would treat Esc as OK).
static bool kremoteLearnRxWarn() {
    drawMainBorderWithTitle("Learn Remote");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.setCursor(8, KVX_TOPBAR_H + 6);
    padprintln("Cardputer ADV has no");
    padprintln("onboard IR receiver.");
    padprintln("");
    padprintln("Learn needs an IR RX");
    padprintln("module (Grove / M5 IR).");
    padprintln("");
    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString("[OK] Continue  [X] Back", tftWidth / 2, tftHeight - uiLineH(uiDenseFont()) - 4, 1);

    check(SelPress);
    check(EscPress);
    check(AnyKeyPress);
    delay(150);
    while (!forceHome) {
        if (check(SelPress)) return true;
        if (check(EscPress)) return false;
        delay(20);
    }
    return false;
}

static void kremoteLearnEntry() {
    if (!kremoteLearnRxWarn()) return;
    kremoteLearnFlow();
}

void kremoteMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Learn Remote", []() { kremoteLearnEntry(); }},
            {"Use Remote", []() { kremoteUseFlow(); }},
            {"Browse IR", []() { kremoteBrowseIr(); }},
            {"Delete Remote", []() { kremoteDeleteFlow(); }},
            {"Button Map", []() { kremoteShowButtonMap(); }},
            {"Settings", []() { kremoteSettingsMenu(); }},
            {"About", []() { kremoteAbout(); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "kvxputer universal remote");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

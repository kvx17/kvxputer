#include "kremote.h"
#include "kremote_learn.h"
#include "kremote_use.h"
#include "kremote_ui.h"
#include "root/config/config.h"
#include "root/ui/display.h"
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

void kremoteMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Learn Remote", []() { kremoteLearnFlow(); }},
            {"Use Remote", []() { kremoteUseFlow(); }},
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

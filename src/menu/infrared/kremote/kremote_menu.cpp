#include "kremote.h"
#include "kremote_config.h"
#include "kremote_learn.h"
#include "kremote_use.h"
#include "kremote_ui.h"
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <globals.h>
#include <SD.h>

static String kremoteBrowseFolderMenuLabel() {
    String p = kvxConfig.kremoteBrowseFolder;
    if (p.length() == 0) return String("IR folder: default");
    const int maxLen = 16;
    if ((int)p.length() > maxLen) p = String("...") + p.substring((int)p.length() - (maxLen - 3));
    return String("IR folder: ") + p;
}

static void kremotePickBrowseFolder() {
    if (!setupSdCard()) {
        displayError("SD required", true);
        return;
    }
    String start = kvxConfig.kremoteBrowseFolder;
    if (start.length() == 0 || !SD.exists(start)) start = kvx::paths::IR_REMOTES;
    if (!SD.exists(start)) start = "/";
    String picked = loopSD(SD, false, "*", start, true);
    if (picked.length() == 0) return;
    kvxConfig.setKremoteBrowseFolder(picked);
    displaySuccess("IR folder set", true);
}

static void kremoteBrowseFolderMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Pick SD folder", []() { kremotePickBrowseFolder(); }},
            {"Reset default", []() { kvxConfig.setKremoteBrowseFolder(""); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Browse IR folder");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}

static void kremoteIrHwMenu() {
    std::vector<Option> opts = {
        {"Cardputer IR", []() { kvxConfig.setKremoteIrHw(KREMOTE_IR_ONBOARD); }},
        {"Unit IR", []() { kvxConfig.setKremoteIrHw(KREMOTE_IR_UNIT); }},
        {"Both (stronger)", []() { kvxConfig.setKremoteIrHw(KREMOTE_IR_BOTH); }},
        {"Back", []() {}},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "IR hardware");
}

static void kremoteSettingsMenu() {
    while (true) {
        std::vector<Option> opts = {
            {String("Orientation: ") + (kvxConfig.kremotePortrait ? "Portrait" : "Landscape"),
             []() { kvxConfig.setKremotePortrait(!kvxConfig.kremotePortrait); }},
            {String("Buttons: ") + (kvxConfig.kremoteButtonsSwapped ? "Swapped" : "Normal"),
             []() { kvxConfig.setKremoteButtonsSwapped(!kvxConfig.kremoteButtonsSwapped); }},
            // Orientation / Buttons are stored but unused by Virtual Remote (fixed key map).
            {String("IR: ") + kremoteIrHwLabel(), []() { kremoteIrHwMenu(); }},
            {kremoteBrowseFolderMenuLabel(), []() { kremoteBrowseFolderMenu(); }},
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
// Opaque floating modal (blue/white) over the menu — not a full-screen redraw.
// OK continues; Esc cancels (displayInfo would treat Esc as OK).
static bool kremoteLearnRxWarn() {
    const bool useUnit = kvxConfig.kremoteIrHw != KREMOTE_IR_ONBOARD;
    const char *onboard[] = {
        "Cardputer ADV has no",
        "onboard IR receiver.",
        "",
        "Learn needs an IR RX",
        "module (Grove / M5 IR).",
        "",
        "[OK] Continue  [Esc] Back",
    };
    const char *unit[] = {
        "Plug Unit IR on Grove.",
        "Yellow = TX, White = RX.",
        "",
        "Learn uses Unit IR RX.",
        "",
        "[OK] Continue  [Esc] Back",
    };
    const char **lines = useUnit ? unit : onboard;
    const int nLines = useUnit ? (int)(sizeof(unit) / sizeof(unit[0]))
                               : (int)(sizeof(onboard) / sizeof(onboard[0]));
    const int size = uiDenseFont();
    const int lineH = uiLineH(size) + 1;
    int boxH = 6 + nLines * lineH;
    if (boxH > tftHeight - 8) boxH = tftHeight - 8;
    const int boxY = tftHeight / 2 - boxH / 2;

    tft.drawPixel(0, 0, 0);
    tft.fillRoundRect(10, boxY, tftWidth - 20, boxH, 7, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextSize(size);
    int startY = boxY + (boxH - nLines * lineH) / 2;
    if (startY < boxY + 2) startY = boxY + 2;
    for (int i = 0; i < nLines; i++) {
        tft.drawCentreString(lines[i], tftWidth / 2, startY + i * lineH);
    }

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
            {"Favorites", []() { kremoteFavoritesFlow(); }},
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

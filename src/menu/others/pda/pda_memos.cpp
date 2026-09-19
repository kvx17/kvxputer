#include "pda_memos.h"
#include "pda_common.h"
#include "pda_editor.h"

#include "root/storage/paths.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>

static String memosPath() { return String(kvx::paths::PDA_MEMOS) + "/memos.txt"; }

static void pdaMemoView(const String &text) {
    // Full-screen scrollable view. Use FM body text with LW-based wrap (fixed in
    // ScrollableTextArea::setup/draw) so lines are not collapsed to ~3 glyphs.
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("Memo");
    }
    const int bodyY = KVX_TOPBAR_H + 4;
    const int bodyH = tftHeight - bodyY - FP * LH - 6;
    ScrollableTextArea area(FM, BORDER_PAD_X, bodyY, tftWidth - 2 * BORDER_PAD_X, bodyH, false, false);
    area.fromString(text.length() ? text : String("(empty)"));
    area.draw(true);
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawCentreString("Arrows scroll  OK/ESC back", tftWidth / 2, tftHeight - FP * LH - 2, 1);
    area.show(false);
}

static void pdaMemoActions(FS *fs, std::vector<String> &lines, size_t index) {
    bool changed = false;
    bool done = false;

    while (true) {
        std::vector<Option> opts = {
            {"View",
             [&]() { pdaMemoView(lines[index]); }},
            {"Edit",
             [&]() {
                 String edited = lines[index];
                 if (pdaTextEditor(edited, "Edit memo:", 120, false) == PDA_EDIT_OK &&
                     edited.length() > 0) {
                     lines[index] = edited;
                     changed = true;
                 }
             }},
            {"Delete",
             [&]() {
                 lines.erase(lines.begin() + index);
                 changed = true;
                 done = true;
             }},
        };
        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Memo");
        if (r < 0 || done || returnToMenu || forceHome) break;
    }

    if (changed) pdaWriteLines(fs, memosPath(), lines);
}

void pdaMemos() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_MEMOS);

    while (true) {
        std::vector<String> lines = pdaReadLines(fs, memosPath());

        std::vector<Option> opts;
        opts.push_back({"New Memo", [&]() {
                            String memo = "";
                            if (pdaTextEditor(memo, "New memo:", 120, false) == PDA_EDIT_OK &&
                                memo.length() > 0) {
                                lines.push_back(memo);
                                pdaWriteLines(fs, memosPath(), lines);
                            }
                        }});
        for (size_t i = 0; i < lines.size(); i++) {
            opts.push_back({lines[i], [&, i]() { pdaMemoActions(fs, lines, i); }});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Memos");
        if (r < 0 || returnToMenu || forceHome) break;
    }
}

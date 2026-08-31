#include "scripts_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "root/scripting/bjs_interpreter/interpreter.h" // for JavaScript interpreter
#include <algorithm>                             // for std::sort

void ScriptsMenu::optionsMenu() {
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    if (interpreter_state >= 0) {
        interpreter_state = 1;
        returnToMenu = true;
        return;
    }

    options = getScriptsOptionsList("", false);

    options.push_back({"Load...", run_bjs_script});
    addOptionToMainMenu();

    loopOptions(options, MENU_TYPE_SUBMENU, "Scripts");
#endif
}

void ScriptsMenu::drawIcon(float scale) {
    clearIconArea();

    int iconW = scale * 40;
    int iconH = scale * 60;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    int foldSize = iconH / 4;
    int arrowSize = iconW / 10;
    int arrowPadX = 2 * arrowSize;
    int arrowPadBottom = 3 * arrowPadX;
    int slashSize = 2 * arrowSize;

    // File
    tft.drawRect(iconCenterX - iconW / 2, iconCenterY - iconH / 2, iconW, iconH, kvxConfig.priColor);
    tft.fillRect(
        iconCenterX + iconW / 2 - foldSize, iconCenterY - iconH / 2, foldSize, foldSize, kvxConfig.bgColor
    );
    tft.drawTriangle(
        (iconCenterX + iconW / 2 - foldSize),
        (iconCenterY - iconH / 2),
        (iconCenterX + iconW / 2 - foldSize),
        (iconCenterY - iconH / 2 + foldSize - 1),
        (iconCenterX + iconW / 2 - 1),
        (iconCenterY - iconH / 2 + foldSize - 1),
        kvxConfig.priColor
    );

    // Left Arrow
    tft.drawLine(
        iconCenterX - iconW / 2 + arrowPadX,
        iconCenterY + iconH / 2 - arrowPadBottom,
        iconCenterX - iconW / 2 + arrowPadX + arrowSize,
        iconCenterY + iconH / 2 - arrowPadBottom + arrowSize,
        kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX - iconW / 2 + arrowPadX,
        iconCenterY + iconH / 2 - arrowPadBottom,
        iconCenterX - iconW / 2 + arrowPadX + arrowSize,
        iconCenterY + iconH / 2 - arrowPadBottom - arrowSize,
        kvxConfig.priColor
    );

    // Slash
    tft.drawLine(
        iconCenterX - slashSize / 2,
        iconCenterY + iconH / 2 - arrowPadBottom + arrowSize,
        iconCenterX + slashSize / 2,
        iconCenterY + iconH / 2 - arrowPadBottom - arrowSize,
        kvxConfig.priColor
    );

    // Right Arrow
    tft.drawLine(
        iconCenterX + iconW / 2 - arrowPadX,
        iconCenterY + iconH / 2 - arrowPadBottom,
        iconCenterX + iconW / 2 - arrowPadX - arrowSize,
        iconCenterY + iconH / 2 - arrowPadBottom + arrowSize,
        kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX + iconW / 2 - arrowPadX,
        iconCenterY + iconH / 2 - arrowPadBottom,
        iconCenterX + iconW / 2 - arrowPadX - arrowSize,
        iconCenterY + iconH / 2 - arrowPadBottom - arrowSize,
        kvxConfig.priColor
    );
}

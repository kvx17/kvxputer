#include "netops_menu.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include <globals.h>

#if defined(EVIL_EXTENSIONS)
#include "menu/netops/netops_extras.h"
#endif

void NetOpsMenu::optionsMenu() {
    options.clear();

#if defined(EVIL_EXTENSIONS)
    evilNetOpsBuildMenu(options);
#else
    options.push_back({"Extensions disabled", []() { displayInfo("Build with -DEVIL_EXTENSIONS=1", true); }});
#endif

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "NetOps");
}

void NetOpsMenu::drawIcon(float scale) {
    clearIconArea();
    int w = scale * 40;
    int h = scale * 28;
    int t = scale * 3;
    if (w % 2) w++;
    if (h % 2) h++;

    // Simple "network nodes" icon
    tft.fillCircle(iconCenterX - w / 2, iconCenterY, t + 1, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + w / 2, iconCenterY - h / 2, t + 1, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + w / 2, iconCenterY + h / 2, t + 1, kvxConfig.priColor);
    tft.drawLine(
        iconCenterX - w / 2, iconCenterY, iconCenterX + w / 2, iconCenterY - h / 2, kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX - w / 2, iconCenterY, iconCenterX + w / 2, iconCenterY + h / 2, kvxConfig.priColor
    );
}

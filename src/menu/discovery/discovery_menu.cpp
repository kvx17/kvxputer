#include "discovery_menu.h"
#include "root/app/app_catalog.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include "root/ui/theme.h"
#include <globals.h>

void DiscoveryMenu::optionsMenu() {
    options.clear();
    for (const auto &item : appCatalogItems()) {
        if (!item.discovery || !appCatalogAvailable(item)) continue;
        String id = item.id;
        options.push_back({item.label, [id]() { appCatalogLaunch(id); }});
    }
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Discovery");
}

void DiscoveryMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 7;
    if (radius < 2) radius = 2;

    tft.fillCircle(iconCenterX, iconCenterY, radius, kvxConfig.priColor);
    tft.drawArc(
        iconCenterX, iconCenterY, 2.4f * radius, 1.9f * radius, 40, 320, DEFAULT_SECCOLOR, kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX, iconCenterY, 3.6f * radius, 3.1f * radius, 20, 340, kvxConfig.priColor, kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX, iconCenterY, 4.8f * radius, 4.3f * radius, 50, 310, DEFAULT_SECCOLOR, kvxConfig.bgColor
    );
}

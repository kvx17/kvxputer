#include "connect_menu.h"
#include "root/serial/connect/file_sharing.h"
#include "root/serial/connect/serial_commands.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "root/net/wifi_common.h"

void ConnectMenu::optionsMenu() {
    options = {
#ifndef LITE_VERSION
        {"Send File", [=]() { FileSharing().sendFile(); }        },
        {"Recv File", [=]() { FileSharing().receiveFile(); }     },

        {"Send Cmds", [=]() { EspSerialCmd().sendCommands(); }   },
        {"Recv Cmds", [=]() { EspSerialCmd().receiveCommands(); }},
#endif
    };
    addOptionToMainMenu();

    loopOptions(options, MENU_TYPE_SUBMENU, getName().c_str());
}

void ConnectMenu::drawIcon(float scale) {
    clearIconArea();

    int iconW = scale * 50;
    int iconH = scale * 40;
    int radius = scale * 7;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    tft.fillCircle(iconCenterX - iconW / 2, iconCenterY, radius, kvxConfig.priColor);

    tft.fillCircle(iconCenterX + 0.3 * iconW, iconCenterY - iconH / 2, radius, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + 0.5 * iconW, iconCenterY, radius, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + 0.3 * iconW, iconCenterY + iconH / 2, radius, kvxConfig.priColor);

    tft.drawLine(
        iconCenterX - iconW / 2,
        iconCenterY,
        iconCenterX + 0.3 * iconW,
        iconCenterY - iconH / 2,
        kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX - iconW / 2, iconCenterY, iconCenterX + 0.5 * iconW, iconCenterY, kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX - iconW / 2,
        iconCenterY,
        iconCenterX + 0.3 * iconW,
        iconCenterY + iconH / 2,
        kvxConfig.priColor
    );
}

#include "files_menu.h"
#include "root/ui/display.h"
#include "root/storage/massStorage.h"
#include "root/storage/sd_functions.h"
#include "root/app/utils.h"
#include "root/net/webInterface.h"
#include "root/serial/connect/file_sharing.h"
#include "root/serial/connect/serial_commands.h"

void FileMenu::optionsMenu() {
    options.clear();
    if (setupSdCard()) options.push_back({"SD Card", [=]() { loopSD(SD); }});
    options.push_back({"LittleFS", [=]() { loopSD(LittleFS); }});
    options.push_back({"WebUI", loopOptionsWebUi});

#if !defined(LITE_VERSION)
    options.push_back({"Connect", [=]() {
        std::vector<Option> connectOpts = {
            {"Send File", [=]() { FileSharing().sendFile(); }        },
            {"Recv File", [=]() { FileSharing().receiveFile(); }     },
            {"Send Cmds", [=]() { EspSerialCmd().sendCommands(); }   },
            {"Recv Cmds", [=]() { EspSerialCmd().receiveCommands(); }},
            {"Back",      [=]() { optionsMenu(); }                   }
        };
        loopOptions(connectOpts, MENU_TYPE_SUBMENU, "Connect");
    }});
#endif

#if defined(SOC_USB_OTG_SUPPORTED)
    options.push_back({"Mass Storage", [=]() { MassStorage(); }});
#endif
    addOptionToMainMenu();

    loopOptions(options, MENU_TYPE_SUBMENU, "Files");
}
void FileMenu::drawIcon(float scale) {
    clearIconArea();
    int iconW = scale * 32;
    int iconH = scale * 48;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    int foldSize = iconH / 4;
    int iconX = iconCenterX - iconW / 2;
    int iconY = iconCenterY - iconH / 2;
    int iconDelta = 10;

    // Files
    tft.drawRect(iconX + iconDelta, iconY - iconDelta, iconW, iconH, kvxConfig.priColor);

    tft.fillRect(iconX, iconY, iconW, iconH, kvxConfig.bgColor);
    tft.drawRect(iconX, iconY, iconW, iconH, kvxConfig.priColor);

    tft.fillRect(iconX - iconDelta, iconY + iconDelta, iconW, iconH, kvxConfig.bgColor);
    tft.drawRect(iconX - iconDelta, iconY + iconDelta, iconW, iconH, kvxConfig.priColor);

    // Erase corners
    tft.fillRect(
        iconX + iconDelta + iconW - foldSize, iconY - iconDelta - 1, foldSize, 2, kvxConfig.bgColor
    );
    tft.fillRect(iconX + iconDelta + iconW - 1, iconY - iconDelta, 2, foldSize, kvxConfig.bgColor);

    tft.fillRect(iconX + iconW - foldSize, iconY - 1, foldSize, 2, kvxConfig.bgColor);
    tft.fillRect(iconX + iconW - 1, iconY, 2, foldSize, kvxConfig.bgColor);

    tft.fillRect(
        iconX - iconDelta + iconW - foldSize, iconY + iconDelta - 1, foldSize, 2, kvxConfig.bgColor
    );
    tft.fillRect(iconX - iconDelta + iconW - 1, iconY + iconDelta, 2, foldSize, kvxConfig.bgColor);

    // Folds
    tft.drawTriangle(
        iconX + iconDelta + iconW - foldSize,
        iconY - iconDelta,
        iconX + iconDelta + iconW - foldSize,
        iconY - iconDelta + foldSize - 1,
        iconX + iconDelta + iconW - 1,
        iconY - iconDelta + foldSize - 1,
        kvxConfig.priColor
    );
    tft.drawTriangle(
        iconX + iconW - foldSize,
        iconY,
        iconX + iconW - foldSize,
        iconY + foldSize - 1,
        iconX + iconW - 1,
        iconY + foldSize - 1,
        kvxConfig.priColor
    );
    tft.drawTriangle(
        iconX - iconDelta + iconW - foldSize,
        iconY + iconDelta,
        iconX - iconDelta + iconW - foldSize,
        iconY + iconDelta + foldSize - 1,
        iconX - iconDelta + iconW - 1,
        iconY + iconDelta + foldSize - 1,
        kvxConfig.priColor
    );
}

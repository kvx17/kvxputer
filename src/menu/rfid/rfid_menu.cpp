#include "rfid_menu.h"
#include "menu/rfid/rfid125.h"
#include "menu/rfid/rfid_shared.h"
#include "menu/rfid/tag_o_matic.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include <globals.h>

void RFIDMenu::optionsMenu() {
    options = {
#if !defined(REMOVE_RFID_HW_INTERFACE)
        {"Read tag", [=]() { TagOMatic(TagOMatic::DOMAIN_RFID); }},
        {"Scan tags", [=]() { TagOMatic(TagOMatic::SCAN_MODE, TagOMatic::DOMAIN_RFID); }},
        {"Load file", [=]() { TagOMatic(TagOMatic::LOAD_MODE, TagOMatic::DOMAIN_RFID); }},
        {"Erase data", [=]() { TagOMatic(TagOMatic::ERASE_MODE, TagOMatic::DOMAIN_RFID); }},
#endif
#ifndef LITE_VERSION
        {"Read 125kHz", [=]() { RFID125(); }},
#endif
        {"Config", [this]() { configMenu(); }},
    };

    addOptionToMainMenu();
    vTaskDelay(pdMS_TO_TICKS(200));
    String txt = "RFID" + rfidModuleTitleSuffix();
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void RFIDMenu::configMenu() {
    rfidConfigMenu("RFID Config", [this]() { optionsMenu(); });
}

void RFIDMenu::drawIcon(float scale) {
    clearIconArea();
    int iconSize = scale * 70;
    int iconRadius = scale * 7;
    int deltaRadius = scale * 10;

    if (iconSize % 2 != 0) iconSize++;

    tft.drawRoundRect(
        iconCenterX - iconSize / 2,
        iconCenterY - iconSize / 2,
        iconSize,
        iconSize,
        iconRadius,
        kvxConfig.priColor
    );
    tft.fillRect(iconCenterX - iconSize / 2, iconCenterY, iconSize / 2, iconSize / 2, kvxConfig.bgColor);

    tft.drawCircle(
        iconCenterX - iconSize / 2 + deltaRadius,
        iconCenterY + iconSize / 2 - deltaRadius,
        iconRadius,
        kvxConfig.priColor
    );

    tft.drawArc(
        iconCenterX - iconSize / 2 + deltaRadius,
        iconCenterY + iconSize / 2 - deltaRadius,
        2.5 * iconRadius,
        2 * iconRadius,
        180,
        270,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 2 + deltaRadius,
        iconCenterY + iconSize / 2 - deltaRadius,
        2.5 * iconRadius + deltaRadius,
        2 * iconRadius + deltaRadius,
        180,
        270,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 2 + deltaRadius,
        iconCenterY + iconSize / 2 - deltaRadius,
        2.5 * iconRadius + 2 * deltaRadius,
        2 * iconRadius + 2 * deltaRadius,
        180,
        270,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

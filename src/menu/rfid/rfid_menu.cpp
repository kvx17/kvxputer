#include "rfid_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "menu/rfid/PN532KillerTools.h"
#include "menu/rfid/amiibo.h"
#include "menu/rfid/chameleon.h"
#include "menu/rfid/pn532ble.h"
#include "menu/rfid/rfid125.h"
#include "menu/rfid/srix_tool.h" //added for srix Tool
#include "menu/rfid/tag_o_matic.h"

#ifndef LITE_VERSION
#include "menu/rfid/emv_reader.hpp"
#endif
void RFIDMenu::optionsMenu() {
    options = {
#if !defined(REMOVE_RFID_HW_INTERFACE)  // Remove Hardware interface menu due to lack of external GPIO
        {"Read tag",    [=]() { TagOMatic(); }                     },
#ifndef LITE_VERSION
        {"Read EMV",    [=]() { EMVReader(); }                     },
        {"Read 125kHz", [=]() { RFID125(); }                       },
#endif
        {"Scan tags",   [=]() { TagOMatic(TagOMatic::SCAN_MODE); } },
        {"Load file",   [=]() { TagOMatic(TagOMatic::LOAD_MODE); } },
        {"Erase data",  [=]() { TagOMatic(TagOMatic::ERASE_MODE); }},
#endif
    };

#if !defined(REMOVE_RFID_HW_INTERFACE)
    // Emulate NDEF (arm the radio directly with a built NDEF message) only
    // makes sense on modules that can act as a target/card emulator.
    bool ndefEmulationSupported = kvxConfigPins.rfidModule == PN532_I2C_MODULE ||
                                  kvxConfigPins.rfidModule == PN532_SPI_MODULE ||
                                  kvxConfigPins.rfidModule == PN532_I2C_SPI_MODULE ||
                                  kvxConfigPins.rfidModule == ST25R3916_SPI_MODULE ||
                                  kvxConfigPins.rfidModule == ST25R3916_I2C_MODULE || false;
    if (ndefEmulationSupported) {
        options.push_back({"Emulate NDEF", [=]() { TagOMatic(TagOMatic::EMULATE_NDEF_MODE); }});
    }
    options.push_back({"Write NDEF", [=]() { TagOMatic(TagOMatic::WRITE_NDEF_MODE); }});
#endif
#ifndef LITE_VERSION
    options.push_back({"Amiibolink", [=]() { Amiibo(); }});
#endif
    options.push_back({"Chameleon", [=]() { Chameleon(); }});
#ifndef LITE_VERSION
    options.push_back({"PN532 BLE", [=]() { Pn532ble(); }});
#if !defined(REMOVE_RFID_HW_INTERFACE) // Remove Hardware interface menu due to lack of external GPIO
    options.push_back({"PN532 UART", [=]() { PN532KillerTools(); }});
#endif
#endif
    options.push_back({"Config", [this]() { configMenu(); }});

#if !defined(REMOVE_RFID_HW_INTERFACE)
#ifndef LITE_VERSION
    if (kvxConfigPins.rfidModule == PN532_I2C_MODULE) {
        // Added SRIX Menu only if PN is set to i2c mode
        options.insert(options.begin() + 3, {"SRIX Tool", [=]() { PN532_SRIX(); }});
    }
#endif
#endif

    addOptionToMainMenu();

    vTaskDelay(pdMS_TO_TICKS(200));

    String txt = "RFID";
    if (kvxConfigPins.rfidModule == M5_RFID2_MODULE) txt += " (RFID2)";
#ifdef M5STICK
    else if (kvxConfigPins.rfidModule == PN532_I2C_MODULE) txt += " (PN532-G33)";
    else if (kvxConfigPins.rfidModule == PN532_I2C_SPI_MODULE) txt += " (PN532-G36)";
#else
    else if (kvxConfigPins.rfidModule == PN532_I2C_MODULE) txt += " (PN532-I2C)";
#endif
    else if (kvxConfigPins.rfidModule == PN532_SPI_MODULE) txt += " (PN532-SPI)";
    else if (kvxConfigPins.rfidModule == RC522_SPI_MODULE) txt += " (RC522-SPI)";
#if !defined(LITE_VERSION)
    else if (kvxConfigPins.rfidModule == ST25R3916_SPI_MODULE) txt += " (ST25R-SPI)";
    else if (kvxConfigPins.rfidModule == ST25R3916_I2C_MODULE) txt += " (ST25R-I2C)";
#endif
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void RFIDMenu::configMenu() {
    options = {
#if !defined(REMOVE_RFID_HW_INTERFACE)  // Remove Hardware interface menu due to lack of external GPIO
        {"RFID Module", setRFIDModuleMenu          },
#endif
        {"Add MIF Key", addMifareKeyMenu           },
        {"Back",        [this]() { optionsMenu(); }},
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "RFID Config");
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

#include "nfc_menu.h"
#include "nfc_tools.h"
#include "menu/rfid/PN532KillerTools.h"
#include "menu/rfid/amiibo.h"
#include "menu/rfid/chameleon.h"
#include "menu/rfid/pn532ble.h"
#include "menu/rfid/rfid_shared.h"
#include "menu/rfid/srix_tool.h"
#include "menu/rfid/tag_o_matic.h"
#include "root/app/utils.h"
#include "root/ui/display.h"
#include <globals.h>

#ifndef LITE_VERSION
#include "menu/rfid/emv_reader.hpp"
#endif

void NFCMenu::optionsMenu() {
    options.clear();
#if !defined(REMOVE_RFID_HW_INTERFACE)
    options.push_back({"Read tag", [=]() { TagOMatic(TagOMatic::DOMAIN_NFC); }});
#ifndef LITE_VERSION
    options.push_back({"Read EMV", [=]() { EMVReader(); }});
#endif
    options.push_back({"Scan tags", [=]() { TagOMatic(TagOMatic::SCAN_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Load file", [=]() { TagOMatic(TagOMatic::LOAD_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Erase data", [=]() { TagOMatic(TagOMatic::ERASE_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back(
        {"Write NDEF", [=]() { TagOMatic(TagOMatic::WRITE_NDEF_MODE, TagOMatic::DOMAIN_NFC); }}
    );
    if (nfcNdefEmulationSupported()) {
        options.push_back(
            {"Emulate NDEF", [=]() { TagOMatic(TagOMatic::EMULATE_NDEF_MODE, TagOMatic::DOMAIN_NFC); }}
        );
    }
    options.push_back({"Read records", nfcReadRecords});
    options.push_back({"Memory dump", nfcMemoryDump});
    options.push_back({"Chip info", nfcChipInfo});
    options.push_back({"Lock tag", nfcLockTag});
    options.push_back({"Password", nfcPasswordMenu});
    options.push_back({"Format memory", nfcFormatMemory});
    options.push_back({"NFC command", nfcSendApdu});
    options.push_back({"Save as file", nfcSaveAsFile});
#ifndef LITE_VERSION
    if (kvxConfigPins.nfcModule == PN532_I2C_MODULE) {
        options.push_back({"SRIX Tool", [=]() { PN532_SRIX(); }});
    }
#endif
#endif
#ifndef LITE_VERSION
    options.push_back({"Amiibolink", [=]() { Amiibo(); }});
#endif
    options.push_back({"Chameleon", [=]() { Chameleon(); }});
#ifndef LITE_VERSION
    options.push_back({"PN532 BLE", [=]() { Pn532ble(); }});
#if !defined(REMOVE_RFID_HW_INTERFACE)
    options.push_back({"PN532 UART", [=]() { PN532KillerTools(); }});
#endif
#endif
    options.push_back({"Config", [this]() { configMenu(); }});

    addOptionToMainMenu();
    vTaskDelay(pdMS_TO_TICKS(200));
    String txt = "NFC" + nfcModuleTitleSuffix();
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void NFCMenu::configMenu() {
    nfcConfigMenu("NFC Config", [this]() { optionsMenu(); });
}

void NFCMenu::drawIcon(float scale) {
    clearIconArea();
    int r = scale * 28;
    if (r < 8) r = 8;
    tft.drawCircle(iconCenterX, iconCenterY, r / 4, kvxConfig.priColor);
    tft.fillCircle(iconCenterX, iconCenterY, r / 8, kvxConfig.priColor);
    tft.drawArc(iconCenterX, iconCenterY, r / 2, r / 2 - 2, 200, 340, kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawArc(iconCenterX, iconCenterY, (3 * r) / 4, (3 * r) / 4 - 2, 200, 340, kvxConfig.priColor,
                kvxConfig.bgColor);
    tft.drawArc(iconCenterX, iconCenterY, r, r - 2, 200, 340, kvxConfig.priColor, kvxConfig.bgColor);
}

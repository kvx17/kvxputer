#include "nfc_menu.h"
#include "nfc_classic.h"
#include "nfc_lab.h"
#include "nfc_niche.h"
#include "nfc_protocols.h"
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

#if !defined(REMOVE_RFID_HW_INTERFACE)
static void nfcNdefToolsMenu() {
    int pick = -1;
    options = {
        {"Write NDEF",   [&]() { pick = 0; }},
        {"Emulate NDEF", [&]() { pick = 1; }},
        {"Read records", [&]() { pick = 2; }},
        {"Save as file", [&]() { pick = 3; }},
        {"Memory dump",  [&]() { pick = 4; }},
        {"Chip info",    [&]() { pick = 5; }},
        {"Lock tag",     [&]() { pick = 6; }},
        {"Password",     [&]() { pick = 7; }},
        {"Format memory",[&]() { pick = 8; }},
        {"NFC command",  [&]() { pick = 9; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "NDEF tools") < 0 || pick < 0) return;
    switch (pick) {
        case 0: TagOMatic(TagOMatic::WRITE_NDEF_MODE, TagOMatic::DOMAIN_NFC); break;
        case 1:
            if (nfcNdefEmulationSupported())
                TagOMatic(TagOMatic::EMULATE_NDEF_MODE, TagOMatic::DOMAIN_NFC);
            else
                displayError("NDEF emulate needs PN532/ST25.", true);
            break;
        case 2: nfcReadRecords(); break;
        case 3: nfcSaveAsFile(); break;
        case 4: nfcMemoryDump(); break;
        case 5: nfcChipInfo(); break;
        case 6: nfcLockTag(); break;
        case 7: nfcPasswordMenu(); break;
        case 8: nfcFormatMemory(); break;
        case 9: nfcSendApdu(); break;
        default: break;
    }
}

static void nfcCompanionsMenu() {
    int pick = -1;
    options.clear();
#ifndef LITE_VERSION
    options.emplace_back("Read EMV", [&]() { pick = 0; });
    if (kvxConfigPins.nfcModule == PN532_I2C_MODULE) {
        options.emplace_back("SRIX Tool", [&]() { pick = 1; });
    }
    options.emplace_back("Amiibolink", [&]() { pick = 2; });
    options.emplace_back("PN532 BLE", [&]() { pick = 3; });
    options.emplace_back("PN532 UART", [&]() { pick = 4; });
#endif
    options.emplace_back("Chameleon", [&]() { pick = 5; });
    if (options.empty() || loopOptions(options, MENU_TYPE_SUBMENU, "Companions") < 0 || pick < 0) return;
    switch (pick) {
#ifndef LITE_VERSION
        case 0: EMVReader(); break;
        case 1: PN532_SRIX(); break;
        case 2: Amiibo(); break;
        case 3: Pn532ble(); break;
        case 4: PN532KillerTools(); break;
#endif
        case 5: Chameleon(); break;
        default: break;
    }
}
#endif

void NFCMenu::optionsMenu() {
    options.clear();
#if !defined(REMOVE_RFID_HW_INTERFACE)
    options.push_back({"Read tag", [=]() { TagOMatic(TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Scan tags", [=]() { TagOMatic(TagOMatic::SCAN_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Load file", [=]() { TagOMatic(TagOMatic::LOAD_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Erase data", [=]() { TagOMatic(TagOMatic::ERASE_MODE, TagOMatic::DOMAIN_NFC); }});
    options.push_back({"Classic...", [=]() { nfcClassicMenu(); }});
    options.push_back({"Tags...", [=]() { nfcTagsMenu(); }});
    options.push_back({"NDEF tools...", [=]() { nfcNdefToolsMenu(); }});
    options.push_back({"Lab...", [=]() { nfcLabMenu(); }});
    options.push_back({"Parsers...", [=]() { nfcParsersMenu(); }});
    options.push_back({"Companions...", [=]() { nfcCompanionsMenu(); }});
#else
#ifndef LITE_VERSION
    options.push_back({"Amiibolink", [=]() { Amiibo(); }});
#endif
    options.push_back({"Chameleon", [=]() { Chameleon(); }});
#ifndef LITE_VERSION
    options.push_back({"PN532 BLE", [=]() { Pn532ble(); }});
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

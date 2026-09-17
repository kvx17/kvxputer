#include "rfid_menu.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "root/hal/pahub.h"
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

#if !defined(REMOVE_RFID_HW_INTERFACE)
static String gpioLabel(int pin) { return "G" + String(pin); }

static void showRfidWiringPage(const String &title, const std::vector<String> &lines) {
    ScrollableTextArea area(title);
    for (const String &line : lines) area.addLine(line);
    area.addLine("");
    area.addLine("Sel/Enter = back");
    area.show();
}

static void showRfidWiringHelp() {
    const int sda = GROVE_SDA;
    const int scl = GROVE_SCL;
    const int sck = SPI_SCK_PIN;
    const int mosi = SPI_MOSI_PIN;
    const int miso = SPI_MISO_PIN;
    const int ss = SPI_SS_PIN;

    while (true) {
        options = {
            {"Overview",
             [=]() {
                 showRfidWiringPage(
                     "RFID WIRING",
                     {
                         "Select module under",
                         "RFID > Config > Module",
                         "",
                         "Recommended: PN532 I2C",
                         "on Grove PORT.A",
                         "",
                         "SDA=" + gpioLabel(sda) + "  SCL=" + gpioLabel(scl),
                         "VCC=5V  GND=GND",
                         "",
                         "Do NOT use Adv G8/G9",
                         "(keyboard I2C).",
                         "",
                         "RFID2: read/write only",
                         "PN532: + emulate NDEF",
                         "",
                         "Read > Save > Emulate",
                         "needs PN532 selected.",
                     }
                 );
             }},
            {"M5 RFID2",
             [=]() {
                 showRfidWiringPage(
                     "M5 RFID2",
                     {
                         "Chip: WS1850S @ 0x28",
                         "Mode: I2C only",
                         "Menu: M5 RFID2",
                         "",
                         "Connect Unit to Grove",
                         "PORT.A (or PaHub ch).",
                         "",
                         "SDA=" + gpioLabel(sda) + "  SCL=" + gpioLabel(scl),
                         "5V + GND from PORT.A",
                         "",
                         "No card emulate.",
                         "Use for Classic/NTAG",
                         "read, write, clone.",
                     }
                 );
             }},
            {"PN532 I2C",
             [=]() {
                 showRfidWiringPage(
                     "PN532 I2C",
                     {
                         "Boards: Elechouse V3,",
                         "ITEAD / Katranji PN532",
                         "Menu: PN532 on I2C",
                         "",
                         "Elechouse switches:",
                         "  CH1 ON, CH2 OFF",
                         "ITEAD switches:",
                         "  SET0 H, SET1 L",
                         "",
                         "Wire to Grove PORT.A:",
                         "  SDA -> " + gpioLabel(sda),
                         "  SCL -> " + gpioLabel(scl),
                         "  VCC -> 5V",
                         "  GND -> GND",
                         "",
                         "Supports emulate.",
                         "Classic -> NDEF T4T",
                         "(not full Classic).",
                     }
                 );
             }},
            {"PN532 SPI",
             [=]() {
                 showRfidWiringPage(
                     "PN532 SPI",
                     {
                         "Menu: PN532 on SPI",
                         "Pins Setup > PN532 Pins",
                         "",
                         "Elechouse: CH1 OFF,",
                         "  CH2 ON",
                         "ITEAD: SET0 L, SET1 H",
                         "",
                         "Default Cardputer pins:",
                         "  SCK  " + gpioLabel(sck),
                         "  MOSI " + gpioLabel(mosi),
                         "  MISO " + gpioLabel(miso),
                         "  SS/CS " + gpioLabel(ss),
                         "  VCC 5V  GND GND",
                         "",
                         "SPI vs I2C exclusive",
                         "on the module.",
                         "SD shares SPI (CS12).",
                     }
                 );
             }},
            {"RC522 SPI",
             [=]() {
                 showRfidWiringPage(
                     "RC522 SPI",
                     {
                         "Chip: MFRC522",
                         "Menu: RC522 on SPI",
                         "",
                         "Same SPI bus as SD:",
                         "  SCK  " + gpioLabel(sck),
                         "  MOSI " + gpioLabel(mosi),
                         "  MISO " + gpioLabel(miso),
                         "  SS   " + gpioLabel(ss),
                         "  3.3V / GND",
                         "",
                         "No emulate (RFID2",
                         "driver path).",
                     }
                 );
             }},
#if !defined(LITE_VERSION)
            {"ST25R3916",
             []() {
                 showRfidWiringPage(
                     "ST25R3916",
                     {
                         "Menu: ST25R SPI/I2C",
                         "Set pins under",
                         "Config > Pins Setup",
                         "> ST25R3916 Pins",
                         "",
                         "Supports emulate.",
                         "Prefer dedicated CS",
                         "not shared with SD.",
                     }
                 );
             }},
#endif
            {"Back", []() {}},
        };

        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "Wiring help");
        if (selected < 0 || selected == (int)options.size() - 1) return;
    }
}
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
    int8_t muxCh = pahubEnabled() ? pahubChannelForRfidModule() : (int8_t)-1;
    if (kvxConfigPins.rfidModule == M5_RFID2_MODULE) {
        txt += (muxCh >= 0) ? (" (RFID2 ch" + String(muxCh) + ")") : " (RFID2)";
    }
#ifdef M5STICK
    else if (kvxConfigPins.rfidModule == PN532_I2C_MODULE) {
        txt += (muxCh >= 0) ? (" (NFC ch" + String(muxCh) + ")") : " (PN532-G33)";
    } else if (kvxConfigPins.rfidModule == PN532_I2C_SPI_MODULE) txt += " (PN532-G36)";
#else
    else if (kvxConfigPins.rfidModule == PN532_I2C_MODULE) {
        txt += (muxCh >= 0) ? (" (NFC ch" + String(muxCh) + ")") : " (PN532-I2C)";
    }
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
        {"Wiring help", showRfidWiringHelp         },
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

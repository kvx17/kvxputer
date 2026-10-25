#include "rfid_shared.h"
// Include order matches tag_o_matic.cpp: ST25 before PN532 (FELICA_CMD_POLLING clash).
#if !defined(LITE_VERSION)
#include "ST25R3916.h"
#endif
#include "PN532.h"
#include "RFID2.h"
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include "root/ui/settings.h"
#include <globals.h>

static bool groveI2cAck(uint8_t addr) {
    TwoWire *w = acquireI2CBus();
    if (!w) return false;
    w->beginTransmission(addr);
    return w->endTransmission() == 0;
}

void resolveNfcModuleSelection() {
#if !defined(LITE_VERSION) && !defined(REMOVE_RFID_HW_INTERFACE)
    // M5 Unit NFC (U216) = ST25R3916 @ 0x50; Elechouse/ITEAD PN532 I2C = 0x24.
    static constexpr uint8_t kPn532Addr = 0x24;
    static constexpr uint8_t kUnitNfcAddr = 0x50;

    const int mod = kvxConfigPins.nfcModule;
    const bool wantPn532I2c = (mod == PN532_I2C_MODULE
#ifdef M5STICK
                               || mod == PN532_I2C_SPI_MODULE
#endif
    );
    const bool wantUnitNfc = (mod == ST25R3916_I2C_MODULE);
    if (!wantPn532I2c && !wantUnitNfc) return;

    const bool hasPn532 = groveI2cAck(kPn532Addr);
    const bool hasUnitNfc = groveI2cAck(kUnitNfcAddr);

    if (wantPn532I2c && !hasPn532 && hasUnitNfc) {
        kvxConfigPins.setNfcModule(ST25R3916_I2C_MODULE);
    } else if (wantUnitNfc && !hasUnitNfc && hasPn532) {
        kvxConfigPins.setNfcModule(PN532_I2C_MODULE);
    }
#endif
}

RFIDInterface *createHfModule(HfDomain domain) {
    if (domain == HfDomain::Nfc) resolveNfcModuleSelection();
    int mod = (domain == HfDomain::Nfc) ? kvxConfigPins.nfcModule : kvxConfigPins.rfidModule;
    switch (mod) {
        case PN532_I2C_MODULE: return new PN532(PN532::CONNECTION_TYPE::I2C);
#ifdef M5STICK
        case PN532_I2C_SPI_MODULE: return new PN532(PN532::CONNECTION_TYPE::I2C_SPI);
#endif
        case PN532_SPI_MODULE: return new PN532(PN532::CONNECTION_TYPE::SPI);
        case RC522_SPI_MODULE: return new RFID2(false);
#if !defined(LITE_VERSION)
        case ST25R3916_SPI_MODULE: return new ST25R3916(ST25R3916::SPI_MODE);
        case ST25R3916_I2C_MODULE: return new ST25R3916(ST25R3916::I2C_MODE);
#endif
        case M5_RFID2_MODULE:
        default: return new RFID2();
    }
}

PahubChannelGuard *openHfPahubGuard(HfDomain domain) {
    int8_t ch = (domain == HfDomain::Nfc) ? pahubChannelForNfcModule() : pahubChannelForRfidModule();
    if (ch < 0) return nullptr;
    return new PahubChannelGuard(ch);
}

bool nfcNdefEmulationSupported() {
    return kvxConfigPins.nfcModule == PN532_I2C_MODULE || kvxConfigPins.nfcModule == PN532_SPI_MODULE ||
           kvxConfigPins.nfcModule == PN532_I2C_SPI_MODULE ||
           kvxConfigPins.nfcModule == ST25R3916_SPI_MODULE ||
           kvxConfigPins.nfcModule == ST25R3916_I2C_MODULE;
}

String rfidModuleTitleSuffix() {
    int8_t muxCh = pahubEnabled() ? pahubChannelForRfidModule() : (int8_t)-1;
    if (kvxConfigPins.rfidModule == M5_RFID2_MODULE) {
        return (muxCh >= 0) ? (" (RFID2 ch" + String(muxCh) + ")") : " (RFID2)";
    }
    if (kvxConfigPins.rfidModule == RC522_SPI_MODULE) return " (RC522-SPI)";
    return "";
}

String nfcModuleTitleSuffix() {
    int8_t muxCh = pahubEnabled() ? pahubChannelForNfcModule() : (int8_t)-1;
#ifdef M5STICK
    if (kvxConfigPins.nfcModule == PN532_I2C_MODULE) {
        return (muxCh >= 0) ? (" (NFC ch" + String(muxCh) + ")") : " (PN532-G33)";
    }
    if (kvxConfigPins.nfcModule == PN532_I2C_SPI_MODULE) return " (PN532-G36)";
#else
    if (kvxConfigPins.nfcModule == PN532_I2C_MODULE) {
        return (muxCh >= 0) ? (" (NFC ch" + String(muxCh) + ")") : " (PN532-I2C)";
    }
#endif
    if (kvxConfigPins.nfcModule == PN532_SPI_MODULE) return " (PN532-SPI)";
#if !defined(LITE_VERSION)
    if (kvxConfigPins.nfcModule == ST25R3916_SPI_MODULE) return " (ST25R-SPI)";
    if (kvxConfigPins.nfcModule == ST25R3916_I2C_MODULE) {
        return (muxCh >= 0) ? (" (Unit NFC ch" + String(muxCh) + ")") : " (Unit NFC)";
    }
#endif
    return "";
}

#if !defined(REMOVE_RFID_HW_INTERFACE)
static String gpioLabel(int pin) { return "G" + String(pin); }

static void showRfidWiringPage(const String &title, const std::vector<String> &lines) {
    ScrollableTextArea area(title);
    for (const String &line : lines) area.addLine(line);
    area.addLine("");
    area.addLine("Sel/Enter = back");
    area.show();
}
#endif

void showRfidWiringHelp() {
#if !defined(REMOVE_RFID_HW_INTERFACE)
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
                         "RFID Config: Unit RFID2",
                         "or RC522 only.",
                         "PN532 / M5 NFC live under",
                         "NFC > Config.",
                         "",
                         "125 kHz uses UART pins",
                         "RFID125_RX / TX.",
                         "",
                         "SDA=" + gpioLabel(sda) + "  SCL=" + gpioLabel(scl),
                     }
                 );
             }},
            {"M5 RFID2",
             [=]() {
                 showRfidWiringPage(
                     "M5 RFID2",
                     {
                         "Chip: WS1850S @ 0x28",
                         "RFID > Config > Module",
                         "",
                         "Grove PORT.A / PaHub:",
                         "SDA=" + gpioLabel(sda) + "  SCL=" + gpioLabel(scl),
                         "5V + GND",
                         "",
                         "Read / write / clone.",
                         "No NDEF emulate.",
                     }
                 );
             }},
            {"RC522 SPI",
             [=]() {
                 showRfidWiringPage(
                     "RC522 SPI",
                     {
                         "RFID > Config > Module",
                         "SCK  " + gpioLabel(sck),
                         "MOSI " + gpioLabel(mosi),
                         "MISO " + gpioLabel(miso),
                         "SS   " + gpioLabel(ss),
                     }
                 );
             }},
            {"Back", []() {}},
        };

        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "RFID Wire");
        if (selected < 0 || selected == (int)options.size() - 1) return;
    }
#else
    displayError("No RFID wiring on this build.", true);
#endif
}

void showNfcWiringHelp() {
#if !defined(REMOVE_RFID_HW_INTERFACE)
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
                     "NFC WIRING",
                     {
                         "NFC Config: M5 Unit NFC,",
                         "PN532, or ST25R3916 SPI.",
                         "Unit RFID2 / RC522 live",
                         "under RFID > Config.",
                         "",
                         "M5 Unit NFC (ST25 @0x50)",
                         "or PN532 I2C on PORT.A",
                         "SDA=" + gpioLabel(sda) + "  SCL=" + gpioLabel(scl),
                         "VCC=5V  GND=GND",
                     }
                 );
             }},
#if !defined(LITE_VERSION)
            {"M5 Unit NFC",
             [=]() {
                 showRfidWiringPage(
                     "M5 Unit NFC",
                     {
                         "SKU U216 — ST25R3916",
                         "I2C address 0x50",
                         "",
                         "NFC > Config > Module",
                         "  -> M5 Unit NFC",
                         "",
                         "SDA -> " + gpioLabel(sda),
                         "SCL -> " + gpioLabel(scl),
                         "VCC -> 5V  GND -> GND",
                     }
                 );
             }},
#endif
            {"PN532 I2C",
             [=]() {
                 showRfidWiringPage(
                     "PN532 I2C",
                     {
                         "NFC > Config > Module",
                         "  -> PN532 on I2C",
                         "Elechouse: CH1 ON, CH2 OFF",
                         "ITEAD: SET0 H, SET1 L",
                         "I2C address 0x24",
                         "",
                         "SDA -> " + gpioLabel(sda),
                         "SCL -> " + gpioLabel(scl),
                         "VCC -> 5V  GND -> GND",
                     }
                 );
             }},
            {"PN532 SPI",
             [=]() {
                 showRfidWiringPage(
                     "PN532 SPI",
                     {
                         "NFC > Config > Module",
                         "SCK  " + gpioLabel(sck),
                         "MOSI " + gpioLabel(mosi),
                         "MISO " + gpioLabel(miso),
                         "SS   " + gpioLabel(ss),
                     }
                 );
             }},
#if !defined(LITE_VERSION)
            {"ST25R3916 SPI",
             []() {
                 showRfidWiringPage(
                     "ST25R3916 SPI",
                     {
                         "NFC > Config > Module",
                         "Pins: Config > Pins Setup",
                         "> ST25R3916 Pins",
                     }
                 );
             }},
#endif
            {"Back", []() {}},
        };

        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "NFC wiring");
        if (selected < 0 || selected == (int)options.size() - 1) return;
    }
#else
    displayError("No NFC wiring on this build.", true);
#endif
}

void rfidConfigMenu(const char *title, std::function<void()> onBack) {
    options = {
#if !defined(REMOVE_RFID_HW_INTERFACE)
        {"RFID Module", setRFIDModuleMenu },
        {"Wiring help", showRfidWiringHelp},
#endif
        {"Add MIF Key", addMifareKeyMenu},
        {"Back",        onBack          },
    };
    loopOptions(options, MENU_TYPE_SUBMENU, title);
}

void nfcConfigMenu(const char *title, std::function<void()> onBack) {
    options = {
#if !defined(REMOVE_RFID_HW_INTERFACE)
        {"NFC Module",  setNFCModuleMenu },
        {"Wiring help", showNfcWiringHelp},
#endif
        {"Add MIF Key", addMifareKeyMenu},
        {"Back",        onBack          },
    };
    loopOptions(options, MENU_TYPE_SUBMENU, title);
}

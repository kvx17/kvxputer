#include "rf_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "menu/rf/record.h"
#include "menu/rf/rf_bruteforce.h"
#include "menu/rf/rf_jammer.h"
#include "menu/rf/rf_listen.h"
#include "menu/rf/rf_scan.h"
#include "menu/rf/rf_send.h"
#include "menu/rf/rf_utils.h"
#include "menu/rf/rf_spectrum.h"
#include "menu/rf/rf_waterfall.h"

void RFMenu::optionsMenu() {
    options = {
        {"Scan/copy",       [=]() { RFScan(); }       },
#if !defined(LITE_VERSION)
        {"Record RAW",      rf_raw_record             }, // Pablo-Ortiz-Lopez
        {"Custom SubGhz",   sendCustomRF              },
#endif
        {"Spectrum",        rf_spectrum               },
#if !defined(LITE_VERSION)
        {"RSSI Spectrum",   rf_CC1101_rssi            }, // @Pirata
        {"SquareWave Spec", rf_SquareWave             }, // @Pirata
        {"Spectogram",      rf_waterfall              }, // dev_eclipse
#if defined(BUZZ_PIN) or defined(HAS_NS4168_SPKR) and defined(RF_LISTEN_H)
        {"Listen",          rf_listen                 }, // dev_eclipse
#endif
        {"Bruteforce",      rf_bruteforce             }, // dev_eclipse
        {"Jammer",          [=]() { RFJammer(true); } },
#endif
        {"Config",          [this]() { configMenu(); }},
    };
    addOptionToMainMenu();

    delay(200);
    String txt = "Radio Frequency";
    if (kvxConfigPins.rfModule == CC1101_SPI_MODULE) txt += " (CC1101)"; // Indicates if CC1101 is connected
    else txt += " Tx: " + String(kvxConfigPins.rfTx) + " Rx: " + String(kvxConfigPins.rfRx);

    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void RFMenu::configMenu() {
    options = {
        {"RF TX Pin", lambdaHelper(gsetRfTxPin, true)},
        {"RF RX Pin", lambdaHelper(gsetRfRxPin, true)},
        {"RF Module", setRFModuleMenu},
        {"RF Frequency", setRFFreqMenu},
        {"Back", [this]() { optionsMenu(); }},
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "RF Config");
}

void RFMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 7;
    int deltaRadius = scale * 10;
    int triangleSize = scale * 30;

    if (triangleSize % 2 != 0) triangleSize++;

    // Body
    tft.fillCircle(iconCenterX, iconCenterY - radius, radius, kvxConfig.priColor);
    tft.fillTriangle(
        iconCenterX,
        iconCenterY,
        iconCenterX - triangleSize / 2,
        iconCenterY + triangleSize,
        iconCenterX + triangleSize / 2,
        iconCenterY + triangleSize,
        kvxConfig.priColor
    );

    // Left Arcs
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius,
        2 * radius,
        40,
        140,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        40,
        140,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        40,
        140,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );

    // Right Arcs
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius,
        2 * radius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX,
        iconCenterY - radius,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

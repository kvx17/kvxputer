#include "infrared_menu.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "menu/infrared/TV-B-Gone.h"
#include "menu/infrared/custom_ir.h"
#include "menu/infrared/ir_jammer.h"
#include "menu/infrared/ir_read.h"
#include "menu/infrared/kremote/kremote.h"
#if defined(EVIL_EXTENSIONS)
    #include "menu/infrared/tagtinker/tagtinker.h"
#endif
#if defined(ARDUINO_M5STICK_S3)
#include <M5Unified.h>
void _setup_codec_speaker(bool enable);
#endif

void IRMenu::optionsMenu() {
#if defined(ARDUINO_M5STICK_S3)
    bool prevPower = M5.Power.getExtOutput();
    M5.Power.setExtOutput(true); // ENABLE 5V OUTPUT (Grove/Hat/IR)
    _setup_codec_speaker(false); // mute amp during IR (avoids RX noise)
#endif
    options = {
        {"kvxputer universal remote", kremoteMenu},
        {"TV-B-Gone", StartTvBGone              },
        {"Custom IR", []() { otherIRcodes(); }},
        {"IR Read",   [=]() { IrRead(); }       },
#if !defined(LITE_VERSION)
        {"IR Jammer", startIrJammer             }, // Simple frequency-adjustable jammer
#endif
#if defined(EVIL_EXTENSIONS)
        {"TagTinker ESL", tagTinkerMenu         },
#endif
        {"Config",    [this]() { configMenu(); }},
    };
    addOptionToMainMenu();

    String txt = "Infrared";
    txt += " Tx: " + String(kvxConfigPins.irTx) + " Rx: " + String(kvxConfigPins.irRx) +
           " Rpts: " + String(kvxConfigPins.irTxRepeats);
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
#if defined(ARDUINO_M5STICK_S3)
    M5.Power.setExtOutput(prevPower);
#endif
}

void IRMenu::configMenu() {
    options = {
        {"Ir TX Pin", lambdaHelper(gsetIrTxPin, true)},
        {"Ir RX Pin", lambdaHelper(gsetIrRxPin, true)},
        {"Ir TX Repeats", setIrTxRepeats},
        {"Back", [this]() { optionsMenu(); }},
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "IR Config");
}

void IRMenu::drawIcon(float scale) {
    clearIconArea();
    int iconSize = scale * 60;
    int radius = scale * 7;
    int deltaRadius = scale * 10;

    if (iconSize % 2 != 0) iconSize++;

    tft.fillRect(
        iconCenterX - iconSize / 2, iconCenterY - iconSize / 2, iconSize / 6, iconSize, kvxConfig.priColor
    );
    tft.fillRect(
        iconCenterX - iconSize / 3,
        iconCenterY - iconSize / 3,
        iconSize / 6,
        2 * iconSize / 3,
        kvxConfig.priColor
    );

    tft.drawCircle(iconCenterX - iconSize / 6, iconCenterY, radius, kvxConfig.priColor);

    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius,
        2 * radius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius + deltaRadius,
        2 * radius + deltaRadius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
    tft.drawArc(
        iconCenterX - iconSize / 6,
        iconCenterY,
        2.5 * radius + 2 * deltaRadius,
        2 * radius + 2 * deltaRadius,
        220,
        320,
        kvxConfig.priColor,
        kvxConfig.bgColor
    );
}

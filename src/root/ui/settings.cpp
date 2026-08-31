#include "root/storage/paths.h"
#include "root/ui/settings.h"
#include "root/hal/led_control.h"
#include "root/net/wifi_common.h"
#include "current_year.h"
#include "root/ui/display.h"
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
#include "root/scripting/bjs_interpreter/interpreter.h"
#endif
#include "menu/ble/ble_api/ble_api.hpp"
#include "menu/others/qrcode_menu.h"
#include "menu/rf/rf_utils.h" // for initRfModule
#include "root/input/mykeyboard.h"
#include "root/app/powerSave.h"
#include "root/storage/sd_functions.h"
#include "settingsColor.h"
#include "root/app/utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <globals.h>

int currentScreenBrightness = -1;

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
**  Function: setBrightness
**  set brightness value
**********************************************************************/
void setBrightness(uint8_t brightval, bool save) {
    if (kvxConfig.bright > 100) kvxConfig.setBright(100);
    _setBrightness(brightval);
    delay(10);

    currentScreenBrightness = brightval;
    if (save) { kvxConfig.setBright(brightval); }
}

/*********************************************************************
**  Function: getBrightness
**  get brightness value
**********************************************************************/
void getBrightness() {
    if (kvxConfig.bright > 100) {
        kvxConfig.setBright(100);
        _setBrightness(kvxConfig.bright);
        delay(10);
        setBrightness(100);
    }

    _setBrightness(kvxConfig.bright);
    delay(10);

    currentScreenBrightness = kvxConfig.bright;
}

/*********************************************************************
**  Function: gsetRotation
**  get/set rotation value
**********************************************************************/
int gsetRotation(bool set) {
    int getRot = kvxConfigPins.rotation;
    int result = ROTATION;
    int mask = ROTATION > 1 ? -2 : 2;

    options = {
        {"Default",         [&]() { result = ROTATION; }                        },
        {"Landscape (180)", [&]() { result = ROTATION + mask; }                 },
#if TFT_WIDTH >= 170 && TFT_HEIGHT >= 240
        {"Portrait (+90)",  [&]() { result = ROTATION > 0 ? ROTATION - 1 : 3; } },
        {"Portrait (-90)",  [&]() { result = ROTATION == 3 ? 0 : ROTATION + 1; }},

#endif
    };
    addOptionToMainMenu();
    if (set) loopOptions(options, "Rotation");
    else result = getRot;

    if (result > 3 || result < 0) {
        result = ROTATION;
        set = true;
    }
    if (set) {
        kvxConfigPins.setRotation(result);
        tft.setRotation(result);
        tft.setRotation(result); // must repeat, sometimes ESP32S3 miss one SPI command and it just
                                 // jumps this step and don't rotate
    }
    returnToMenu = true;

    if (result & 0b01) { // if 1 or 3
        tftWidth = TFT_HEIGHT;
#if defined(HAS_TOUCH)
        tftHeight = TFT_WIDTH - 20;
#else
        tftHeight = TFT_WIDTH;
#endif
    } else { // if 2 or 0
        tftWidth = TFT_WIDTH;
#if defined(HAS_TOUCH)
        tftHeight = TFT_HEIGHT - 20;
#else
        tftHeight = TFT_HEIGHT;
#endif
    }
    return result;
}

/*********************************************************************
**  Function: setBrightnessMenu
**  Handles Menu to set brightness
**********************************************************************/
void setBrightnessMenu() {
    int idx = 0;
    if (kvxConfig.bright == 100) idx = 0;
    else if (kvxConfig.bright == 75) idx = 1;
    else if (kvxConfig.bright == 50) idx = 2;
    else if (kvxConfig.bright == 25) idx = 3;
    else if (kvxConfig.bright == 1) idx = 4;

    options = {
        {"100%",
         [=]() { setBrightness((uint8_t)100); },
         kvxConfig.bright == 100,
         [](void *pointer, bool shouldRender) {
             setBrightness((uint8_t)100, false);
             return false;
         }},
        {"75 %",
         [=]() { setBrightness((uint8_t)75); },
         kvxConfig.bright == 75,
         [](void *pointer, bool shouldRender) {
             setBrightness((uint8_t)75, false);
             return false;
         }},
        {"50 %",
         [=]() { setBrightness((uint8_t)50); },
         kvxConfig.bright == 50,
         [](void *pointer, bool shouldRender) {
             setBrightness((uint8_t)50, false);
             return false;
         }},
        {"25 %",
         [=]() { setBrightness((uint8_t)25); },
         kvxConfig.bright == 25,
         [](void *pointer, bool shouldRender) {
             setBrightness((uint8_t)25, false);
             return false;
         }},
        {" 1 %",
         [=]() { setBrightness((uint8_t)1); },
         kvxConfig.bright == 1,
         [](void *pointer, bool shouldRender) {
             setBrightness((uint8_t)1, false);
             return false;
         }}
    };
    addOptionToMainMenu(); // this one bugs the brightness selection
    loopOptions(options, "Brightness", idx);
    setBrightness(kvxConfig.bright, false);
}

/*********************************************************************
**  Function: setSleepMode
**  Turn screen off and reduces cpu clock
**********************************************************************/
void setSleepMode() {
    sleepModeOn();
    while (1) {
        if (check(AnyKeyPress)) {
            sleepModeOff();
            returnToMenu = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*********************************************************************
**  Function: setDimmerTimeMenu
**  Handles Menu to set dimmer time
**********************************************************************/
void setDimmerTimeMenu() {
    int idx = 0;
    if (kvxConfig.dimmerSet == 10) idx = 0;
    else if (kvxConfig.dimmerSet == 20) idx = 1;
    else if (kvxConfig.dimmerSet == 30) idx = 2;
    else if (kvxConfig.dimmerSet == 60) idx = 3;
    else if (kvxConfig.dimmerSet == 0) idx = 4;
    options = {
        {"10s",      [=]() { kvxConfig.setDimmer(10); }, kvxConfig.dimmerSet == 10},
        {"20s",      [=]() { kvxConfig.setDimmer(20); }, kvxConfig.dimmerSet == 20},
        {"30s",      [=]() { kvxConfig.setDimmer(30); }, kvxConfig.dimmerSet == 30},
        {"60s",      [=]() { kvxConfig.setDimmer(60); }, kvxConfig.dimmerSet == 60},
        {"Disabled", [=]() { kvxConfig.setDimmer(0); },  kvxConfig.dimmerSet == 0 },
    };
    loopOptions(options, "Dimmer", idx);
}

/*********************************************************************
**  Function: setUIColor
**  Set and store main UI color
**********************************************************************/
void setUIColor() {

    while (1) {
        options.clear();
        int idx = UI_COLOR_COUNT;
        int i = 0;
        for (const auto &mapping : UI_COLORS) {
            if (kvxConfig.priColor == mapping.priColor && kvxConfig.secColor == mapping.secColor &&
                kvxConfig.bgColor == mapping.bgColor) {
                idx = i;
            }

            options.emplace_back(
                mapping.name,
                [=, &mapping]() {
                    uint16_t secColor = mapping.secColor;
                    uint16_t bgColor = mapping.bgColor;
                    kvxConfig.setUiColor(mapping.priColor, &secColor, &bgColor);
                },
                idx == i
            );
            ++i;
        }

        options.push_back(
            {"Custom Color",
             [=]() {
                 uint16_t oldPriColor = kvxConfig.priColor;
                 uint16_t oldSecColor = kvxConfig.secColor;
                 uint16_t oldBgColor = kvxConfig.bgColor;

                 if (setCustomUIColorMenu()) {
                     kvxConfig.setUiColor(
                         kvxConfig.priColor, &kvxConfig.secColor, &kvxConfig.bgColor
                     );
                 } else {
                     kvxConfig.priColor = oldPriColor;
                     kvxConfig.secColor = oldSecColor;
                     kvxConfig.bgColor = oldBgColor;
                 }
                 tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
             },
             idx == UI_COLOR_COUNT}
        );

        options.push_back(
            {"Invert Color",
             [=]() {
                 kvxConfig.setColorInverted(!kvxConfig.colorInverted);
                 tft.invertDisplay(kvxConfig.colorInverted);
             },
             kvxConfig.colorInverted > 0}
        );

        addOptionToMainMenu();

        int selectedOption = loopOptions(options, "UI Color", idx);
        if (selectedOption == -1 || selectedOption == options.size() - 1) return;
    }
}

uint16_t alterOneColorChannel565(uint16_t color, int newR, int newG, int newB) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    if (newR != 256) r = newR & 0x1F;
    if (newG != 256) g = newG & 0x3F;
    if (newB != 256) b = newB & 0x1F;

    return (r << 11) | (g << 5) | b;
}

bool setCustomUIColorMenu() {
    while (1) {
        options = {
            {"Primary",    [=]() { setCustomUIColorChoiceMenu(1); }},
            {"Secondary",  [=]() { setCustomUIColorChoiceMenu(2); }},
            {"Background", [=]() { setCustomUIColorChoiceMenu(3); }},
            {"Save",       [=]() {}                                },
            {"Cancel",     [=]() {}                                }
        };

        int selectedOption = loopOptions(options, "Custom Color");
        if (selectedOption == -1 || selectedOption == options.size() - 1) {
            return false;
        } else if (selectedOption == 3) {
            return true;
        }
    }
}

void setCustomUIColorChoiceMenu(int colorType) {
    while (1) {
        options = {
            {"Red Channel",   [=]() { setCustomUIColorSettingMenuR(colorType); }},
            {"Green Channel", [=]() { setCustomUIColorSettingMenuG(colorType); }},
            {"Blue Channel",  [=]() { setCustomUIColorSettingMenuB(colorType); }},
            {"Back",          [=]() {}                                          }
        };

        int selectedOption = loopOptions(options, "Color Channel");
        if (selectedOption == -1 || selectedOption == options.size() - 1) return;
    }
}

void setCustomUIColorSettingMenuR(int colorType) {
    setCustomUIColorSettingMenu(colorType, 1, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, i, 256, 256);
    });
}

void setCustomUIColorSettingMenuG(int colorType) {
    setCustomUIColorSettingMenu(colorType, 2, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, 256, i, 256);
    });
}

void setCustomUIColorSettingMenuB(int colorType) {
    setCustomUIColorSettingMenu(colorType, 3, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, 256, 256, i);
    });
}

constexpr const char *colorTypes[] = {
    "Background", // 0
    "Primary",    // 1
    "Secondary"   // 2
};

constexpr const char *rgbNames[] = {
    "Blue", // 0
    "Red",  // 1
    "Green" // 2
};

void setCustomUIColorSettingMenu(
    int colorType, int rgb, std::function<uint16_t(uint16_t, int)> colorGenerator
) {
    uint16_t color = (colorType == 1)   ? kvxConfig.priColor
                     : (colorType == 2) ? kvxConfig.secColor
                                        : kvxConfig.bgColor;

    options.clear();

    static auto hoverFunctionPriColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting primary color to: %04X\n", colorToSet);
        kvxConfig.priColor = colorToSet;
        return false;
    };
    static auto hoverFunctionSecColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting secondary color to: %04X\n", colorToSet);
        kvxConfig.secColor = colorToSet;
        return false;
    };

    static auto hoverFunctionBgColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting bg color to: %04X\n", colorToSet);
        kvxConfig.bgColor = colorToSet;
        tft.fillScreen(kvxConfig.bgColor);
        return false;
    };

    static uint16_t colorStorage[32];
    int selectedIndex = 0;
    int i = 0;
    int index = 0;

    if (rgb == 1) {
        selectedIndex = (color >> 11) & 0x1F;
    } else if (rgb == 2) {
        selectedIndex = ((color >> 5) & 0x3F);
    } else {
        selectedIndex = color & 0x1F;
    }

    while (i <= (rgb == 2 ? 63 : 31)) {
        if (i == 0 || (rgb == 2 && (i + 1) % 2 == 0) || (rgb != 2)) {
            uint16_t updatedColor = colorGenerator(color, i);
            colorStorage[index] = updatedColor;

            options.emplace_back(
                String(i),
                [colorType, updatedColor]() {
                    if (colorType == 1) kvxConfig.priColor = updatedColor;
                    else if (colorType == 2) kvxConfig.secColor = updatedColor;
                    else kvxConfig.bgColor = updatedColor;
                },
                selectedIndex == i,
                (colorType == 1 ? hoverFunctionPriColor
                                : (colorType == 2 ? hoverFunctionSecColor : hoverFunctionBgColor)),
                &colorStorage[index]
            );
            ++index;
        }
        ++i;
    }

    addOptionToMainMenu();

    int selectedOption = loopOptions(
        options,
        MENU_TYPE_SUBMENU,
        (String(colorType == 1 ? "Primary" : (colorType == 2 ? "Secondary" : "Background")) + " - " +
         (rgb == 1 ? "Red" : (rgb == 2 ? "Green" : "Blue")))
            .c_str(),
        (rgb != 2) ? selectedIndex : (selectedIndex > 0 ? (selectedIndex + 1) / 2 : 0)
    );
    if (selectedOption == -1 || selectedOption == options.size() - 1) {
        if (colorType == 1) {
            kvxConfig.priColor = color;
        } else if (colorType == 2) {
            kvxConfig.secColor = color;
        } else {
            kvxConfig.bgColor = color;
        }
        return;
    }
}

/*********************************************************************
**  Function: setSoundConfig - 01/2026 - Refactored "ConfigMenu" (this function manteined for
* retrocompatibility)
**  Enable or disable sound
**********************************************************************/
void setSoundConfig() {
    options = {
        {"Sound off", [=]() { kvxConfig.setSoundEnabled(0); }, kvxConfig.soundEnabled == 0},
        {"Sound on",  [=]() { kvxConfig.setSoundEnabled(1); }, kvxConfig.soundEnabled == 1},
    };
    loopOptions(options, "Sound", kvxConfig.soundEnabled);
}

/*********************************************************************
**  Function: setSoundVolume
**  Set sound volume
**********************************************************************/
void setSoundVolume() {
    options = {
        {"10%",  [=]() { kvxConfig.setSoundVolume(10); },  kvxConfig.soundVolume == 10 },
        {"20%",  [=]() { kvxConfig.setSoundVolume(20); },  kvxConfig.soundVolume == 20 },
        {"30%",  [=]() { kvxConfig.setSoundVolume(30); },  kvxConfig.soundVolume == 30 },
        {"40%",  [=]() { kvxConfig.setSoundVolume(40); },  kvxConfig.soundVolume == 40 },
        {"50%",  [=]() { kvxConfig.setSoundVolume(50); },  kvxConfig.soundVolume == 50 },
        {"60%",  [=]() { kvxConfig.setSoundVolume(60); },  kvxConfig.soundVolume == 60 },
        {"70%",  [=]() { kvxConfig.setSoundVolume(70); },  kvxConfig.soundVolume == 70 },
        {"80%",  [=]() { kvxConfig.setSoundVolume(80); },  kvxConfig.soundVolume == 80 },
        {"90%",  [=]() { kvxConfig.setSoundVolume(90); },  kvxConfig.soundVolume == 90 },
        {"100%", [=]() { kvxConfig.setSoundVolume(100); }, kvxConfig.soundVolume == 100},
    };
    loopOptions(options, "Volume", kvxConfig.soundVolume);
}

#ifdef HAS_RGB_LED
/*********************************************************************
**  Function: setLedBlinkConfig - 01/2026 - Refactored "ConfigMenu" (this function manteined for
* retrocompatibility)
**  Enable or disable led blink
**********************************************************************/
void setLedBlinkConfig() {
    options = {
        {"Led Blink off", [=]() { kvxConfig.setLedBlinkEnabled(0); }, kvxConfig.ledBlinkEnabled == 0},
        {"Led Blink on",  [=]() { kvxConfig.setLedBlinkEnabled(1); }, kvxConfig.ledBlinkEnabled == 1},
    };
    loopOptions(options, "LED Blink", kvxConfig.ledBlinkEnabled);
}
#endif

/*********************************************************************
**  Function: setWifiStartupConfig
**  Enable or disable wifi connection at startup
**********************************************************************/
void setWifiStartupConfig() {
    options = {
        {"Disable", [=]() { kvxConfig.setWifiAtStartup(0); }, kvxConfig.wifiAtStartup == 0},
        {"Enable",  [=]() { kvxConfig.setWifiAtStartup(1); }, kvxConfig.wifiAtStartup == 1},
    };
    loopOptions(options, "WiFi Startup", kvxConfig.wifiAtStartup);
}

/*********************************************************************
**  Function: addEvilWifiMenu
**  Handles Menu to add evil wifi names into config list
**********************************************************************/
void addEvilWifiMenu() {
    String apName = keyboard("", 30, "Evil Portal SSID");
    if (apName != "\x1B") kvxConfig.addEvilWifiName(apName);
}

/*********************************************************************
**  Function: removeEvilWifiMenu
**  Handles Menu to remove evil wifi names from config list
**********************************************************************/
void removeEvilWifiMenu() {
    options = {};

    for (const auto &wifi_name : kvxConfig.evilWifiNames) {
        options.push_back({wifi_name.c_str(), [wifi_name]() { kvxConfig.removeEvilWifiName(wifi_name); }});
    }

    options.push_back({"Cancel", [=]() { backToMenu(); }});

    loopOptions(options, "Evil WiFi");
}

/*********************************************************************
**  Function: setEvilEndpointCreds
**  Handles menu for changing the endpoint to access captured creds
**********************************************************************/
void setEvilEndpointCreds() {
    String userInput = keyboard(kvxConfig.evilPortalEndpoints.getCredsEndpoint, 30, "Evil creds endpoint");
    if (userInput != "\x1B") kvxConfig.setEvilEndpointCreds(userInput);
}

/*********************************************************************
**  Function: setEvilEndpointSsid
**  Handles menu for changing the endpoint to change evilSsid
**********************************************************************/
void setEvilEndpointSsid() {
    String userInput = keyboard(kvxConfig.evilPortalEndpoints.setSsidEndpoint, 30, "Evil creds endpoint");
    if (userInput != "\x1B") kvxConfig.setEvilEndpointSsid(userInput);
}

/*********************************************************************
**  Function: setEvilAllowGetCredentials
**  Handles menu for toggling access to the credential list endpoint
**********************************************************************/

void setEvilAllowGetCreds() {
    options = {
        {"Disallow",
         [=]() { kvxConfig.setEvilAllowGetCreds(false); },
         kvxConfig.evilPortalEndpoints.allowGetCreds == false},
        {"Allow",
         [=]() { kvxConfig.setEvilAllowGetCreds(true); },
         kvxConfig.evilPortalEndpoints.allowGetCreds == true },
    };
    loopOptions(options, "Get Creds", kvxConfig.evilPortalEndpoints.allowGetCreds);
}

/*********************************************************************
**  Function: setEvilAllowGetCredentials
**  Handles menu for toggling access to the change SSID endpoint
**********************************************************************/

void setEvilAllowSetSsid() {
    options = {
        {"Disallow",
         [=]() { kvxConfig.setEvilAllowSetSsid(false); },
         kvxConfig.evilPortalEndpoints.allowSetSsid == false},
        {"Allow",
         [=]() { kvxConfig.setEvilAllowSetSsid(true); },
         kvxConfig.evilPortalEndpoints.allowSetSsid == true },
    };
    loopOptions(options, "Set SSID", kvxConfig.evilPortalEndpoints.allowSetSsid);
}

/*********************************************************************
**  Function: setEvilAllowEndpointDisplay
**  Handles menu for toggling the display of the Evil Portal endpoints
**********************************************************************/

void setEvilAllowEndpointDisplay() {
    options = {
        {"Disallow",
         [=]() { kvxConfig.setEvilAllowEndpointDisplay(false); },
         kvxConfig.evilPortalEndpoints.showEndpoints == false},
        {"Allow",
         [=]() { kvxConfig.setEvilAllowEndpointDisplay(true); },
         kvxConfig.evilPortalEndpoints.showEndpoints == true },
    };
    loopOptions(options, "Endpoints", kvxConfig.evilPortalEndpoints.showEndpoints);
}

/*********************************************************************
** Function: setEvilPasswordMode
** Handles menu for setting the evil portal password mode
***********************************************************************/
void setEvilPasswordMode() {
    options = {
        {"Save 'password'",
         [=]() { kvxConfig.setEvilPasswordMode(FULL_PASSWORD); },
         kvxConfig.evilPortalPasswordMode == FULL_PASSWORD  },
        {"Save 'p******d'",
         [=]() { kvxConfig.setEvilPasswordMode(FIRST_LAST_CHAR); },
         kvxConfig.evilPortalPasswordMode == FIRST_LAST_CHAR},
        {"Save '*hidden*'",
         [=]() { kvxConfig.setEvilPasswordMode(HIDE_PASSWORD); },
         kvxConfig.evilPortalPasswordMode == HIDE_PASSWORD  },
        {"Save length",
         [=]() { kvxConfig.setEvilPasswordMode(SAVE_LENGTH); },
         kvxConfig.evilPortalPasswordMode == SAVE_LENGTH    },
    };
    loopOptions(options, "Password Mode", kvxConfig.evilPortalPasswordMode);
}

/*********************************************************************
** Function: setEvilGatewayIp
** Handles menu for setting the Evil Portal gateway IP
***********************************************************************/
void setEvilGatewayIp() {
    options = {
        {"172.0.0.1",
         [=]() { kvxConfig.setEvilGatewayIp("172.0.0.1"); },
         kvxConfig.evilPortalGatewayIp == "172.0.0.1"},
        {"192.168.4.1",
         [=]() { kvxConfig.setEvilGatewayIp("192.168.4.1"); },
         kvxConfig.evilPortalGatewayIp == "192.168.4.1"},
        {"Custom", [=]() {
             String ip = num_keyboard("", 15, "Gateway Addr");
             kvxConfig.setEvilGatewayIp(ip);
         }},
    };
    loopOptions(options, "Gateway IP", kvxConfig.evilPortalGatewayIp == "192.168.4.1" ? 1 : 0);
}

/*********************************************************************
**  Function: setRFModuleMenu
**  Handles Menu to set the RF module in use
**********************************************************************/
void setRFModuleMenu() {
    int result = 0;
    int idx = 0;
    uint8_t pins_setup = 0;
    if (kvxConfigPins.rfModule == M5_RF_MODULE) idx = 0;
    else if (kvxConfigPins.rfModule == CC1101_SPI_MODULE) {
        idx = 1;
#if defined(ARDUINO_M5STICK_C_PLUS) || defined(ARDUINO_M5STICK_C_PLUS2)
        if (kvxConfigPins.CC1101_bus.mosi == GPIO_NUM_26) idx = 2;
#endif
    }

    options = {
        {"M5 RF433T/R",         [&]() { result = M5_RF_MODULE; }   },
#if defined(ARDUINO_M5STICK_C_PLUS) || defined(ARDUINO_M5STICK_C_PLUS2)
        {"CC1101 (legacy)",     [&pins_setup]() { pins_setup = 1; }},
        {"CC1101 (Shared SPI)", [&pins_setup]() { pins_setup = 2; }},
#else
        {"CC1101", [&]() { result = CC1101_SPI_MODULE; }},
#endif
        /* WIP:
         * #ifdef USE_CC1101_VIA_PCA9554
         * {"CC1101+PCA9554",  [&]() { result = 2; }},
         * #endif
         */
    };
    loopOptions(options, "RF Module", idx);
    if (result == CC1101_SPI_MODULE || pins_setup > 0) {
        // This setting is meant to StickCPlus and StickCPlus2 to setup the ports from RF Menu
        if (pins_setup == 1) {
            result = CC1101_SPI_MODULE;
            kvxConfigPins.setCC1101Pins(
                {(gpio_num_t)CC1101_SCK_PIN,
                 (gpio_num_t)CC1101_MISO_PIN,
                 (gpio_num_t)CC1101_MOSI_PIN,
                 (gpio_num_t)CC1101_SS_PIN,
                 (gpio_num_t)CC1101_GDO0_PIN,
                 GPIO_NUM_NC}
            );
            kvxConfigPins.setNrf24Pins(
                {(gpio_num_t)CC1101_SCK_PIN,
                 (gpio_num_t)CC1101_MISO_PIN,
                 (gpio_num_t)CC1101_MOSI_PIN,
                 (gpio_num_t)CC1101_SS_PIN,
                 (gpio_num_t)CC1101_GDO0_PIN,
                 GPIO_NUM_NC}
            );
        } else if (pins_setup == 2) {
#if CONFIG_SOC_GPIO_OUT_RANGE_MAX > 30
            result = CC1101_SPI_MODULE;
            kvxConfigPins.setCC1101Pins(
                {(gpio_num_t)SDCARD_SCK,
                 (gpio_num_t)SDCARD_MISO,
                 (gpio_num_t)SDCARD_MOSI,
                 GPIO_NUM_33,
                 GPIO_NUM_32,
                 GPIO_NUM_NC}
            );
            kvxConfigPins.setNrf24Pins(
                {(gpio_num_t)SDCARD_SCK,
                 (gpio_num_t)SDCARD_MISO,
                 (gpio_num_t)SDCARD_MOSI,
                 GPIO_NUM_33,
                 GPIO_NUM_32,
                 GPIO_NUM_NC}
            );
#endif
        }
        if (initRfModule()) {
            kvxConfigPins.setRfModule(CC1101_SPI_MODULE);
            deinitRfModule();
            if (pins_setup == 1) AUX_SPI.end();
            return;
        }
        // else display an error
        displayError("CC1101 not found", true);
        if (pins_setup == 1)
            qrcode_display("https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick.jpg");
        if (pins_setup == 2)
            qrcode_display(
                "https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick_SDCard.jpg"
            );
        while (!check(AnyKeyPress)) vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    // fallback to "M5 RF433T/R" on errors
    kvxConfigPins.setRfModule(M5_RF_MODULE);
}

/*********************************************************************
**  Function: setRFFreqMenu
**  Handles Menu to set the default frequency for the RF module
**********************************************************************/
void setRFFreqMenu() {
    float result = 433.92;
    String freq_str = num_keyboard(String(kvxConfigPins.rfFreq), 10, "Default frequency:");
    if (freq_str == "\x1B") return;
    if (freq_str.length() > 1) {
        result = freq_str.toFloat();          // returns 0 if not valid
        if (result >= 280 && result <= 928) { // TODO: check valid freq according to current module?
            kvxConfigPins.setRfFreq(result);
            return;
        }
    }
    // else
    displayError("Invalid frequency");
    kvxConfigPins.setRfFreq(433.92); // reset to default
    delay(1000);
}

/*********************************************************************
**  Function: setRFIDModuleMenu
**  Handles Menu to set the RFID module in use
**********************************************************************/
void setRFIDModuleMenu() {
    options = {
        {"M5 RFID2",
         [=]() { kvxConfigPins.setRfidModule(M5_RFID2_MODULE); },
         kvxConfigPins.rfidModule == M5_RFID2_MODULE     },
#ifdef M5STICK
        {"PN532 I2C G33",
         [=]() { kvxConfigPins.setRfidModule(PN532_I2C_MODULE); },
         kvxConfigPins.rfidModule == PN532_I2C_MODULE    },
        {"PN532 I2C G36",
         [=]() { kvxConfigPins.setRfidModule(PN532_I2C_SPI_MODULE); },
         kvxConfigPins.rfidModule == PN532_I2C_SPI_MODULE},
#else
        {"PN532 on I2C",
         [=]() { kvxConfigPins.setRfidModule(PN532_I2C_MODULE); },
         kvxConfigPins.rfidModule == PN532_I2C_MODULE},
#endif
        {"PN532 on SPI",
         [=]() { kvxConfigPins.setRfidModule(PN532_SPI_MODULE); },
         kvxConfigPins.rfidModule == PN532_SPI_MODULE    },
        {"RC522 on SPI",
         [=]() { kvxConfigPins.setRfidModule(RC522_SPI_MODULE); },
         kvxConfigPins.rfidModule == RC522_SPI_MODULE    },
#if !defined(LITE_VERSION)
        {"ST25R3916 SPI",
         [=]() { kvxConfigPins.setRfidModule(ST25R3916_SPI_MODULE); },
         kvxConfigPins.rfidModule == ST25R3916_SPI_MODULE},
        {"ST25R3916 I2C",
         [=]() { kvxConfigPins.setRfidModule(ST25R3916_I2C_MODULE); },
         kvxConfigPins.rfidModule == ST25R3916_I2C_MODULE},
#endif
    };
    loopOptions(options, "RFID Module", kvxConfigPins.rfidModule);
}

/*********************************************************************
**  Function: addMifareKeyMenu
**  Handles Menu to add MIFARE keys into config list
**********************************************************************/
void addMifareKeyMenu() {
    String key = keyboard("", 12, "MIFARE key");
    if (key != "\x1B") kvxConfig.addMifareKey(key);
}

/*********************************************************************
**  Function: setClock
**  Handles Menu to set timezone to NTP
**********************************************************************/
const char *ntpServer = "pool.ntp.org";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, 0, 0);

void setClock() {
#if defined(HAS_RTC)
    RTC_TimeTypeDef TimeStruct;
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
#endif

    options = {
        {"Via NTP Set Timezone",                                                 [&]() { kvxConfig.setAutomaticTimeUpdateViaNTP(true); } },
        {"Set Time Manually",                                                    [&]() { kvxConfig.setAutomaticTimeUpdateViaNTP(false); }},
        {("Daylight Savings " + String(kvxConfig.dst ? "On" : "Off")).c_str(),
         [&]() {
             kvxConfig.setDST(!kvxConfig.dst);
             updateClockTimezone();
             returnToMenu = true;
         }                                                                                                                                 },
        {(kvxConfig.clock24hr ? "24-Hour Format" : "12-Hour Format"),          [&]() {
             kvxConfig.setClock24Hr(!kvxConfig.clock24hr);
             returnToMenu = true;
         }                                                          }
    };

    addOptionToMainMenu();
    loopOptions(options, "Clock");

    if (kvxConfig.automaticTimeUpdateViaNTP) {
        if (!wifiConnected) wifiConnectMenu();

        options.clear();

#ifndef LITE_VERSION

        struct TimezoneMapping {
            const char *name;
            float offset;
        };

        constexpr TimezoneMapping timezoneMappings[] = {
            {"UTC-12 (Baker Island, Howland Island)",     -12  },
            {"UTC-11 (Niue, Pago Pago)",                  -11  },
            {"UTC-10 (Honolulu, Papeete)",                -10  },
            {"UTC-9 (Anchorage, Gambell)",                -9   },
            {"UTC-9.5 (Marquesas Islands)",               -9.5 },
            {"UTC-8 (Los Angeles, Vancouver, Tijuana)",   -8   },
            {"UTC-7 (Denver, Phoenix, Edmonton)",         -7   },
            {"UTC-6 (Mexico City, Chicago, Tegucigalpa)", -6   },
            {"UTC-5 (New York, Toronto, Lima)",           -5   },
            {"UTC-4 (Caracas, Santiago, La Paz)",         -4   },
            {"UTC-3 (Brasilia, Sao Paulo, Montevideo)",   -3   },
            {"UTC-2 (South Georgia, Mid-Atlantic)",       -2   },
            {"UTC-1 (Azores, Cape Verde)",                -1   },
            {"UTC+0 (London, Lisbon, Casablanca)",        0    },
            {"UTC+0.5 (Tehran)",                          0.5  },
            {"UTC+1 (Berlin, Paris, Rome)",               1    },
            {"UTC+2 (Cairo, Athens, Johannesburg)",       2    },
            {"UTC+3 (Moscow, Riyadh, Nairobi)",           3    },
            {"UTC+3.5 (Tehran)",                          3.5  },
            {"UTC+4 (Dubai, Baku, Muscat)",               4    },
            {"UTC+4.5 (Kabul)",                           4.5  },
            {"UTC+5 (Islamabad, Karachi, Tashkent)",      5    },
            {"UTC+5.5 (New Delhi, Mumbai, Colombo)",      5.5  },
            {"UTC+5.75 (Kathmandu)",                      5.75 },
            {"UTC+6 (Dhaka, Almaty, Omsk)",               6    },
            {"UTC+6.5 (Yangon, Cocos Islands)",           6.5  },
            {"UTC+7 (Bangkok, Jakarta, Hanoi)",           7    },
            {"UTC+8 (Beijing, Singapore, Perth)",         8    },
            {"UTC+8.75 (Eucla)",                          8.75 },
            {"UTC+9 (Tokyo, Seoul, Pyongyang)",           9    },
            {"UTC+9.5 (Adelaide, Darwin)",                9.5  },
            {"UTC+10 (Sydney, Melbourne, Vladivostok)",   10   },
            {"UTC+10.5 (Lord Howe Island)",               10.5 },
            {"UTC+11 (Solomon Islands, Nouméa)",          11   },
            {"UTC+12 (Auckland, Fiji, Kamchatka)",        12   },
            {"UTC+12.75 (Chatham Islands)",               12.75},
            {"UTC+13 (Tonga, Phoenix Islands)",           13   },
            {"UTC+14 (Kiritimati)",                       14   }
        };

        int idx = 0;
        int i = 0;
        for (const auto &mapping : timezoneMappings) {
            if (kvxConfig.tmz == mapping.offset) { idx = i; }

            options.emplace_back(
                mapping.name, [=, &mapping]() { kvxConfig.setTmz(mapping.offset); }, idx == i
            );
            ++i;
        }

#else
        constexpr float timezoneOffsets[] = {-12, -11, -10,  -9.5, -9,  -8,    -7, -6, -5,   -4,
                                             -3,  -2,  -1,   0,    0.5, 1,     2,  3,  3.5,  4,
                                             4.5, 5,   5.5,  5.75, 6,   6.5,   7,  8,  8.75, 9,
                                             9.5, 10,  10.5, 11,   12,  12.75, 13, 14};

        int idx = 0;
        int i = 0;
        for (const auto &offset : timezoneOffsets) {
            if (kvxConfig.tmz == offset) idx = i;

            options.emplace_back(
                ("UTC" + String(offset >= 0 ? "+" : "") + String(offset)).c_str(),
                [=]() { kvxConfig.setTmz(offset); },
                kvxConfig.tmz == offset
            );
            ++i;
        }

#endif

        addOptionToMainMenu();

        loopOptions(options, "Timezone", idx);

        updateClockTimezone();

    } else {
        int hr, mn, am = 0; // Initialize am to default value
        options = {};
        for (int i = 0; i < 12; i++) {
            String tmp = String(i < 10 ? "0" : "") + String(i);
            options.push_back({tmp.c_str(), [&]() { delay(1); }});
        }

        hr = loopOptions(options, MENU_TYPE_SUBMENU, "Set Hour");
        options.clear();

        for (int i = 0; i < 60; i++) {
            String tmp = String(i < 10 ? "0" : "") + String(i);
            options.push_back({tmp.c_str(), [&]() { delay(1); }});
        }

        mn = loopOptions(options, MENU_TYPE_SUBMENU, "Set Minute");
        options.clear();

        options = {
            {"AM", [&]() { am = 0; } },
            {"PM", [&]() { am = 12; }},
        };

        loopOptions(options, "AM/PM");

#if defined(HAS_RTC)
        TimeStruct.Hours = hr + am;
        TimeStruct.Minutes = mn;
        TimeStruct.Seconds = 0;
        _rtc.SetTime(&TimeStruct);
        _rtc.GetTime(&_time);
        _rtc.GetDate(&_date);

        struct tm timeinfo = {};
        timeinfo.tm_sec = _time.Seconds;
        timeinfo.tm_min = _time.Minutes;
        timeinfo.tm_hour = _time.Hours;
        timeinfo.tm_mday = _date.Date;
        timeinfo.tm_mon = _date.Month > 0 ? _date.Month - 1 : 0;
        timeinfo.tm_year = _date.Year >= 1900 ? _date.Year - 1900 : 0;
        time_t epoch = mktime(&timeinfo);
        struct timeval tv = {.tv_sec = epoch};
        settimeofday(&tv, nullptr);
#else
        rtc.setTime(0, mn, hr + am, 20, 06, CURRENT_YEAR); // send me a gift, @Pirata!
        struct tm t = rtc.getTimeStruct();
        time_t epoch = mktime(&t);
        struct timeval tv = {.tv_sec = epoch};
        settimeofday(&tv, nullptr);
#endif
        clock_set = true;
    }
}

void runClockLoop(bool showMenuHint) {
    int tmp = 0;
    unsigned long hintStartTime = millis();
    bool hintVisible = showMenuHint;

#if defined(HAS_RTC)
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
    _rtc.GetTime(&_time);
#endif

    // Delay due to SelPress() detected on run
    tft.fillScreen(kvxConfig.bgColor);
    delay(300);

    for (;;) {
        if (millis() - tmp > 1000) {
#if defined(HAS_RTC)
            updateTimeStr(_rtc.getTimeStruct());
#else
            updateTimeStr(rtc.getTimeStruct());
#endif
            Serial.print("Current time: ");
            Serial.println(timeStr);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawRect(
                BORDER_PAD_X,
                BORDER_PAD_X,
                tftWidth - 2 * BORDER_PAD_X,
                tftHeight - 2 * BORDER_PAD_X,
                kvxConfig.priColor
            );
            uint8_t f_size = 4;
            for (uint8_t i = 4; i > 0; i--) {
                if (i * LW * strlen(timeStr) < (tftWidth - BORDER_PAD_X * 2)) {
                    f_size = i;
                    break;
                }
            }
            tft.setTextSize(f_size);
            tft.drawCentreString(timeStr, tftWidth / 2, tftHeight / 2 - 13, 1);

            // "OK to show menu" hint management
            if (hintVisible && (millis() - hintStartTime < 5000)) {
                tft.setTextSize(1);
                tft.drawCentreString("OK to show menu", tftWidth / 2, tftHeight / 2 + 25, 1);
            } else if (hintVisible && (millis() - hintStartTime >= 5000)) {
                // Clear hint after 5 seconds
                tft.fillRect(
                    BORDER_PAD_X + 1,
                    tftHeight / 2 + 20,
                    tftWidth - 2 * BORDER_PAD_X - 2,
                    20,
                    kvxConfig.bgColor
                );
                hintVisible = false;
            }
            tmp = millis();
        }

        // Checks to exit the loop
        if (check(SelPress)) {
            tft.fillScreen(kvxConfig.bgColor);
            if (showMenuHint) {
                // Exits the loop to return to the caller (ClockMenu)
                break;
            } else {
                // Original behavior
                returnToMenu = true;
                break;
            }
        }

        if (check(EscPress)) {
            tft.fillScreen(kvxConfig.bgColor);
            returnToMenu = true;
            break;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/*********************************************************************
**  Function: gsetIrTxPin
**  get or set IR Tx Pin
**********************************************************************/
int gsetIrTxPin(bool set) {
    int result = kvxConfigPins.irTx;

    if (result > 50) kvxConfigPins.setIrTxPin(TXLED);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = IR_TX_PINS;
        int idx = 100;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == kvxConfigPins.irTx && idx == 100) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != SDCARD_CS && i != SDCARD_MOSI && i != SDCARD_MISO)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { kvxConfigPins.setIrTxPin(pin.second); },
                     pin.second == kvxConfigPins.irTx}
                );
        }

        loopOptions(options, "IR TX Pin", idx);
    }

    returnToMenu = true;
    return kvxConfigPins.irTx;
}

void setIrTxRepeats() {
    uint8_t chRpts = 0; // Chosen Repeats

    options = {
        {"None",             [&]() { chRpts = 0; } },
        {"5  (+ 1 initial)", [&]() { chRpts = 5; } },
        {"10 (+ 1 initial)", [&]() { chRpts = 10; }},
        {"Custom",           [&]() {
             // up to 99 repeats
             String rpt =
                 num_keyboard(String(kvxConfigPins.irTxRepeats), 2, "Nbr of Repeats (+ 1 initial)");
             chRpts = static_cast<uint8_t>(rpt.toInt());
         }                       },
    };
    addOptionToMainMenu();

    loopOptions(options, "IR Repeats");
}
/*********************************************************************
**  Function: gsetIrRxPin
**  get or set IR Rx Pin
**********************************************************************/
int gsetIrRxPin(bool set) {
    int result = kvxConfigPins.irRx;

    if (result > 45) kvxConfigPins.setIrRxPin(GROVE_SCL);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = IR_RX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == kvxConfigPins.irRx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != SDCARD_CS && i != SDCARD_MOSI && i != SDCARD_MISO)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { kvxConfigPins.setIrRxPin(pin.second); },
                     pin.second == kvxConfigPins.irRx}
                );
        }

        loopOptions(options, "IR RX Pin");
    }

    returnToMenu = true;
    return kvxConfigPins.irRx;
}

/*********************************************************************
**  Function: gsetRfTxPin
**  get or set RF Tx Pin
**********************************************************************/
int gsetRfTxPin(bool set) {
    int result = kvxConfigPins.rfTx;

    if (result > 45) kvxConfigPins.setRfTxPin(GROVE_SDA);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = RF_TX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == kvxConfigPins.rfTx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != SDCARD_CS && i != SDCARD_MOSI && i != SDCARD_MISO)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { kvxConfigPins.setRfTxPin(pin.second); },
                     pin.second == kvxConfigPins.rfTx}
                );
        }

        loopOptions(options, "RF TX Pin");
        options.clear();
    }

    returnToMenu = true;
    return kvxConfigPins.rfTx;
}

/*********************************************************************
**  Function: gsetRfRxPin
**  get or set FR Rx Pin
**********************************************************************/
int gsetRfRxPin(bool set) {
    int result = kvxConfigPins.rfRx;

    if (result > 36) kvxConfigPins.setRfRxPin(GROVE_SCL);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = RF_RX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == kvxConfigPins.rfRx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != SDCARD_CS && i != SDCARD_MOSI && i != SDCARD_MISO)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { kvxConfigPins.setRfRxPin(pin.second); },
                     pin.second == kvxConfigPins.rfRx}
                );
        }

        loopOptions(options, "RF RX Pin");
        options.clear();
    }

    returnToMenu = true;
    return kvxConfigPins.rfRx;
}

/*********************************************************************
**  Function: setStartupApp
**  Handles Menu to set startup app
**********************************************************************/
void setStartupApp() {
    int idx = 0;
    if (kvxConfig.startupApp == "") idx = 0;

    options = {
        {"None", [=]() { kvxConfig.setStartupApp(""); }, kvxConfig.startupApp == ""}
    };

    int index = 0;
    for (String appName : startupApp.getAppNames()) {
        index++;
        if (kvxConfig.startupApp == appName) idx = index;

        options.push_back({appName.c_str(), [=]() {
                               kvxConfig.setStartupApp(appName);
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
                               if (appName == "JS Interpreter") {
                                   options = getScriptsOptionsList("", true);
                                   loopOptions(options, MENU_TYPE_SUBMENU, "Startup Script");
                               }
#endif
                           }});
    }

    loopOptions(options, "Startup App", idx);
    options.clear();
}

/*********************************************************************
**  Function: setGpsBaudrateMenu
**  Handles Menu to set the baudrate for the GPS module
**********************************************************************/
void setGpsBaudrateMenu() {
    options = {
        {"9600 bps",   [=]() { kvxConfigPins.setGpsBaudrate(9600); },  kvxConfigPins.gpsBaudrate == 9600 },
        {"19200 bps",  [=]() { kvxConfigPins.setGpsBaudrate(19200); }, kvxConfigPins.gpsBaudrate == 19200},
        {"38400 bps",  [=]() { kvxConfigPins.setGpsBaudrate(38400); }, kvxConfigPins.gpsBaudrate == 38400},
        {"57600 bps",  [=]() { kvxConfigPins.setGpsBaudrate(57600); }, kvxConfigPins.gpsBaudrate == 57600},
        {"115200 bps",
         [=]() { kvxConfigPins.setGpsBaudrate(115200); },
         kvxConfigPins.gpsBaudrate == 115200                                                               },
    };

    loopOptions(options, "GPS Baud", kvxConfigPins.gpsBaudrate);
}

/*********************************************************************
**  Function: setWifiApSsidMenu
**  Handles Menu to set the WiFi AP SSID
**********************************************************************/
void setWifiApSsidMenu() {
    const bool isDefault = kvxConfig.wifiAp.ssid == "KvxputerNet";

    options = {
        {"Default (KvxputerNet)",
         [=]() { kvxConfig.setWifiApCreds("KvxputerNet", kvxConfig.wifiAp.pwd); },
         isDefault                                                                            },
        {"Custom",
         [=]() {
             String newSsid = keyboard(kvxConfig.wifiAp.ssid, 32, "WiFi AP SSID:");
             if (newSsid != "\x1B") {
                 if (!newSsid.isEmpty()) kvxConfig.setWifiApCreds(newSsid, kvxConfig.wifiAp.pwd);
                 else displayError("SSID cannot be empty", true);
             }
         },                                                                         !isDefault},
    };
    addOptionToMainMenu();

    loopOptions(options, "AP SSID", isDefault ? 0 : 1);
}

/*********************************************************************
**  Function: setWifiApPasswordMenu
**  Handles Menu to set the WiFi AP Password
**********************************************************************/
void setWifiApPasswordMenu() {
    const bool isDefault = kvxConfig.wifiAp.pwd == "kvxputernet";

    options = {
        {"Default (kvxputernet)",
         [=]() { kvxConfig.setWifiApCreds(kvxConfig.wifiAp.ssid, "kvxputernet"); },
         isDefault                                                                             },
        {"Custom",
         [=]() {
             String newPassword = keyboard(kvxConfig.wifiAp.pwd, 32, "WiFi AP Password:", true);
             if (newPassword != "\x1B") {
                 if (!newPassword.isEmpty()) kvxConfig.setWifiApCreds(kvxConfig.wifiAp.ssid, newPassword);
                 else displayError("Password cannot be empty", true);
             }
         },                                                                          !isDefault},
    };
    addOptionToMainMenu();

    loopOptions(options, "AP Password", isDefault ? 0 : 1);
}

/*********************************************************************
**  Function: setWifiApCredsMenu
**  Handles Menu to configure WiFi AP Credentials
**********************************************************************/
void setWifiApCredsMenu() {
    options = {
        {"SSID",     setWifiApSsidMenu    },
        {"Password", setWifiApPasswordMenu},
    };
    addOptionToMainMenu();

    loopOptions(options, "AP Creds");
}

/*********************************************************************
**  Function: setNetworkCredsMenu
**  Main Menu for setting Network credentials (BLE & WiFi)
**********************************************************************/
void setNetworkCredsMenu() {
    options = {
        {"WiFi AP Creds", setWifiApCredsMenu}
    };
    addOptionToMainMenu();

    loopOptions(options, "Network Creds");
}

/*********************************************************************
**  Function: setBadUSBBLEMenu
**  Main Menu for setting Bad USB/BLE options
**********************************************************************/
void setBadUSBBLEMenu() {
    options = {
        {"Keyboard Layout", setBadUSBBLEKeyboardLayoutMenu},
        {"Key Delay",       setBadUSBBLEKeyDelayMenu      },
        {"Show Output",     setBadUSBBLEShowOutputMenu    },
    };
    addOptionToMainMenu();

    loopOptions(options, "BadUSB BLE");
}

/*********************************************************************
**  Function: setBadUSBBLEKeyboardLayoutMenu
**  Main Menu for setting Bad USB/BLE Keyboard Layout
**********************************************************************/
void setBadUSBBLEKeyboardLayoutMenu() {
    uint8_t opt = kvxConfig.badUSBBLEKeyboardLayout;

    options.clear();
    options = {
        {"US International",      [&]() { opt = 0; } },
        {"Danish",                [&]() { opt = 1; } },
        {"English (UK)",          [&]() { opt = 2; } },
        {"French (AZERTY)",       [&]() { opt = 3; } },
        {"German",                [&]() { opt = 4; } },
        {"Hungarian",             [&]() { opt = 5; } },
        {"Italian",               [&]() { opt = 6; } },
        {"Polish",                [&]() { opt = 7; } },
        {"Portuguese (Brazil)",   [&]() { opt = 8; } },
        {"Portuguese (Portugal)", [&]() { opt = 9; } },
        {"Slovenian",             [&]() { opt = 10; }},
        {"Spanish",               [&]() { opt = 11; }},
        {"Swedish",               [&]() { opt = 12; }},
        {"Turkish",               [&]() { opt = 13; }},
    };
    addOptionToMainMenu();

    loopOptions(options, "Keyboard", opt);

    if (opt != kvxConfig.badUSBBLEKeyboardLayout) { kvxConfig.setBadUSBBLEKeyboardLayout(opt); }
}

/*********************************************************************
**  Function: setBadUSBBLEKeyDelayMenu
**  Main Menu for setting Bad USB/BLE Keyboard Key Delay
**********************************************************************/
void setBadUSBBLEKeyDelayMenu() {
    String delayStr = num_keyboard(String(kvxConfig.badUSBBLEKeyDelay), 3, "Key Delay (ms):");
    if (delayStr != "\x1B") {
        uint16_t delayVal = static_cast<uint16_t>(delayStr.toInt());
        if (delayVal <= 500) {
            kvxConfig.setBadUSBBLEKeyDelay(delayVal);
        } else if (delayVal != 0) {
            displayError("Invalid key delay value (0 to 500)", true);
        }
    }
}

/*********************************************************************
**  Function: setBadUSBBLEShowOutputMenu
**  Main Menu for setting Bad USB/BLE Show Output
**********************************************************************/
void setBadUSBBLEShowOutputMenu() {
    options.clear();
    options = {
        {"Enable",  [&]() { kvxConfig.setBadUSBBLEShowOutput(true); } },
        {"Disable", [&]() { kvxConfig.setBadUSBBLEShowOutput(false); }},
    };
    addOptionToMainMenu();

    loopOptions(options, "Show Output", kvxConfig.badUSBBLEShowOutput ? 0 : 1);
}

/*********************************************************************
**  Function: setMacAddressMenu - @IncursioHack
**  Handles Menu to configure WiFi MAC Address
**********************************************************************/
void setMacAddressMenu() {
    String currentMAC = kvxConfig.wifiMAC;
    if (currentMAC == "") currentMAC = WiFi.macAddress();

    options.clear();
    options = {
        {"Default MAC (" + WiFi.macAddress() + ")",
         [&]() { kvxConfig.setWifiMAC(""); },
         kvxConfig.wifiMAC == ""},
        {"Set Custom MAC",
         [&]() {
             String newMAC = keyboard(kvxConfig.wifiMAC, 17, "XX:YY:ZZ:AA:BB:CC");
             if (newMAC == "\x1B") return;
             if (newMAC.length() == 17) {
                 kvxConfig.setWifiMAC(newMAC);
             } else {
                 displayError("Invalid MAC format");
             }
         }, kvxConfig.wifiMAC != ""},
        {"Random MAC", [&]() {
             uint8_t randomMac[6];
             for (int i = 0; i < 6; i++) randomMac[i] = random(0x00, 0xFF);
             char buf[18];
             snprintf(
                 buf,
                 sizeof(buf),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 randomMac[0],
                 randomMac[1],
                 randomMac[2],
                 randomMac[3],
                 randomMac[4],
                 randomMac[5]
             );
             kvxConfig.setWifiMAC(String(buf));
         }}
    };

    addOptionToMainMenu();
    loopOptions(options, "MAC Address");
}

/*********************************************************************
**  Function: setSPIPins
**  Main Menu to manually set SPI Pins
**********************************************************************/
void setSPIPinsMenu(KvxputerConfigPins::SPIPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    KvxputerConfigPins::SPIPins points = value;

RELOAD:
    options = {
        {String("SCK =" + String(points.sck)).c_str(), [&]() { opt = 1; }},
        {String("MISO=" + String(points.miso)).c_str(), [&]() { opt = 2; }},
        {String("MOSI=" + String(points.mosi)).c_str(), [&]() { opt = 3; }},
        {String("CS  =" + String(points.cs)).c_str(), [&]() { opt = 4; }},
        {String("CE/GDO0=" + String(points.io0)).c_str(), [&]() { opt = 5; }},
        {String("NC/GDO2=" + String(points.io2)).c_str(), [&]() { opt = 6; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options, "SPI Pins");
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            kvxConfigPins.setSpiPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.sck + 1;
        else if (opt == 2) index = points.miso + 1;
        else if (opt == 3) index = points.mosi + 1;
        else if (opt == 4) index = points.cs + 1;
        else if (opt == 5) index = points.io0 + 1;
        else if (opt == 6) index = points.io2 + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, "GPIO", index);
        options.clear();
        if (opt == 1) points.sck = sel;
        else if (opt == 2) points.miso = sel;
        else if (opt == 3) points.mosi = sel;
        else if (opt == 4) points.cs = sel;
        else if (opt == 5) points.io0 = sel;
        else if (opt == 6) points.io2 = sel;
        changed = true;
        goto RELOAD;
    }
}

/*********************************************************************
**  Function: setUARTPins
**  Main Menu to manually set SPI Pins
**********************************************************************/
void setUARTPinsMenu(KvxputerConfigPins::UARTPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    KvxputerConfigPins::UARTPins points = value;

RELOAD:
    options = {
        {String("RX = " + String(points.rx)).c_str(), [&]() { opt = 1; }},
        {String("TX = " + String(points.tx)).c_str(), [&]() { opt = 2; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options, "UART Pins");
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            kvxConfigPins.setUARTPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.rx + 1;
        else if (opt == 2) index = points.tx + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, "GPIO", index);
        options.clear();
        if (opt == 1) points.rx = sel;
        else if (opt == 2) points.tx = sel;
        changed = true;
        goto RELOAD;
    }
}

/*********************************************************************
**  Function: setI2CPins
**  Main Menu to manually set SPI Pins
**********************************************************************/
void setI2CPinsMenu(KvxputerConfigPins::I2CPins &value) {
#if defined(SOC_HP_I2C_NUM) && SOC_HP_I2C_NUM < 2 && SYS_I2C_SDA >= 0 && SYS_I2C_SCL >= 0
    displayError("I2C Pins cannot be changed on this board", true);
    return;
#else
    uint8_t opt = 0;
    bool changed = false;
    KvxputerConfigPins::I2CPins points = value;

RELOAD:
    options = {
        {String("SDA = " + String(points.sda)).c_str(), [&]() { opt = 1; }},
        {String("SCL = " + String(points.scl)).c_str(), [&]() { opt = 2; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options, "I2C Pins");
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            kvxConfigPins.setI2CPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.sda + 1;
        else if (opt == 2) index = points.scl + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, "GPIO", index);
        options.clear();
        if (opt == 1) points.sda = sel;
        else if (opt == 2) points.scl = sel;
        changed = true;
        goto RELOAD;
    }
#endif
}

/*********************************************************************
**  Function: setTheme
**  Menu to change Theme
**********************************************************************/
void setTheme() {
    FS *fs = &LittleFS;
    options = {
        {"Little FS", [&]() { fs = &LittleFS; }},
        {"Default",
         [&]() {
             kvxConfig.removeTheme();
             kvxConfig.themePath = "";
             kvxConfig.theme.fs = 0;
             kvxConfig.secColor = DEFAULT_SECCOLOR;
             kvxConfig.bgColor = TFT_BLACK;
             kvxConfig.setUiColor(DEFAULT_PRICOLOR);
#ifdef HAS_RGB_LED
             kvxConfig.ledBright = 50;
             kvxConfig.ledColor = 0x960064;
             kvxConfig.ledEffect = 0;
             kvxConfig.ledEffectSpeed = 5;
             kvxConfig.ledEffectDirection = 1;
             ledSetup();
#endif
             kvxConfig.saveFile();
             fs = nullptr;
         }                                     },
        {"Main Menu", [&]() { fs = nullptr; }  }
    };
    if (setupSdCard()) {
        options.insert(options.begin(), {"SD Card", [&]() { fs = &SD; }});
    }
    loopOptions(options, "Theme");
    if (fs == nullptr) return;

    String filepath = loopSD(*fs, true, "JSON");
    if (kvxConfig.openThemeFile(fs, filepath, true)) {
        kvxConfig.themePath = filepath;
        if (fs == &LittleFS) kvxConfig.theme.fs = 1;
        else if (fs == &SD) kvxConfig.theme.fs = 2;
        else kvxConfig.theme.fs = 0;

        kvxConfig.saveFile();
    }
}
#if !defined(LITE_VERSION)
BLE_API bleApi;
static bool ble_api_enabled = false;

void enableBLEAPI() {
    if (!ble_api_enabled) {
        // displayWarning("BLE API require huge amount of RAM.");
        // displayWarning("Some features may stop working.");
        Serial.println(ESP.getFreeHeap());
        bleApi.setup();
        Serial.println(ESP.getFreeHeap());
    } else {
        bleApi.end();
    }

    ble_api_enabled = !ble_api_enabled;
}

bool appStoreInstalled() {
    FS *fs;
    if (!getFsStorage(fs)) {
        log_i("Fail getting filesystem");
        return false;
    }

    return fs->exists(String(kvx::paths::MENU_SCRIPTS_TOOLS) + "/App Store.js");
}

#include <HTTPClient.h>
void installAppStoreJS() {

    if (!WiFi.isConnected()) { wifiConnectMenu(WIFI_STA); }
    if (!WiFi.isConnected()) {
        displayWarning("WiFi not connected", true);
        return;
    }

    FS *fs;
    if (!getFsStorage(fs)) {
        log_i("Fail getting filesystem");
        return;
    }

    if (!fs->exists(kvx::paths::MENU_SCRIPTS)) {
        if (!fs->mkdir(kvx::paths::MENU_SCRIPTS)) {
            displayWarning("Failed to create scripts directory", true);
            return;
        }
    }

    if (!fs->exists(kvx::paths::MENU_SCRIPTS_TOOLS)) {
        if (!fs->mkdir(kvx::paths::MENU_SCRIPTS_TOOLS)) {
            displayWarning("Failed to create scripts/Tools directory", true);
            return;
        }
    }

    HTTPClient http;
    http.begin("https://ghp.iceis.co.uk/service/appstore/");
    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        displayWarning("Failed to download App Store", true);
        return;
    }

    File file = fs->open(String(kvx::paths::MENU_SCRIPTS_TOOLS) + "/App Store.js", FILE_WRITE);
    if (!file) {
        displayWarning("Failed to save App Store", true);
        return;
    }
    file.print(http.getString());
    http.end();
    file.close();

    displaySuccess("App Store installed", true);
    displaySuccess("Goto JS Interpreter -> Tools -> App Store", true);
}
#endif

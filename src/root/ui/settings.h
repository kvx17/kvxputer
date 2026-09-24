#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include "root/config/config.h"
#include "root/config/configPins.h"
#include <NTPClient.h>
#include <globals.h>

void _setBrightness(uint8_t brightval) __attribute__((weak));

void setBrightness(uint8_t brightval, bool save = true);

void getBrightness();

// Same steps as Config → Brightness / Sound Volume (shared with home-grid FN shortcuts).
uint8_t nextBrightnessValue(uint8_t current, int dir);
int brightnessSettingIndex(uint8_t bright);
int nextVolumeValue(int current, int dir);
int volumeSettingIndex(int volume);

int gsetRotation(bool set = false);

void setBrightnessMenu();

void setUIColor();

void setAccentColor();

bool setCustomUIColorMenu();

void setCustomUIColorChoiceMenu(int colorType);

void setCustomUIColorSettingMenuR(int colorType);

void setCustomUIColorSettingMenuG(int colorType);

void setCustomUIColorSettingMenuB(int colorType);

void setCustomUIColorSettingMenu(
    int colorType, int rgb, std::function<uint16_t(uint16_t, int)> colorGenerator
);

void addEvilWifiMenu();

void removeEvilWifiMenu();

void setEvilEndpointCreds();

void setEvilEndpointSsid();

void setEvilAllowEndpointDisplay();

void setEvilAllowGetCreds();

void setEvilAllowSetSsid();

void setEvilPasswordMode();

void setEvilGatewayIp();

void setRFModuleMenu();

void setRFFreqMenu();

void setRFIDModuleMenu();

void addMifareKeyMenu();

void setSleepMode();

void setDimmerTimeMenu();

void setClock();

void runClockLoop(bool showMenuHint = false);

int gsetIrTxPin(bool set = false);

void setIrTxRepeats();

int gsetIrRxPin(bool set = false);

int gsetRfTxPin(bool set = false);

int gsetRfRxPin(bool set = false);

void setSoundConfig();

void setSoundVolume();

#ifdef HAS_RGB_LED
void setLedBlinkConfig();
#endif

void setWifiStartupConfig();

void setStartupApp();

void setGpsBaudrateMenu();

void setNetworkCredsMenu();

void setBadUSBBLEMenu();
void setBadUSBBLEKeyboardLayoutMenu();
void setBadUSBBLEKeyDelayMenu();
void setBadUSBBLEShowOutputMenu();

void setSPIPinsMenu(KvxputerConfigPins::SPIPins &value);

void setUARTPinsMenu(KvxputerConfigPins::UARTPins &value);

void setI2CPinsMenu(KvxputerConfigPins::I2CPins &value);

void setTheme();

void setMacAddressMenu();

#if !defined(LITE_VERSION)
void enableBLEAPI();

bool appStoreInstalled();

void installAppStoreJS();
#endif

#endif

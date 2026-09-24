#ifndef __UTILS_H__
#define __UTILS_H__
#include <Arduino.h>
#include <Wire.h>
void backToMenu();
void addOptionToMainMenu();
int getBattery() __attribute__((weak));
int getBatteryMilliVolts();
int getBatteryTrendMilliVoltsPerMin();
bool isUsbCablePresent();

enum ChargeState : uint8_t {
    CHARGE_BATTERY = 0,
    CHARGE_USB,
    CHARGE_CHARGING,
    CHARGE_FULL
};

struct ChargeInfo {
    ChargeState state = CHARGE_BATTERY;
    int percent = 0;
    int milliVolts = 0;
    int trendMvPerMin = 0;
    bool usb = false;
    bool estimated = true;
    bool chargeSwitchOff = false; // Cardputer flip switch: GPIO10 seeing USB rail
#ifdef USE_BQ27220_VIA_I2C
    int remainMah = 0;
    int fullMah = 0;
    int designMah = 0;
    int currentMa = 0;
    int avgPowerMw = 0;
    int timeToEmptyMin = 0;
#endif
};

ChargeInfo readChargeInfo();
const char *chargeStateLabel(ChargeState state);
bool updateClockTimezone();
#if !defined(HAS_RTC)
void restorePersistedClock();
#endif
void updateTimeStr(struct tm timeInfo);
void showDeviceInfo();
String formatTimeDecimal(uint32_t totalMillis);
String getOptionsJSON();
void touchHeatMap(struct TouchPoint t);
void i2c_bulk_write(TwoWire *wire, uint8_t addr, const uint8_t *bulk_data);
void printMemoryUsage(const char *msg = "");
String repeatString(int length, String character);
String formatBytes(uint64_t bytes);
#endif

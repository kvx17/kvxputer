#include "root/app/utils.h"
#include "root/net/wifi_common.h" //to return MAC addr
#include "root/ui/scrollableTextArea.h"
#include <Preferences.h>
#include <globals.h>
#if defined(SOC_USB_SERIAL_JTAG_SUPPORTED)
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "driver/usb_serial_jtag.h"
#define KVX_HAS_USB_JTAG_CONN 1
#endif
#endif
#if defined(USB_as_HID) && __has_include("tusb.h")
#include "tusb.h"
#define KVX_HAS_TUD_CONN 1
#endif

/*********************************************************************
**  Function: backToMenu
**  sets the global var to be be used in the options second parameter
**  and returnToMenu will be user do handle the breaks of all loops

when using loopfunctions with an option to "Back to Menu", use:

add this option:
    options.push_back({"Main Menu", [=]() { backToMenu(); }});

while(1) {
    if(returnToMenu) break; // stop this loop and return to the previous loop

    ...
    loopOptions(options);
    ...
}
*/

void backToMenu() { returnToMenu = true; }

void addOptionToMainMenu() {
    if (!forceHome) returnToMenu = false;
    options.push_back({"Main Menu", backToMenu});
}

#ifndef ANALOG_BAT_MULTIPLIER
#define ANALOG_BAT_MULTIPLIER 2.0f
#endif

static const float kBatMinMv = 3300.0f;
static const float kBatMaxMv = 4150.0f;
static const int kBatHist = 8;

static int gLastBatMv = 0;
static int gBatHistMv[kBatHist];
static unsigned long gBatHistMs[kBatHist];
static int gBatHistCount = 0;
static int gBatHistHead = 0;
static uint8_t gFullStreak = 0;
static ChargeState gLastChargeState = CHARGE_BATTERY;
static volatile bool gBatReading = false;
static int gShownPercent = 50;
static bool gRailLatch = false;

static void noteBatterySample(int mv) {
    unsigned long now = millis();
    if (gBatHistCount > 0) {
        int last = (gBatHistHead + kBatHist - 1) % kBatHist;
        if (now - gBatHistMs[last] < 800) return;
    }
    gBatHistMv[gBatHistHead] = mv;
    gBatHistMs[gBatHistHead] = now;
    gBatHistHead = (gBatHistHead + 1) % kBatHist;
    if (gBatHistCount < kBatHist) gBatHistCount++;
}

static int milliVoltsToPercent(int mv) {
    float percent = ((float)mv - kBatMinMv) / (kBatMaxMv - (kBatMinMv + 50.0f)) * 100.0f;
    if (percent < 1.0f) return 1;
    if (percent > 100.0f) return 100;
    return (int)percent;
}

bool isUsbCablePresent() {
#ifdef KVX_HAS_USB_JTAG_CONN
    if (usb_serial_jtag_is_connected()) return true;
#endif
#ifdef KVX_HAS_TUD_CONN
    if (tud_inited() && tud_connected()) return true;
#endif
    return false;
}

int getBatteryMilliVolts() {
#ifdef USE_BQ27220_VIA_I2C
    gLastBatMv = (int)bq.getVolt(VOLT_MODE::VOLT);
    if (gLastBatMv < 0) gLastBatMv = 0;
    noteBatterySample(gLastBatMv);
    return gLastBatMv;
#endif
#ifdef ANALOG_BAT_PIN
    if (gBatReading) return gLastBatMv > 0 ? gLastBatMv : 3700;
    gBatReading = true;
    static bool adcInitialized = false;
    static int lastGoodMv = 0;
    if (!adcInitialized) {
        pinMode(ANALOG_BAT_PIN, INPUT);
        adcInitialized = true;
    }
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) sum += analogReadMilliVolts(ANALOG_BAT_PIN);
    int raw = (int)((float)(sum / 4) * ANALOG_BAT_MULTIPLIER);

    // Charge switch OFF: GPIO10 sees the 5 V USB rail (~4.1–6.6 V after the divider),
    // not the pack. Latch until a mid-range pack reading comes back.
    if (raw > 4200) gRailLatch = true;
    else if (lastGoodMv > 0 && lastGoodMv < 3980 && raw >= 4050) gRailLatch = true;
    else if (gRailLatch && raw >= 2800 && raw < 4000) gRailLatch = false;

    int mv = raw;
    if (gRailLatch) {
        mv = lastGoodMv > 0 ? lastGoodMv : 3700;
    } else if (raw > 4200 || raw < 2800) {
        mv = lastGoodMv > 0 ? lastGoodMv : 3700;
    } else if (lastGoodMv > 0 && abs(raw - lastGoodMv) > 200) {
        mv = lastGoodMv + (raw > lastGoodMv ? 40 : -40);
        lastGoodMv = mv;
    } else {
        lastGoodMv = mv;
    }
    gLastBatMv = mv;
    noteBatterySample(gLastBatMv);
    gBatReading = false;
    return gLastBatMv;
#endif
    gLastBatMv = 0;
    return 0;
}

int getBatteryTrendMilliVoltsPerMin() {
    if (gBatHistCount < 2) return 0;
    int oldest = (gBatHistCount < kBatHist) ? 0 : gBatHistHead;
    int newest = (gBatHistHead + kBatHist - 1) % kBatHist;
    long dt = (long)(gBatHistMs[newest] - gBatHistMs[oldest]);
    if (dt < 1000) return 0;
    int dv = gBatHistMv[newest] - gBatHistMv[oldest];
    return (int)((long)dv * 60000L / dt);
}

const char *chargeStateLabel(ChargeState state) {
    switch (state) {
        case CHARGE_USB: return "On USB";
        case CHARGE_CHARGING: return "Charging";
        case CHARGE_FULL: return "Full";
        case CHARGE_BATTERY:
        default: return "Battery";
    }
}

ChargeInfo readChargeInfo() {
    ChargeInfo info;
    info.milliVolts = getBatteryMilliVolts();
    info.trendMvPerMin = getBatteryTrendMilliVoltsPerMin();
    info.usb = isUsbCablePresent();

#ifdef USE_BQ27220_VIA_I2C
    info.estimated = false;
    float pct = bq.getChargePcnt();
    if (pct <= 0.0f) info.percent = 1;
    else if (pct > 100.0f) info.percent = 100;
    else info.percent = (int)pct;
    info.remainMah = (int)bq.getRemainCap();
    info.fullMah = (int)bq.getFullChargeCap();
    info.designMah = (int)bq.getDesignCap();
    info.currentMa = (int)bq.getCurr(CURR_MODE::CURR_AVERAGE);
    info.avgPowerMw = (int)bq.getAvgPower();
    info.timeToEmptyMin = (int)bq.getTimeToEmpty();
    bool chg = bq.getIsCharging();
    if (info.percent >= 99 && !chg) info.state = CHARGE_FULL;
    else if (chg) info.state = CHARGE_CHARGING;
    else if (info.usb) info.state = CHARGE_USB;
    else info.state = CHARGE_BATTERY;
#else
    info.estimated = true;
    info.percent = milliVoltsToPercent(info.milliVolts);
    bool rising = info.trendMvPerMin >= 25;
    bool holdingHigh = info.milliVolts >= 4000 && info.milliVolts <= 4200 && info.trendMvPerMin > -25;
    if (info.percent >= 99 && info.milliVolts >= 4120 && info.milliVolts <= 4200 &&
        (gLastChargeState == CHARGE_CHARGING || gLastChargeState == CHARGE_FULL)) {
        if (gFullStreak < 10) gFullStreak++;
    } else {
        gFullStreak = 0;
    }
    bool full = gFullStreak >= 3 && !gRailLatch;

    if (gRailLatch) {
        info.percent = gShownPercent;
        if (info.percent > 99) info.percent = 99;
        info.state = info.usb ? CHARGE_USB : CHARGE_BATTERY;
        gFullStreak = 0;
        gLastChargeState = info.state;
        return info;
    }

    if (full) {
        info.state = CHARGE_FULL;
    } else if (info.usb) {
        if (rising) info.state = CHARGE_CHARGING;
        else info.state = CHARGE_USB;
    } else if (rising) {
        info.state = CHARGE_CHARGING;
    } else if (gLastChargeState == CHARGE_CHARGING && holdingHigh && info.trendMvPerMin > -40) {
        info.state = CHARGE_CHARGING;
    } else {
        info.state = CHARGE_BATTERY;
    }

    gShownPercent = info.percent;
    if (info.state != CHARGE_FULL && info.percent > 99) info.percent = 99;
#endif
    gLastChargeState = info.state;
    return info;
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Returns the battery value from 1-100
***************************************************************************************/
int getBattery() {
#ifdef USE_BQ27220_VIA_I2C
    float pct = bq.getChargePcnt();
    if (pct <= 0.0f) return 1;
    if (pct > 100.0f) return 100;
    return (int)pct;
#endif
#ifdef ANALOG_BAT_PIN
    return milliVoltsToPercent(getBatteryMilliVolts());
#endif
    return 0;
}

void updateClockTimezone() {
    timeClient.begin();
    timeClient.update();

    timeClient.setTimeOffset(kvxConfig.tmz * 3600);

    localTime = timeClient.getEpochTime() + (kvxConfig.dst ? 3600 : 0);

#if defined(HAS_RTC)
    struct tm *timeinfo = localtime(&localTime);
    RTC_TimeTypeDef TimeStruct;
    TimeStruct.Hours = timeinfo->tm_hour;
    TimeStruct.Minutes = timeinfo->tm_min;
    TimeStruct.Seconds = timeinfo->tm_sec;
    _rtc.SetTime(&TimeStruct);
    updateTimeStr(_rtc.getTimeStruct());
#else
    rtc.setTime(localTime);
    updateTimeStr(rtc.getTimeStruct());
    clock_set = true;
#endif
    // Update Internal clock to system time
    struct timeval tv = {.tv_sec = localTime};
    settimeofday(&tv, nullptr);
}

#if !defined(HAS_RTC)
// --- Persistent software clock (boards without an RTC chip) ---
// Boards without an RTC lose a set time on a full power-off. Periodically save
// the current epoch to NVS and restore it on boot, so the clock comes back
// close to correct (it re-syncs exactly via NTP/GPS when available). NVS (not
// the user filesystem) so it survives FS reformats and custom partition layouts.
#define CLOCK_PERSIST_NS "clock"
#define CLOCK_PERSIST_KEY "epoch"
#define CLOCK_PERSIST_INTERVAL_MS 300000 // 5 min: bounds NVS wear, <=5 min drift after power loss

static void time_persist_task(void *param) {
    Preferences prefs;
    uint32_t last_write_ms = 0;
    bool saved = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // wake every minute
        if (!clock_set) continue;
        // Save promptly the first time the clock is set, then at most every 5 min.
        if (saved && (millis() - last_write_ms) < CLOCK_PERSIST_INTERVAL_MS) continue;
        if (prefs.begin(CLOCK_PERSIST_NS, false)) {
            prefs.putULong(CLOCK_PERSIST_KEY, (uint32_t)rtc.getEpoch());
            prefs.end();
            last_write_ms = millis();
            saved = true;
        }
    }
}

// Restore the last-saved time from NVS (if plausible) and start the periodic
// save task. Call once from init_clock() on boards without an RTC.
void restorePersistedClock() {
    Preferences prefs;
    if (prefs.begin(CLOCK_PERSIST_NS, true)) { // read-only
        uint32_t epoch = prefs.getULong(CLOCK_PERSIST_KEY, 0);
        prefs.end();
        if (epoch > 1735689600UL) { // sanity: only restore a plausible time (after 2025-01-01)
            rtc.setTime((unsigned long)epoch);
            clock_set = true;
            struct timeval tv = {.tv_sec = (time_t)epoch};
            settimeofday(&tv, nullptr);
        }
    }
    xTaskCreate(time_persist_task, "clockSave", 4096, NULL, 1, NULL);
}
#endif

void updateTimeStr(struct tm timeInfo) {
    if (kvxConfig.clock24hr) {
        // Use 24 hour format
        snprintf(
            timeStr, sizeof(timeStr), "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec
        );
    } else {
        // Use 12 hour format with AM/PM
        int hour12 = (timeInfo.tm_hour == 0)   ? 12
                     : (timeInfo.tm_hour > 12) ? timeInfo.tm_hour - 12
                                               : timeInfo.tm_hour;
        const char *ampm = (timeInfo.tm_hour < 12) ? "AM" : "PM";

        snprintf(
            timeStr, sizeof(timeStr), "%02d:%02d:%02d %s", hour12, timeInfo.tm_min, timeInfo.tm_sec, ampm
        );
    }
}

void showDeviceInfo() {
    ScrollableTextArea area = ScrollableTextArea("DEVICE INFO");

    area.addLine("kvxputer Version: " + String(KVXPUTER_VERSION));
    area.addLine("EEPROM size: " + String(EEPROMSIZE));
    area.addLine("");
    area.addLine("[MEMORY]");
    area.addLine("Total heap: " + formatBytes(ESP.getHeapSize()));
    area.addLine("Free heap: " + formatBytes(ESP.getFreeHeap()));
    if (psramFound()) {
        area.addLine("Total PSRAM: " + formatBytes(ESP.getPsramSize()));
        area.addLine("Free PSRAM: " + formatBytes(ESP.getFreePsram()));
    }
    area.addLine("");
    area.addLine("[NETWORK]");
    area.addLine("MAC addr: " + String(WiFi.macAddress()));
    String localIP = WiFi.localIP().toString();
    String softAPIP = WiFi.softAPIP().toString();
    String ipStatus = (WiFi.isConnected()) ? (localIP != "0.0.0.0"    ? localIP
                                              : softAPIP != "0.0.0.0" ? softAPIP
                                                                      : "No valid IP")
                                           : "Not connected";
    area.addLine("IP address: " + ipStatus);
    area.addLine("");
    area.addLine("[STORAGE]");
    area.addLine("LittleFS total: " + formatBytes(LittleFS.totalBytes()));
    area.addLine("LittleFS used: " + formatBytes(LittleFS.usedBytes()));
    area.addLine("LittleFS free: " + formatBytes(LittleFS.totalBytes() - LittleFS.usedBytes()));
    area.addLine("");
    area.addLine("SD Card total: " + formatBytes(SD.totalBytes()));
    area.addLine("SD Card used: " + formatBytes(SD.usedBytes()));
    area.addLine("SD Card free: " + formatBytes(SD.totalBytes() - SD.usedBytes()));
    area.addLine("");

#ifdef HAS_SCREEN
    area.addLine("[SCREEN]");
    area.addLine("Rotation: " + String(ROTATION));
    area.addLine("Width: " + String(tftWidth) + "px");
    area.addLine("Height: " + String(tftHeight) + "px");
    area.addLine("Brightness: " + String(kvxConfig.bright) + "%");
    area.addLine("");
#endif

    area.addLine("[GPIO]");
    area.addLine("GROVE_SDA: " + String(kvxConfigPins.i2c_bus.sda));
    area.addLine("GROVE_SCL: " + String(kvxConfigPins.i2c_bus.scl));
    area.addLine("SYS_I2C_SDA: " + String(kvxConfigPins.sys_i2c.sda));
    area.addLine("SYS_I2C_SCL: " + String(kvxConfigPins.sys_i2c.scl));
    area.addLine("SERIAL TX: " + String(kvxConfigPins.uart_bus.tx));
    area.addLine("SERIAL RX: " + String(kvxConfigPins.uart_bus.rx));
    area.addLine("SPI_SCK_PIN: " + String(SPI_SCK_PIN));
    area.addLine("SPI_MOSI_PIN: " + String(SPI_MOSI_PIN));
    area.addLine("SPI_MISO_PIN: " + String(SPI_MISO_PIN));
    area.addLine("SPI_SS_PIN: " + String(SPI_SS_PIN));
    area.addLine("IR TX: " + String(TXLED));
    area.addLine("IR RX: " + String(RXLED));
    area.addLine("");

    area.addLine("[BAT]");
    area.addLine("Charge: " + String(getBattery()) + "%");
#ifdef USE_BQ27220_VIA_I2C
    area.addLine("BQ27220 ADDR: " + String(BQ27220_I2C_ADDRESS));
    area.addLine("Curr Capacity: " + String(bq.getRemainCap()) + "mAh");
    area.addLine("Full Capacity: " + String(bq.getFullChargeCap()) + "mAh");
    area.addLine("Design Capacity: " + String(bq.getDesignCap()) + "mAh");
    area.addLine("Charging: " + String(bq.getIsCharging()));
    area.addLine(
        "Charging Voltage: " + String(((double)bq.getVolt(VOLT_MODE::VOLT_CHARGING) / 1000.0)) + "V"
    );
    area.addLine("Charging Current: " + String(bq.getCurr(CURR_MODE::CURR_CHARGING)) + "mA");
    area.addLine(
        "Time to Empty: " + String((bq.getTimeToEmpty() / 1440)) + " days " +
        String(((bq.getTimeToEmpty() % 1440) / 60)) + " hrs " + String(((bq.getTimeToEmpty() % 1440) % 60)) +
        " mins"
    );
    area.addLine("Avg Power Use: " + String(bq.getAvgPower()) + "mW");
    area.addLine("Voltage: " + String(((double)bq.getVolt(VOLT_MODE::VOLT) / 1000.0)) + "V");
    area.addLine("Raw Voltage: " + String(bq.getVolt(VOLT_MODE::VOLT_RWA)) + "mV");
    area.addLine("Curr Current: " + String(bq.getCurr(CURR_INSTANT)) + "mA");
    area.addLine("Avg Current: " + String(bq.getCurr(CURR_MODE::CURR_AVERAGE)) + "mA");
    area.addLine("Raw Current: " + String(bq.getCurr(CURR_MODE::CURR_RAW)) + "mA");
#endif

    area.show();
}

#if defined(HAS_TOUCH)
/*********************************************************************
** Function: touchHeatMap
** Touchscreen Mapping, include this function after reading the touchPoint
**********************************************************************/
void touchHeatMap(struct TouchPoint t) {
    int third_x = tftWidth / 3;
    int third_y = tftHeight / 3;

    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y > third_y) PrevPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && ((t.y > third_y && t.y < third_y * 2) || t.y > tftHeight))
        SelPress = true;
    if (t.x > third_x * 2 && t.x < third_x * 3) NextPress = true;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y < third_y) EscPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y < third_y) UpPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y > third_y * 2 && t.y < third_y * 3) DownPress = true;
    /*
                        Touch area Map
                ________________________________ 0
                |   Esc   |   UP    |         |
                |_________|_________|         |_> third_y
                |         |   Sel   |         |
                |         |_________|  Next   |_> third_y*2
                |  Prev   |  Down   |         |
                |_________|_________|_________|_> third_y*3
                |__Prev___|___Sel___|__Next___| 20 pixel touch area where the touchFooter is drawn
                0         L third_x |         |
                                    Lthird_x*2|
                                              Lthird_x*3
    */
}

#endif

String getOptionsJSON() {
    String menutype = "regular_menu";
    if (menuOptionType == 0) menutype = "main_menu";
    else if (menuOptionType == 1) menutype = "sub_menu";

    String response = "{\"width\":" + String(tftWidth) + ", \"height\":" + String(tftHeight) +
                      ",\"menu\":\"" + menutype + "\",\"menu_title\":\"" + menuOptionLabel +
                      "\", \"options\":[";
    int i = 0;
    int sel = 0;
    for (auto opt : options) {
        response += "{\"n\":" + String(i) + ",\"label\":\"" + opt.label + "\"}";
        if (opt.hovered) sel = i;
        i++;
        if (i < options.size()) response += ",";
    }
    response += "], \"active\":" + String(sel) + "}";
    return response;
}

/*********************************************************************
** Function: i2c_bulk_write
** Sends múltiple registers via I2C using a compact table.
   bulk_data example..
   const uint8_t bulk_data[] = {
      2, 0x00, 0x00,       // <- datalen = 2, reg = 0x00, data = 0x00
      3, 0x01, 0x00, 0x02, // <- datalen = 3, reg = 0x01, data = 0x00, 0x02
      0 };                 // <- datalen 0 is end of data.
**********************************************************************/
void i2c_bulk_write(TwoWire *wire, uint8_t addr, const uint8_t *bulk_data) {
    const uint8_t *p = bulk_data;
    while (true) {
        uint8_t datalen = *p++;
        if (datalen == 0) { break; } // --- end of table ---
        uint8_t reg = *p++;
        wire->beginTransmission(addr);
        wire->write(reg);
        for (uint8_t i = 0; i < datalen - 1; i++) { wire->write(*p++); }
        uint8_t error = wire->endTransmission();
        if (error != 0) { log_e("I2C Write error %d", error); }
        delay(1);
    }
}

String formatTimeDecimal(uint32_t totalMillis) {
    uint16_t minutes = totalMillis / 60000;
    float seconds = (totalMillis % 60000) / 1000.0;

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%06.3f", minutes, seconds);
    return String(buffer);
}

void printMemoryUsage(const char *msg) {
    Serial.printf(
        "%s:\nPSRAM: [Free: %lu, max alloc: %lu],\nRAM: [Free: %lu, "
        "max alloc: %lu]\n\n",
        msg,
        ESP.getFreePsram(),
        ESP.getMaxAllocPsram(),
        ESP.getFreeHeap(),
        ESP.getMaxAllocHeap()
    );
}

String repeatString(int length, String character) {
    String result = "";
    for (int i = 0; i < length; i++) { result += character; }
    return result;
}

String formatBytes(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    float size = bytes;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    if (unitIndex == 0) {
        return String(bytes) + " " + units[unitIndex];
    } else {
        return String(size, 2) + " " + units[unitIndex];
    }
}

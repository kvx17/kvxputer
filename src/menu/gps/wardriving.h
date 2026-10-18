/**
 * @file wardriving.h
 * @brief WiFi / BLE wardriving session (WD WiFi, WD BLE, WD Both)
 */

#ifndef __WAR_DRIVING_H__
#define __WAR_DRIVING_H__

#include "menu/gps/wd/wd_config.h"
#include "menu/gps/wd/wd_filter.h"
#include "menu/gps/wd/wd_gps.h"
#include "menu/gps/wd/wd_logger.h"
#include "menu/gps/wd/wd_scanner.h"
#include "menu/gps/wd/wd_ui.h"
#include "menu/ble/ble_common.h"
#include <globals.h>
#include <set>

class Wardriving {
public:
    Wardriving(bool scanWiFi = false, bool scanBLE = false);
    ~Wardriving();

    void setup();
    void loop();

    // GPS menu: edit wardriverconfig.txt scalars
    static void configMenu();

private:
    bool wantWifi = false;
    bool wantBle = false;
    bool bleInitialized = false;

    WdConfig cfg;
    WdGps gps;
    WdScanner scanner;
    WdFilter filter;
    WdLogger logger;
    WdUi ui;
    WdSessionStats stats;

    std::set<String> alertMACs;
    FS *fs = nullptr;

    double lastLat = 0;
    double lastLng = 0;
    bool hadPosition = false;
    uint32_t lastScanMs = 0;
    uint32_t lastStatsMs = 0;
    uint32_t lastGeofenceBeepMs = 0;
    uint32_t sweepIndex = 0;
    bool sessionEnded = false;

    void endSession();
    void loadAlertMACs();
    void maybeAutoUpload();
    void runSweep();
    void scanBle(const WdGpsFix &fix, bool skipGeofence);
    void handleKeys();
    void updateHud();
    void checkBattery();
    void writeStatsRow(const char *event);
    void checkForAlert(const String &mac, const String &type, const String &name);
    void beep(unsigned freq, unsigned ms);
    static bool parseMacToU64(const String &mac, uint64_t &out);
};

void wardrivingConfigMenu();
void wardrivingViewCaptures();

#endif

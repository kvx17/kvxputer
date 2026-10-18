/**
 * @file wardriving.cpp
 * @brief WiFi / BLE wardriving with GPS QoL ported from cardputer-wardriver patterns
 */

#include "wardriving.h"
#include "menu/gps/wigle.h"
#include "menu/others/audio.h"
#include "root/app/utils.h"
#include "root/hal/ble/ble_backend.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/settings.h"
#include <cctype>
#include <cstring>
#include <esp_wifi.h>

namespace {
constexpr const char *OUT_DIR = kvx::paths::GPS_WDBOTH;

void countAuth(WdSessionStats &st, wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: st.openCount++; break;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA2_ENTERPRISE: st.wpa2Count++; break;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK: st.wpa3Count++; break;
        default: st.otherSec++; break;
    }
}
} // namespace

bool Wardriving::parseMacToU64(const String &mac, uint64_t &out) {
    uint64_t value = 0;
    int nibbles = 0;
    for (size_t i = 0; i < mac.length(); i++) {
        char c = mac[i];
        if (c == ':' || c == '-') continue;
        if (!isxdigit(static_cast<unsigned char>(c))) return false;
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<uint64_t>(c - 'A' + 10);
        else return false;
        nibbles++;
        if (nibbles > 12) return false;
    }
    if (nibbles != 12) return false;
    out = value;
    return true;
}

Wardriving::Wardriving(bool scanWiFi, bool scanBLE) {
    wantWifi = scanWiFi;
    wantBle = scanBLE;
    setup();
}

Wardriving::~Wardriving() {
    if (!sessionEnded) endSession();
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#ifdef USE_BOOST
    PPM.disableOTG();
#endif
}

void Wardriving::beep(unsigned freq, unsigned ms) {
    if (stats.muted) return;
    _tone(freq, ms);
}

void Wardriving::setup() {
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#ifdef USE_BOOST
    PPM.enableOTG();
#endif
    stats = WdSessionStats{};
    stats.startMs = millis();
    ui.reset();

    drawMainBorderWithTitle(wantWifi && wantBle ? "WD Both" : (wantWifi ? "WD WiFi" : "WD BLE"));
    displayTextLine("Initializing...");

    setupSdCard();
    if (!getFsStorage(fs) || !fs) {
        displayError("Storage setup error", true);
        returnToMenu = true;
        sessionEnded = true;
        return;
    }
    kvx::paths::ensureDir(*fs, OUT_DIR);
    wd_config::load(*fs, OUT_DIR, cfg);
    gps.applyConfig(cfg);
    scanner.applyConfig(cfg);
    filter.load(cfg);

    loadAlertMACs();
    maybeAutoUpload();

    if (wantWifi) scanner.beginRadio();
    else {
        WiFi.mode(WIFI_OFF);
    }

    if (!gps.begin()) {
        displayError("GPS UART fail", true);
        endSession();
        return;
    }
    gpsConnected = true;

    displayTextLine("Waiting GPS sentences...");
    uint32_t t0 = millis();
    while (gps.msSinceSentence() == 0xFFFFFFFF && millis() - t0 < 20000) {
        gps.pump();
        if (check(EscPress)) {
            endSession();
            return;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    if (!logger.openSession(*fs, OUT_DIR)) {
        displayError("CSV path failed", true);
        endSession();
        return;
    }
    // File is created lazily on the first logged row (avoids empty header-only CSVs).
    displaySuccess(
        String(fs == &SD ? "SD " : "LFS ") + logger.filename() + "\n(on first AP)", true
    );

    lastScanMs = 0;
    lastStatsMs = millis();
    writeStatsRow("init");
    loop();
}

void Wardriving::maybeAutoUpload() {
    if (!cfg.auto_upload_on_launch || cfg.upload_ssid.isEmpty()) return;
    if (kvxConfig.wigleBasicToken.isEmpty()) return;

    displayTextLine("Auto-upload check...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    int n = WiFi.scanNetworks(false, true, false, 300, 0);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == cfg.upload_ssid) {
            found = true;
            break;
        }
    }
    WiFi.scanDelete();
    if (!found) {
        displayTextLine("Upload SSID not seen");
        vTaskDelay(800 / portTICK_PERIOD_MS);
        return;
    }
    if (check(EscPress)) return;

    wifiDisconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.upload_ssid.c_str(), cfg.upload_password.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        if (check(EscPress)) {
            wifiDisconnect();
            return;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    if (WiFi.status() != WL_CONNECTED) {
        displayError("Upload WiFi fail", true);
        wifiDisconnect();
        return;
    }
    wifiConnected = true;
    Wigle wigle;
    wigle.upload_folder(
        fs,
        OUT_DIR,
        cfg.delete_after_upload,
        cfg.compress,
        cfg.min_sweeps
    );
    wifiDisconnect();
}

void Wardriving::loadAlertMACs() {
    alertMACs.clear();
    String alertPath = String(OUT_DIR) + "/alert.txt";
    if (fs->exists(alertPath)) {
        File alertFile = fs->open(alertPath, FILE_READ);
        if (alertFile) {
            while (alertFile.available()) {
                String line = alertFile.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && !line.startsWith("#")) {
                    line.toLowerCase();
                    alertMACs.insert(line);
                }
            }
            alertFile.close();
        }
    } else {
        File alertFile = fs->open(alertPath, FILE_WRITE);
        if (alertFile) {
            alertFile.println("# Alert MAC addresses - one per line");
            alertFile.println("# aa:bb:cc:dd:ee:ff");
            alertFile.close();
        }
    }
}

void Wardriving::checkForAlert(const String &mac, const String &type, const String &name) {
    String macLower = mac;
    macLower.toLowerCase();
    if (alertMACs.find(macLower) == alertMACs.end()) return;
    stats.alerts++;
    String msg = "ALERT: " + type + " " + mac;
    if (name.length()) msg += " " + name;
    displayError(msg.c_str());
    beep(3000, 80);
    vTaskDelay(1200 / portTICK_PERIOD_MS);
}

void Wardriving::writeStatsRow(const char *event) {
    if (!cfg.stats_enabled || !fs) return;
    String dir = String(OUT_DIR) + "/logs";
    kvx::paths::ensureDir(*fs, dir.c_str());
    String path = dir + "/stats.csv";
    bool neu = !fs->exists(path);
    File f = fs->open(path, FILE_APPEND);
    if (!f) return;
    if (neu) {
        f.println("event,ms,uptime_s,heap,wifi_log,ble_log,sweeps,unique,bat");
    }
    f.printf(
        "%s,%lu,%lu,%u,%lu,%lu,%lu,%u,%d\n",
        event,
        (unsigned long)millis(),
        (unsigned long)((millis() - stats.startMs) / 1000),
        (unsigned)ESP.getFreeHeap(),
        (unsigned long)stats.wifiLogged,
        (unsigned long)stats.bleLogged,
        (unsigned long)logger.sweeps(),
        (unsigned)logger.uniqueCount(),
        getBattery()
    );
    f.close();
}

void Wardriving::checkBattery() {
    if (cfg.low_battery_pct <= 0) return;
    int bat = getBattery();
    if (bat > 0 && bat <= cfg.low_battery_pct) {
        displayError("Low battery — exiting", true);
        endSession();
    }
}

void Wardriving::endSession() {
    if (sessionEnded) return;
    sessionEnded = true;
    writeStatsRow("exit");
    logger.flush();
    const uint32_t rowsDone = logger.rowsWritten();
    const uint32_t sweepsDone = logger.sweeps();
    const size_t uniqueDone = logger.uniqueCount();
    logger.close();
    if (wantBle) {
#if !defined(KVX_BLE_BACKEND_HOSTED)
        if (bleInitialized) {
            BLEDevice::deinit(true);
            pBLEScan = nullptr;
            bleInitialized = false;
        }
#else
        bleBackend().deinit();
#endif
    }
    if (wantWifi) {
        scanner.restoreRadio();
        wifiDisconnect();
    }
    gps.end();
    gpsConnected = false;
    if (stats.displayBlank) setBrightness(kvxConfig.bright, false);

    drawMainBorderWithTitle("WD Summary");
    padprintln("Rows: " + String(rowsDone));
    padprintln("WiFi: " + String(stats.wifiLogged) + " BLE: " + String(stats.bleLogged));
    padprintln("Unique: " + String(uniqueDone));
    padprintln("Sweeps: " + String(sweepsDone));
    padprintf("Dist: %.2f km\n", stats.distance_m / 1000.0);
    padprintln("U=upload  any=exit");
    uint32_t t0 = millis();
    while (millis() - t0 < 8000) {
        char c = checkLetterShortcutPress();
        if (c == 'u' || c == 'U') {
            Wigle wigle;
            if (wifiConnectMenu()) {
                wigle.upload_folder(fs, OUT_DIR, cfg.delete_after_upload, cfg.compress, cfg.min_sweeps);
            }
            break;
        }
        if (check(EscPress) || check(SelPress) || check(AnyKeyPress)) break;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    returnToMenu = true;
}

void Wardriving::handleKeys() {
    if (check(SelPress)) {
        ui.nextView();
        return;
    }
    if (check(UpPress) || check(PrevPress)) {
        if (ui.view() == WdDashView::Channels) ui.nextChannelMode();
        return;
    }
    if (check(DownPress) || check(NextPress)) {
        if (ui.view() == WdDashView::Channels) ui.nextChannelMode();
        return;
    }

    char c = checkLetterShortcutPress();
    if (c == (char)-1) return;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

    switch (c) {
        case 'h': ui.showHelp(); break;
        case 'b':
            stats.displayBlank = !stats.displayBlank;
            setBrightness(stats.displayBlank ? 0 : kvxConfig.bright, false);
            break;
        case 's':
            stats.muted = !stats.muted;
            beep(2000, 40);
            break;
        case 'p':
            stats.loggingPaused = !stats.loggingPaused;
            logger.flush();
            beep(stats.loggingPaused ? 1200 : 2200, 40);
            break;
        case 'x':
            if (!wantWifi) break;
            if (scanner.wifiStopped()) {
                scanner.startWifi();
                stats.wifiStopped = false;
                logger.close();
                logger.openSession(*fs, OUT_DIR);
                beep(2400, 50);
            } else {
                scanner.stopWifi();
                stats.wifiStopped = true;
                logger.flush();
                beep(900, 50);
            }
            break;
        case 'f': {
            WdGpsFix fix;
            if (!gps.resolveLogPosition(fix) || stats.loggingPaused) break;
            if (logger.writeFlag(fix)) {
                stats.flags++;
                beep(2800, 60);
            }
            break;
        }
        case 'q': endSession(); break;
        case ',':
        case '.':
            if (ui.view() == WdDashView::Channels) ui.nextChannelMode();
            break;
        default: break;
    }
}

void Wardriving::updateHud() {
    if (stats.displayBlank) return;
    const char *title = "WD Both";
    if (wantWifi && !wantBle) title = "WD WiFi";
    else if (!wantWifi && wantBle) title = "WD BLE";

    WdGpsFix live = gps.getFix();
    WdGpsFix logFix;
    bool canLog = gps.resolveLogPosition(logFix);
    stats.gpsModeLabel = cfg.gps_log_mode;
    if (gps.sentencesStale()) stats.statusNote = "GPS: no sentences";
    else stats.statusNote = "";

    bool skipGeo = (cfg.gps_log_mode == "zero_gps" && !(live.has_3d_fix && live.accuracy_ok));
    stats.inGeofence = canLog && !skipGeo && filter.inGeofence(logFix.lat, logFix.lng);

    ui.draw(title, live, canLog ? &logFix : nullptr, canLog, stats, logger, cfg);
}

void Wardriving::scanBle(const WdGpsFix &fix, bool skipGeofence) {
#if defined(KVX_BLE_BACKEND_HOSTED)
    if (!bleBackend().startScan()) {
        displayError("WD BLE\nneeds hosted C6 BLE GAP", true);
        return;
    }
    uint32_t end = millis() + (uint32_t)scanTime * 1000UL;
    while (millis() < end) {
        gps.pump();
        if (check(EscPress)) break;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    BleScanResult hits[32];
    int count = bleBackend().copyScan(hits, 32);
    bleBackend().stopScan();
    stats.bleSeen += count;
    for (int i = 0; i < count; i++) {
        String address = hits[i].addr;
        String name = hits[i].name;
        int rssi = hits[i].rssi;
        uint16_t mfg = 0;
        if (hits[i].mfgLen >= 2) mfg = (uint16_t(hits[i].mfg[1]) << 8) | uint16_t(hits[i].mfg[0]);
        uint64_t key = 0;
        parseMacToU64(address, key);
        bool isNew = key ? logger.noteUnique(key) : true;
        bool allow =
            filter.allowLog("", address, fix.lat, fix.lng, skipGeofence);
        if (!allow) {
            stats.blocked++;
            if (cfg.beep_on_blocked) beep(800, 30);
            continue;
        }
        if (!stats.loggingPaused) {
            logger.writeBle(address, name, rssi, mfg, fix);
            stats.bleLogged++;
        }
        if (isNew && cfg.beep_on_new) beep(2500, 40);
        checkForAlert(address, "BLE", name);
    }
#else
    if (!bleInitialized || pBLEScan == nullptr) {
        if (!BLEDevice::init("")) return;
        pBLEScan = BLEDevice::getScan();
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(SCAN_INT);
        pBLEScan->setWindow(SCAN_WINDOW);
        bleInitialized = true;
    }
    if (pBLEScan->isScanning()) {
        pBLEScan->stop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    // Pump GPS during the blocking window by using short async-style waits:
    // getResults blocks; pump before/after.
    gps.pump();
    BLEScanResults found = pBLEScan->getResults(scanTime * 1000, false);
    gps.pump();
    int count = found.getCount();
    stats.bleSeen += count;
    for (int i = 0; i < count; i++) {
        const NimBLEAdvertisedDevice *device = found.getDevice(i);
        if (!device) continue;
        String address;
        String name;
        int rssi = 0;
        uint16_t mfg = 0;
        try {
            address = device->getAddress().toString().c_str();
            name = device->getName().c_str();
            rssi = device->getRSSI();
            if (device->haveManufacturerData()) {
                std::string md = device->getManufacturerData();
                if (md.length() >= 2) mfg = (uint16_t(md[1]) << 8) | uint16_t(md[0]);
            }
        } catch (...) { continue; }
        uint64_t key = 0;
        parseMacToU64(address, key);
        bool isNew = key ? logger.noteUnique(key) : true;
        if (!filter.allowLog("", address, fix.lat, fix.lng, skipGeofence)) {
            stats.blocked++;
            if (cfg.beep_on_blocked) beep(800, 30);
            continue;
        }
        if (!stats.loggingPaused) {
            logger.writeBle(address, name, rssi, mfg, fix);
            stats.bleLogged++;
        }
        if (isNew && cfg.beep_on_new) beep(2500, 40);
        checkForAlert(address, "BLE", name);
        if ((i & 0x1F) == 0) {
            gps.pump();
            vTaskDelay(1);
        }
    }
    pBLEScan->clearResults();
#endif
}

void Wardriving::runSweep() {
    WdGpsFix fix;
    if (!gps.resolveLogPosition(fix)) {
        // Still scan for UI counts when possible
        if (wantWifi && !scanner.wifiStopped()) {
            if (wantWifi) scanner.maybeRotateMac(sweepIndex);
            std::vector<WdWifiHit> hits;
            scanner.sweep(hits, [this]() { gps.pump(); });
            memset(stats.channelSweep, 0, sizeof(stats.channelSweep));
            for (const auto &h : hits) {
                stats.wifiSeen++;
                int ch = h.channel;
                if (ch >= 1 && ch <= 13) {
                    stats.channelSweep[ch]++;
                    stats.channelSession[ch]++;
                }
                countAuth(stats, h.auth);
                uint64_t key = 0;
                bool ok = parseMacToU64(h.bssid, key);
                bool isNew = ok && logger.noteUnique(key);
                if (isNew && ch >= 1 && ch <= 13) stats.channelUnique[ch]++;
                WdRecentAp ap{h.ssid, h.bssid, h.rssi, h.channel, h.auth, "", isNew};
                ui.pushRecent(ap, isNew);
            }
            logger.noteSweep();
            sweepIndex++;
        }
        if (wantBle) {
            // BLE without log position: skip CSV
        }
        return;
    }

    bool skipGeo = (cfg.gps_log_mode == "zero_gps" && (fix.lat == 0.0 && fix.lng == 0.0));
    if (!skipGeo && filter.inGeofence(fix.lat, fix.lng)) {
        stats.inGeofence = true;
        if (cfg.beep_on_geofence && millis() - lastGeofenceBeepMs > 10000) {
            beep(1500, 40);
            lastGeofenceBeepMs = millis();
        }
    }

    if (hadPosition) stats.distance_m += gps.distanceMeters(lastLat, lastLng, fix.lat, fix.lng);
    lastLat = fix.lat;
    lastLng = fix.lng;
    hadPosition = true;

    if (wantWifi && !scanner.wifiStopped()) {
        scanner.maybeRotateMac(sweepIndex);
        std::vector<WdWifiHit> hits;
        scanner.sweep(hits, [this]() { gps.pump(); });
        memset(stats.channelSweep, 0, sizeof(stats.channelSweep));
        for (const auto &h : hits) {
            stats.wifiSeen++;
            int ch = h.channel;
            if (ch >= 1 && ch <= 13) {
                stats.channelSweep[ch]++;
                stats.channelSession[ch]++;
            }
            countAuth(stats, h.auth);
            uint64_t key = 0;
            bool ok = parseMacToU64(h.bssid, key);
            bool isNew = ok && logger.noteUnique(key);
            if (isNew && ch >= 1 && ch <= 13) stats.channelUnique[ch]++;

            bool allow = filter.allowLog(h.ssid, h.bssid, fix.lat, fix.lng, skipGeo);
            if (!allow) {
                stats.blocked++;
                if (cfg.beep_on_blocked) beep(800, 25);
            } else if (!stats.loggingPaused) {
                logger.writeWifi(h, fix);
                stats.wifiLogged++;
                if (isNew && cfg.beep_on_new) beep(2500, 35);
            }
            WdRecentAp ap{h.ssid, h.bssid, h.rssi, h.channel, h.auth, fix.datetime, isNew};
            ui.pushRecent(ap, isNew);
            checkForAlert(h.bssid, "WiFi", h.ssid);
        }
    }

    if (wantBle) scanBle(fix, skipGeo);

    logger.noteSweep();
    sweepIndex++;
    logger.maybeFlush();
}

void Wardriving::loop() {
    returnToMenu = false;
    while (!sessionEnded && !returnToMenu && !forceHome) {
        if (check(EscPress)) {
            endSession();
            break;
        }
        gps.pump();
        handleKeys();
        if (sessionEnded) break;

        checkBattery();
        updateHud();

        uint32_t delayMs = (uint32_t)max(50, cfg.scan_delay_ms);
        if (lastScanMs == 0 || millis() - lastScanMs >= delayMs) {
            runSweep();
            lastScanMs = millis();
        }

        if (cfg.stats_enabled && millis() - lastStatsMs >= (uint32_t)cfg.stats_interval_s * 1000UL) {
            writeStatsRow("run");
            lastStatsMs = millis();
        }

        uint32_t slice = millis();
        while (millis() - slice < 40) {
            gps.pump();
            if (check(EscPress)) {
                endSession();
                break;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    if (!sessionEnded) endSession();
}

void Wardriving::configMenu() { wardrivingConfigMenu(); }

void wardrivingConfigMenu() {
    FS *fs = nullptr;
    setupSdCard();
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    kvx::paths::ensureDir(*fs, OUT_DIR);
    WdConfig cfg;
    wd_config::load(*fs, OUT_DIR, cfg);

    while (true) {
        options = {
            {"GPS mode: " + cfg.gps_log_mode,
             [&]() {
                 options = {
                     {"fix_only", [&]() { cfg.gps_log_mode = "fix_only"; }},
                     {"zero_gps", [&]() { cfg.gps_log_mode = "zero_gps"; }},
                     {"last_known", [&]() { cfg.gps_log_mode = "last_known"; }},
                 };
                 loopOptions(options, MENU_TYPE_SUBMENU, "GPS log mode");
             }},
            {"Scan: " + cfg.scan_mode,
             [&]() {
                 options = {
                     {"active_all", [&]() { cfg.scan_mode = "active_all"; }},
                     {"active_hop", [&]() { cfg.scan_mode = "active_hop"; }},
                     {"passive_all", [&]() { cfg.scan_mode = "passive_all"; }},
                     {"passive_hop", [&]() { cfg.scan_mode = "passive_hop"; }},
                 };
                 loopOptions(options, MENU_TYPE_SUBMENU, "Scan mode");
             }},
            {"Min sats: " + String(cfg.min_sats),
             [&]() {
                 String v = num_keyboard(String(cfg.min_sats), 2, "Min satellites");
                 if (v.length()) cfg.min_sats = constrain(v.toInt(), 1, 20);
             }},
            {"Acc m: " + String((int)cfg.accuracy_threshold_m),
             [&]() {
                 String v = num_keyboard(String((int)cfg.accuracy_threshold_m), 5, "Accuracy meters");
                 if (v.length()) cfg.accuracy_threshold_m = (float)v.toInt();
             }},
            {"GMT offset: " + String(cfg.gmt_offset_hours),
             [&]() {
                 String v = num_keyboard(String(cfg.gmt_offset_hours), 3, "GMT offset hours");
                 if (v.length()) cfg.gmt_offset_hours = constrain(v.toInt(), -12, 14);
             }},
            {"Active dwell: " + String(cfg.active_dwell_ms),
             [&]() {
                 String v = num_keyboard(String(cfg.active_dwell_ms), 4, "Active dwell ms");
                 if (v.length()) cfg.active_dwell_ms = constrain(v.toInt(), 10, 2000);
             }},
            {"Passive dwell: " + String(cfg.passive_dwell_ms),
             [&]() {
                 String v = num_keyboard(String(cfg.passive_dwell_ms), 4, "Passive dwell ms");
                 if (v.length()) cfg.passive_dwell_ms = constrain(v.toInt(), 10, 2000);
             }},
            {"Scan delay: " + String(cfg.scan_delay_ms),
             [&]() {
                 String v = num_keyboard(String(cfg.scan_delay_ms), 4, "Scan delay ms");
                 if (v.length()) cfg.scan_delay_ms = constrain(v.toInt(), 50, 5000);
             }},
            {"Country: " + cfg.wifi_country_code,
             [&]() {
                 String v = keyboard(cfg.wifi_country_code, 2, "Country code");
                 if (v.length()) {
                     v.toUpperCase();
                     cfg.wifi_country_code = v;
                 }
             }},
            {"TX power: " + String(cfg.wifi_tx_power),
             [&]() {
                 String v = num_keyboard(String(cfg.wifi_tx_power), 3, "TX quarter-dBm");
                 if (v.length()) cfg.wifi_tx_power = constrain(v.toInt(), 8, 84);
             }},
            {"MAC rotate: " + String(cfg.mac_randomize_interval),
             [&]() {
                 String v = num_keyboard(String(cfg.mac_randomize_interval), 3, "0=off N=every N");
                 if (v.length()) cfg.mac_randomize_interval = constrain(v.toInt(), 0, 100);
             }},
            {"MAC OUI spoof: " + String(cfg.mac_spoof_oui ? "ON" : "OFF"),
             [&]() { cfg.mac_spoof_oui = !cfg.mac_spoof_oui; }},
            {"Beep new AP: " + String(cfg.beep_on_new ? "ON" : "OFF"),
             [&]() { cfg.beep_on_new = !cfg.beep_on_new; }},
            {"Beep blocked: " + String(cfg.beep_on_blocked ? "ON" : "OFF"),
             [&]() { cfg.beep_on_blocked = !cfg.beep_on_blocked; }},
            {"Beep geofence: " + String(cfg.beep_on_geofence ? "ON" : "OFF"),
             [&]() { cfg.beep_on_geofence = !cfg.beep_on_geofence; }},
            {"Low bat %: " + String(cfg.low_battery_pct),
             [&]() {
                 String v = num_keyboard(String(cfg.low_battery_pct), 3, "0=disable");
                 if (v.length()) cfg.low_battery_pct = constrain(v.toInt(), 0, 50);
             }},
            {"Stats CSV: " + String(cfg.stats_enabled ? "ON" : "OFF"),
             [&]() { cfg.stats_enabled = !cfg.stats_enabled; }},
            {"Auto upload: " + String(cfg.auto_upload_on_launch ? "ON" : "OFF"),
             [&]() { cfg.auto_upload_on_launch = !cfg.auto_upload_on_launch; }},
            {"Upload SSID",
             [&]() {
                 String v = keyboard(cfg.upload_ssid, 32, "Home SSID");
                 cfg.upload_ssid = v;
             }},
            {"Upload pass",
             [&]() {
                 String v = keyboard(cfg.upload_password, 64, "WiFi password", true);
                 cfg.upload_password = v;
             }},
            {"Gzip upload: " + String(cfg.compress ? "ON" : "OFF"), [&]() { cfg.compress = !cfg.compress; }},
            {"Delete after: " + String(cfg.delete_after_upload ? "ON" : "OFF"),
             [&]() { cfg.delete_after_upload = !cfg.delete_after_upload; }},
            {"Min sweeps: " + String(cfg.min_sweeps),
             [&]() {
                 String v = num_keyboard(String(cfg.min_sweeps), 4, "Thin quarantine");
                 if (v.length()) cfg.min_sweeps = constrain(v.toInt(), 0, 9999);
             }},
            {"Clear exclude lists",
             [&]() {
                 cfg.excluded_ssids.clear();
                 cfg.excluded_bssids.clear();
                 cfg.geofence_boxes.clear();
                 displaySuccess("Lists cleared", true);
             }},
            {"Save",
             [&]() {
                 if (wd_config::save(*fs, OUT_DIR, cfg)) displaySuccess("Saved", true);
                 else displayError("Save failed", true);
             }},
            {"Back", [&]() { returnToMenu = true; }},
        };
        returnToMenu = false;
        loopOptions(options, MENU_TYPE_SUBMENU, "WD Config");
        if (returnToMenu) break;
    }
    returnToMenu = false;
}

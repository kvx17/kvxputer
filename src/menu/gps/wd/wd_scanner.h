#pragma once

#include "wd_config.h"
#include "wd_gps.h"
#include <Arduino.h>
#include <WiFi.h>
#include <functional>
#include <vector>

struct WdWifiHit {
    String bssid;
    String ssid;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    int channel = 0;
    int frequency = 0;
    int rssi = 0;
};

class WdScanner {
public:
    void applyConfig(const WdConfig &cfg);
    void beginRadio();
    void restoreRadio();

    /// Runs one full sweep. Calls onTick often (GPS pump + Esc). Returns -1 if aborted.
    int sweep(
        std::vector<WdWifiHit> &out,
        const std::function<bool()> &shouldAbort,
        const std::function<void()> &onTick
    );

    void maybeRotateMac(uint32_t sweepIndex);
    void stopWifi();
    void startWifi();
    bool wifiStopped() const { return stopped; }
    void abortScan();

    int channelCount() const { return maxChannel; }

private:
    String mode = "active_all";
    int activeDwell = 150;
    int passiveDwell = 250;
    int macInterval = 1;
    bool macSpoofOui = false;
    String country = "US";
    int txPower = 80;
    int maxChannel = 11;
    bool stopped = false;
    bool radioApplied = false;
    uint8_t savedMac[6] = {};
    bool savedMacValid = false;
    int8_t savedTx = 80;
    wifi_country_t savedCountry = {};
    bool savedCountryValid = false;

    void applyCountryAndPower();
    int freqForChannel(int ch) const;
    void randomizeMac();
    int scanChannelAsync(
        uint8_t channel,
        bool passive,
        uint32_t dwell,
        const std::function<bool()> &shouldAbort,
        const std::function<void()> &onTick
    );
};

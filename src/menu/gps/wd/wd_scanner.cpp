#include "wd_scanner.h"
#include <esp_wifi.h>

void WdScanner::applyConfig(const WdConfig &cfg) {
    mode = cfg.scan_mode;
    activeDwell = cfg.active_dwell_ms;
    passiveDwell = cfg.passive_dwell_ms;
    macInterval = cfg.mac_randomize_interval;
    macSpoofOui = cfg.mac_spoof_oui;
    country = cfg.wifi_country_code;
    txPower = cfg.wifi_tx_power;

    String cc = country;
    cc.toUpperCase();
    if (cc == "US" || cc == "CA" || cc == "MX") maxChannel = 11;
    else if (cc == "JP") maxChannel = 14;
    else maxChannel = 13;
}

void WdScanner::beginRadio() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        memcpy(savedMac, mac, 6);
        savedMacValid = true;
    }
    wifi_country_t cur;
    if (esp_wifi_get_country(&cur) == ESP_OK) {
        savedCountry = cur;
        savedCountryValid = true;
    }
    int8_t pwr = 0;
    if (esp_wifi_get_max_tx_power(&pwr) == ESP_OK) savedTx = pwr;
    applyCountryAndPower();
    radioApplied = true;
    stopped = false;
}

void WdScanner::applyCountryAndPower() {
#if !defined(ARDUINO_M5STACK_TAB5)
    wifi_country_t c = {};
    String cc = country;
    cc.toUpperCase();
    if (cc.length() >= 2) {
        c.cc[0] = cc[0];
        c.cc[1] = cc[1];
        c.cc[2] = 0;
    } else {
        c.cc[0] = 'U';
        c.cc[1] = 'S';
        c.cc[2] = 0;
    }
    c.schan = 1;
    c.nchan = maxChannel;
    c.policy = WIFI_COUNTRY_POLICY_MANUAL;
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
    c.max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER;
#endif
    esp_wifi_set_country(&c);
    esp_wifi_set_max_tx_power((int8_t)txPower);
#endif
}

void WdScanner::restoreRadio() {
    if (!radioApplied) return;
#if !defined(ARDUINO_M5STACK_TAB5)
    if (savedCountryValid) esp_wifi_set_country(&savedCountry);
    esp_wifi_set_max_tx_power(savedTx);
#endif
    if (savedMacValid) esp_wifi_set_mac(WIFI_IF_STA, savedMac);
    radioApplied = false;
}

void WdScanner::stopWifi() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    stopped = true;
}

void WdScanner::startWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    applyCountryAndPower();
    stopped = false;
}

void WdScanner::randomizeMac() {
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)esp_random();
    if (macSpoofOui) {
        // Keep locally administered bit clear half the time for OUI-like look
        if (esp_random() & 1) mac[0] = (mac[0] & 0xFC) | 0x02;
        else mac[0] = (mac[0] & 0xFE); // unicast, may look like vendor
    } else {
        mac[0] = (mac[0] & 0xFC) | 0x02; // locally administered unicast
    }
    esp_wifi_set_mac(WIFI_IF_STA, mac);
}

void WdScanner::maybeRotateMac(uint32_t sweepIndex) {
    if (macInterval <= 0) return;
    if ((sweepIndex % (uint32_t)macInterval) != 0) return;
    randomizeMac();
}

int WdScanner::freqForChannel(int ch) const {
    if (ch == 14) return 2484;
    if (ch >= 1 && ch <= 13) return 2407 + ch * 5;
    return 0;
}

int WdScanner::sweep(std::vector<WdWifiHit> &out, const std::function<void()> &gpsPump) {
    out.clear();
    if (stopped) return 0;

    const bool passive = mode.startsWith("passive");
    const bool hop = mode.endsWith("hop");
    const uint32_t dwell = passive ? (uint32_t)passiveDwell : (uint32_t)activeDwell;

    auto harvest = [&](int n) {
        for (int i = 0; i < n; i++) {
            WdWifiHit h;
            h.bssid = WiFi.BSSIDstr(i);
            h.ssid = WiFi.SSID(i);
            h.auth = WiFi.encryptionType(i);
            h.channel = WiFi.channel(i);
            h.frequency = freqForChannel(h.channel);
            h.rssi = WiFi.RSSI(i);
            out.push_back(h);
        }
        WiFi.scanDelete();
    };

    if (!hop) {
        if (gpsPump) gpsPump();
        int n = WiFi.scanNetworks(false, true, passive, dwell, 0);
        if (gpsPump) gpsPump();
        if (n < 0) n = 0;
        harvest(n);
        return (int)out.size();
    }

    for (int ch = 1; ch <= maxChannel; ch++) {
        if (gpsPump) gpsPump();
        int n = WiFi.scanNetworks(false, true, passive, dwell, (uint8_t)ch);
        if (gpsPump) gpsPump();
        if (n < 0) n = 0;
        for (int i = 0; i < n; i++) {
            WdWifiHit h;
            h.bssid = WiFi.BSSIDstr(i);
            h.ssid = WiFi.SSID(i);
            h.auth = WiFi.encryptionType(i);
            h.channel = WiFi.channel(i);
            if (h.channel == 0) h.channel = ch;
            h.frequency = freqForChannel(h.channel);
            h.rssi = WiFi.RSSI(i);
            out.push_back(h);
        }
        WiFi.scanDelete();
    }
    return (int)out.size();
}

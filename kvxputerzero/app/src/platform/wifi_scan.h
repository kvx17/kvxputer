#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct WifiAp {
    std::string ssid;
    std::string bssid;
    int channel{-1};
    int rssi{-127};
    bool open{false};
    std::string security;     // open | other
    std::string security_raw; // nmcli text, e.g. WPA2
};

// Uses `nmcli` when available. Desktop without NM returns a clear error.
std::vector<WifiAp> wifi_scan(std::string& error, bool* cancel = nullptr);

bool wifi_connect_open(const std::string& ssid, std::string& error);

// Passphrase is passed to nmcli on the command line; never written to a world-readable file.
bool wifi_connect_psk(const std::string& ssid, const std::string& psk, std::string& error);

bool wifi_disconnect(std::string& error);
bool wifi_radio_off(std::string& error);

} // namespace platform

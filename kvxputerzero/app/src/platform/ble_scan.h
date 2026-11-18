#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace platform {

struct BleDevice {
    std::string name;
    std::string address;
    std::string addr_type;
    int rssi{-127};
    std::string services;
    std::string mfg_hex;
    std::string tx_power;
    std::string appearance;
};

// Best-effort via bluetoothctl. Desktop without BlueZ returns a clear error.
// on_device may be called from the calling thread as devices are parsed.
bool ble_scan_once(std::vector<BleDevice>& devices, std::string& error, int seconds = 5,
                   bool* cancel = nullptr);

bool ble_power_off(std::string& error);

struct BleStatusResult {
    bool ok{false};
    std::string error;
    std::vector<std::string> lines;
};

// Scan briefly, find devices matching name substring and/or service UUID substring (case-insensitive),
// try connect, report name/address/rssi and connect result. Does not write GATT characteristics.
BleStatusResult ble_status_probe(const char* name_substr, const char* service_uuid_substr,
                                 int scan_seconds = 5, bool* cancel = nullptr);

} // namespace platform

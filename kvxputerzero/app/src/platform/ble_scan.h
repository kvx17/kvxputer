#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct BleDevice {
    std::string name;
    std::string address;
    std::string addr_type;
    int rssi{-127};
    std::string services;
    // Continuous hex, company id little-endian + payload; multiple entries joined with ';'.
    // Same layout as Cardputer ScannerAdvSnap.mfg (bytes → hex here).
    std::string mfg_hex;
    std::string tx_power;
    std::string appearance;
    // Raw AD structures from the advert (NimBLE getPayload() equivalent). Used to
    // match Flipper UUID16 lists the same way wall_of_flipper.cpp does.
    std::vector<uint8_t> adv_payload;
};

// LE scan via BlueZ discovery + HCI advert sniff (raw AD). Desktop without BlueZ
// returns a clear error.
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

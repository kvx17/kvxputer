/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "device_status.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef APP_POWER_SUPPLY_ROOT
#define APP_POWER_SUPPLY_ROOT "/sys/class/power_supply"
#endif

#ifndef APP_NETWORK_CLASS_ROOT
#define APP_NETWORK_CLASS_ROOT "/sys/class/net"
#endif

#ifndef APP_WIRELESS_STATUS_PATH
#define APP_WIRELESS_STATUS_PATH "/proc/net/wireless"
#endif

namespace platform {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool read_text_file(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::getline(file, value);
    value = trim(value);
    return true;
}

int32_t read_int_file(const std::filesystem::path& path, int32_t fallback = -1) {
    std::ifstream file(path);
    int32_t value = fallback;
    return file >> value ? value : fallback;
}

void read_battery(DeviceStatus& status) {
    const std::filesystem::path root(APP_POWER_SUPPLY_ROOT);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            return;
        }
        std::string type;
        if (!read_text_file(entry.path() / "type", type) || type != "Battery") {
            continue;
        }

        status.battery_percent = read_int_file(entry.path() / "capacity");
        status.battery_present = status.battery_percent >= 0;
        std::string battery_state;
        status.battery_charging = read_text_file(entry.path() / "status", battery_state) &&
                                  battery_state == "Charging";
        return;
    }
}

void read_wifi_strength(DeviceStatus& status, const std::string& interface_name) {
    std::ifstream file(APP_WIRELESS_STATUS_PATH);
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        if (trim(line.substr(0, colon)) != interface_name) {
            continue;
        }
        std::istringstream values(line.substr(colon + 1));
        std::string state;
        double quality = -1.0;
        if (values >> state >> quality) {
            status.wifi_strength_percent = std::clamp(static_cast<int32_t>(quality * 100.0 / 70.0), 0, 100);
            return;
        }
    }
}

void read_network(DeviceStatus& status) {
    const std::filesystem::path root(APP_NETWORK_CLASS_ROOT);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return;
    }

    std::string connected_wifi_interface;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || entry.path().filename() == "lo") {
            continue;
        }

        const bool wireless = std::filesystem::exists(entry.path() / "wireless", ec);
        const bool connected = read_int_file(entry.path() / "carrier", 0) == 1;
        if (wireless) {
            status.wifi_present = true;
            status.wifi_connected |= connected;
            if (connected && connected_wifi_interface.empty()) {
                connected_wifi_interface = entry.path().filename().string();
            }
        }
        else if (read_int_file(entry.path() / "type") == 1) {
            status.ethernet_present = true;
            status.ethernet_connected |= connected;
        }
    }
    if (!connected_wifi_interface.empty()) {
        read_wifi_strength(status, connected_wifi_interface);
    }
}

} // namespace

DeviceStatus read_device_status() {
#if USE_DESKTOP
    return DeviceStatus{
        true,
        true,
        100,
        true,
        true,
        true,
        true,
        100,
    };
#else
    DeviceStatus status;
    read_network(status);
    read_battery(status);
    return status;
#endif
}

} // namespace platform

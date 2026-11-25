#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct UsbDeviceInfo {
    std::string sys_name;     // e.g. 1-1.2
    std::string vendor_id;    // 4 hex digits
    std::string product_id;   // 4 hex digits
    std::string manufacturer;
    std::string product;
    std::string device_class; // bDeviceClass hex
    std::vector<std::string> interfaces; // "cls=03 proto=01 (HID keyboard)" …
    bool has_hid_keyboard{false};
    bool suspicious{false};
    std::string suspicion_reason;
};

// Walk /sys/bus/usb/devices. Desktop without sysfs returns empty + error.
std::vector<UsbDeviceInfo> list_usb_devices(std::string& error);

} // namespace platform

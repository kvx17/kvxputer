#include "usb_probe.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace platform {
namespace {

namespace fs = std::filesystem;

std::string read_trim(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    return s;
}

bool looks_like_usb_device_dir(const std::string& name) {
    // Device dirs look like "1-1", "1-1.2", "2-3"; skip "usb1", "1-0:1.0" interfaces.
    if (name.empty() || name.find(':') != std::string::npos) return false;
    if (name.rfind("usb", 0) == 0) return false;
    bool has_digit = false;
    for (char c : name) {
        if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
        else if (c != '-' && c != '.') return false;
    }
    return has_digit && name.find('-') != std::string::npos;
}

std::string class_name(const std::string& hex) {
    int v = -1;
    try {
        v = std::stoi(hex, nullptr, 16);
    } catch (...) {
        return hex.empty() ? "?" : hex;
    }
    switch (v) {
    case 0x00: return "per-iface";
    case 0x01: return "Audio";
    case 0x02: return "CDC";
    case 0x03: return "HID";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC-Data";
    case 0x0E: return "Video";
    case 0xE0: return "Wireless";
    case 0xFF: return "Vendor";
    default: return hex;
    }
}

} // namespace

std::vector<UsbDeviceInfo> list_usb_devices(std::string& error) {
    error.clear();
    const fs::path root = "/sys/bus/usb/devices";
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        error = "USB sysfs unavailable";
        return {};
    }

    std::vector<UsbDeviceInfo> out;
    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        const std::string name = ent.path().filename().string();
        if (!looks_like_usb_device_dir(name)) continue;

        const fs::path dir = ent.path();
        UsbDeviceInfo d;
        d.sys_name = name;
        d.vendor_id = read_trim(dir / "idVendor");
        d.product_id = read_trim(dir / "idProduct");
        if (d.vendor_id.empty() || d.product_id.empty()) continue;
        d.manufacturer = read_trim(dir / "manufacturer");
        d.product = read_trim(dir / "product");
        d.device_class = read_trim(dir / "bDeviceClass");

        // Child interface dirs: "1-1.2:1.0"
        for (const auto& iface : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!iface.is_directory(ec)) continue;
            const std::string iname = iface.path().filename().string();
            if (iname.find(':') == std::string::npos) continue;
            const std::string icls = read_trim(iface.path() / "bInterfaceClass");
            const std::string iproto = read_trim(iface.path() / "bInterfaceProtocol");
            if (icls.empty()) continue;
            std::ostringstream label;
            label << "cls=" << icls << " proto=" << (iproto.empty() ? "?" : iproto) << " ("
                  << class_name(icls);
            int proto = -1;
            try {
                proto = std::stoi(iproto, nullptr, 16);
            } catch (...) {
            }
            int cls = -1;
            try {
                cls = std::stoi(icls, nullptr, 16);
            } catch (...) {
            }
            if (cls == 0x03 && proto == 0x01) {
                label << " keyboard";
                d.has_hid_keyboard = true;
            } else if (cls == 0x03 && proto == 0x02) {
                label << " mouse";
            }
            label << ")";
            d.interfaces.push_back(label.str());
        }

        if (d.has_hid_keyboard) {
            d.suspicious = true;
            d.suspicion_reason = "HID keyboard interface present";
        }
        out.push_back(std::move(d));
    }

    if (out.empty() && error.empty()) {
        // Empty is valid (no devices); leave error clear.
    }
    return out;
}

} // namespace platform

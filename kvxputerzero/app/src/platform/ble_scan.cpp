#include "ble_scan.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace platform {
namespace {

std::string run_capture(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return {};
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string strip_ansi(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\033' && i + 1 < in.size() && in[i + 1] == '[') {
            i += 2;
            while (i < in.size() && in[i] != 'm') ++i;
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string dbus_string_value(const std::string& prop) {
    const auto q = prop.find('"');
    if (q == std::string::npos) return {};
    const auto q2 = prop.find('"', q + 1);
    if (q2 == std::string::npos) return {};
    return prop.substr(q + 1, q2 - q - 1);
}

// busctl prints "n -61" / "i -61" / "q 123". Also tolerate "RSSI: 0xffffffc3 (-61)".
bool parse_int_token(const std::string& prop, int& out) {
    const auto paren = prop.find('(');
    if (paren != std::string::npos) {
        const auto end = prop.find(')', paren + 1);
        if (end != std::string::npos) {
            try {
                out = std::stoi(trim_copy(prop.substr(paren + 1, end - paren - 1)));
                return true;
            } catch (...) {
            }
        }
    }
    std::istringstream iss(prop);
    std::string tag;
    if (!(iss >> tag)) return false;
    int v = 0;
    if (iss >> v) {
        out = v;
        return true;
    }
    // Fall back: first signed integer in the string.
    for (size_t i = 0; i < prop.size(); ++i) {
        if (prop[i] == '-' || std::isdigit(static_cast<unsigned char>(prop[i]))) {
            try {
                out = std::stoi(prop.substr(i));
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

int parse_uint_token(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    if (pos >= s.size()) return -1;
    size_t end = pos;
    if (s[end] == '0' && end + 1 < s.size() && (s[end + 1] == 'x' || s[end + 1] == 'X')) {
        end += 2;
        while (end < s.size() && std::isxdigit(static_cast<unsigned char>(s[end]))) ++end;
        try {
            const int v = static_cast<int>(std::stoul(s.substr(pos, end - pos), nullptr, 0));
            pos = end;
            return v;
        } catch (...) {
            return -1;
        }
    }
    if (s[end] == '-' || std::isdigit(static_cast<unsigned char>(s[end]))) {
        if (s[end] == '-') ++end;
        while (end < s.size() && std::isdigit(static_cast<unsigned char>(s[end]))) ++end;
        try {
            const int v = std::stoi(s.substr(pos, end - pos));
            pos = end;
            return v;
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

std::string adapter_path() {
    const std::string tree = run_capture("busctl --system tree org.bluez 2>/dev/null");
    std::istringstream iss(tree);
    std::string line;
    while (std::getline(iss, line)) {
        const auto pos = line.find("/org/bluez/hci");
        if (pos == std::string::npos) continue;
        std::string path = line.substr(pos);
        const auto dev = path.find("/dev_");
        if (dev != std::string::npos) path = path.substr(0, dev);
        while (!path.empty() && (path.back() == ' ' || path.back() == '\r')) path.pop_back();
        if (path.rfind("/org/bluez/hci", 0) == 0) return path;
    }
    return "/org/bluez/hci0";
}

std::vector<std::string> list_device_paths(const std::string& adapter) {
    const std::string tree = run_capture("busctl --system tree org.bluez 2>/dev/null");
    std::set<std::string> paths;
    std::istringstream iss(tree);
    std::string line;
    const std::string prefix = adapter + "/dev_";
    while (std::getline(iss, line)) {
        const auto pos = line.find(prefix);
        if (pos == std::string::npos) continue;
        std::string path = line.substr(pos);
        const auto slash = path.find('/', prefix.size());
        if (slash != std::string::npos) path = path.substr(0, slash);
        while (!path.empty() && (path.back() == ' ' || path.back() == '\r')) path.pop_back();
        paths.insert(path);
    }
    return {paths.begin(), paths.end()};
}

std::string get_prop(const std::string& path, const char* iface_prop) {
    const std::string cmd =
        "busctl --system get-property org.bluez '" + path + "' org.bluez.Device1 " + iface_prop +
        " 2>/dev/null";
    return run_capture(cmd.c_str());
}

// BlueZ ManufacturerData: dict company_id -> bytes (company id NOT repeated in value).
// Rebuild the same layout NimBLE gives the firmware: CID little-endian + payload.
std::string parse_manufacturer_data(const std::string& prop) {
    if (prop.empty() || prop.find("Failed") != std::string::npos) return {};

    size_t pos = prop.find("a{qv}");
    if (pos == std::string::npos) return {};
    pos += 5;
    const int count = parse_uint_token(prop, pos);
    if (count <= 0) return {};

    std::string out;
    for (int e = 0; e < count; ++e) {
        const int key = parse_uint_token(prop, pos);
        if (key < 0) break;
        while (pos < prop.size() && std::isspace(static_cast<unsigned char>(prop[pos]))) ++pos;
        if (pos + 1 >= prop.size() || prop[pos] != 'a' || prop[pos + 1] != 'y') break;
        pos += 2;
        const int len = parse_uint_token(prop, pos);
        if (len < 0) break;
        char cid[8];
        std::snprintf(cid, sizeof(cid), "%02x%02x", key & 0xff, (key >> 8) & 0xff);
        std::string entry = cid;
        for (int i = 0; i < len; ++i) {
            const int b = parse_uint_token(prop, pos);
            if (b < 0) break;
            char hx[4];
            std::snprintf(hx, sizeof(hx), "%02x", b & 0xff);
            entry += hx;
        }
        if (!out.empty()) out.push_back(';');
        out += entry;
    }
    return out;
}

std::string parse_uuids(const std::string& prop) {
    std::string out;
    std::size_t pos = 0;
    while (true) {
        const auto q1 = prop.find('"', pos);
        if (q1 == std::string::npos) break;
        const auto q2 = prop.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string u = prop.substr(q1 + 1, q2 - q1 - 1);
        if (!u.empty()) {
            if (!out.empty()) out.push_back(' ');
            out += u;
        }
        pos = q2 + 1;
    }
    return out;
}

// ServiceData keys are UUID strings — Flipper 16-bit UUIDs can show up here too.
std::string parse_service_data_keys(const std::string& prop) {
    return parse_uuids(prop);
}

void fill_from_dbus(BleDevice& d, const std::string& path) {
    const std::string addr = dbus_string_value(get_prop(path, "Address"));
    if (!addr.empty()) d.address = addr;
    std::string name = dbus_string_value(get_prop(path, "Name"));
    if (name.empty()) name = dbus_string_value(get_prop(path, "Alias"));
    if (!name.empty()) d.name = name;
    d.addr_type = dbus_string_value(get_prop(path, "AddressType"));
    int rssi = -127;
    const std::string rssi_prop = get_prop(path, "RSSI");
    if (!rssi_prop.empty() && rssi_prop.find("Failed") == std::string::npos && parse_int_token(rssi_prop, rssi)) {
        d.rssi = rssi;
    }
    const std::string uu = parse_uuids(get_prop(path, "UUIDs"));
    if (!uu.empty()) d.services = uu;
    const std::string mfg_prop = get_prop(path, "ManufacturerData");
    const std::string mfg = parse_manufacturer_data(mfg_prop);
    if (!mfg.empty()) d.mfg_hex = mfg;
    const std::string sd = get_prop(path, "ServiceData");
    if (!sd.empty() && sd.find("Failed") == std::string::npos) {
        const std::string keys = parse_service_data_keys(sd);
        if (!keys.empty()) {
            if (!d.services.empty()) d.services.push_back(' ');
            d.services += keys;
        }
    }
}

void merge_device(std::map<std::string, BleDevice>& by_addr, BleDevice&& d) {
    if (d.address.size() < 17) return;
    auto it = by_addr.find(d.address);
    if (it == by_addr.end()) {
        by_addr[d.address] = std::move(d);
        return;
    }
    auto& dst = it->second;
    if ((dst.name.empty() || dst.name == "(unknown)") && !d.name.empty() && d.name != "(unknown)") {
        dst.name = d.name;
    }
    if (d.rssi > -127) dst.rssi = d.rssi;
    if (!d.addr_type.empty()) dst.addr_type = d.addr_type;
    if (!d.services.empty()) {
        if (dst.services.empty()) dst.services = d.services;
        else {
            std::istringstream iss(d.services);
            std::string tok;
            while (iss >> tok) {
                if (dst.services.find(tok) == std::string::npos) {
                    dst.services.push_back(' ');
                    dst.services += tok;
                }
            }
        }
    }
    if (!d.mfg_hex.empty()) {
        if (dst.mfg_hex.empty()) dst.mfg_hex = d.mfg_hex;
        else {
            std::istringstream iss(d.mfg_hex);
            std::string tok;
            while (std::getline(iss, tok, ';')) {
                if (tok.empty()) continue;
                if (dst.mfg_hex.find(tok) == std::string::npos) {
                    dst.mfg_hex.push_back(';');
                    dst.mfg_hex += tok;
                }
            }
        }
    }
}

void ingest_scan_log_line(std::map<std::string, BleDevice>& by_addr, const std::string& raw) {
    const std::string sl = strip_ansi(raw);
    const auto dpos = sl.find("Device ");
    if (dpos == std::string::npos) return;
    const std::string rest = sl.substr(dpos + 7);
    if (rest.size() < 17) return;
    const std::string addr = rest.substr(0, 17);
    if (addr[2] != ':' || addr[5] != ':') return;

    BleDevice d;
    d.address = addr;
    d.name = "(unknown)";
    d.rssi = -127;

    const auto rpos = sl.find("RSSI:");
    if (rpos != std::string::npos) {
        int rssi = -127;
        if (parse_int_token(sl.substr(rpos + 5), rssi)) d.rssi = rssi;
    }

    const auto uuid_pos = sl.find("UUIDs:");
    if (uuid_pos != std::string::npos) {
        d.services = trim_copy(sl.substr(uuid_pos + 6));
    } else if (rest.size() > 18 && sl.find("NEW") != std::string::npos && sl.find("RSSI:") == std::string::npos) {
        d.name = trim_copy(rest.substr(18));
        if (d.name.empty()) d.name = "(unknown)";
    }

    const auto name_pos = sl.find("Name:");
    if (name_pos != std::string::npos) {
        d.name = trim_copy(sl.substr(name_pos + 5));
    }
    const auto alias_pos = sl.find("Alias:");
    if (alias_pos != std::string::npos && (d.name.empty() || d.name == "(unknown)")) {
        d.name = trim_copy(sl.substr(alias_pos + 6));
    }

    merge_device(by_addr, std::move(d));
}

void poll_dbus_devices(const std::string& adapter, std::map<std::string, BleDevice>& by_addr, bool* cancel) {
    for (const auto& path : list_device_paths(adapter)) {
        if (cancel && *cancel) break;
        BleDevice d;
        d.name = "(unknown)";
        d.rssi = -127;
        fill_from_dbus(d, path);
        // Only merge devices that still expose live advert fields, or that we already track.
        if (d.address.size() >= 17 &&
            (d.rssi > -127 || !d.mfg_hex.empty() || !d.services.empty() || by_addr.count(d.address))) {
            merge_device(by_addr, std::move(d));
        }
    }
}

} // namespace

bool ble_scan_once(std::vector<BleDevice>& devices, std::string& error, int seconds, bool* cancel) {
    devices.clear();
    error.clear();
    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }
    if (seconds < 1) seconds = 1;
    if (seconds > 20) seconds = 20;

    const std::string adapter = adapter_path();
    (void)run_capture(("busctl --system set-property org.bluez '" + adapter +
                       "' org.bluez.Adapter1 Powered b true 2>/dev/null")
                          .c_str());
    // Prefer LE adverts + duplicate reports so ManufacturerData/RSSI keep updating (NimBLE-like).
    (void)run_capture(("busctl --system call org.bluez '" + adapter +
                       "' org.bluez.Adapter1 SetDiscoveryFilter a{sv} 2 Transport s le DuplicateData b true "
                       "2>/dev/null")
                          .c_str());

    std::map<std::string, BleDevice> by_addr;
    std::mutex mu;
    std::atomic<bool> scan_done{false};

    // BlueZ drops ManufacturerData/RSSI when discovery stops. Run a timed scan and poll D-Bus
    // while Discovering=yes — same live-adv window the firmware gets from NimBLE callbacks.
    std::thread scan_thread([&]() {
        const std::string cmd = "bluetoothctl --timeout " + std::to_string(seconds) + " scan on 2>&1";
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            scan_done = true;
            return;
        }
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) {
            if (cancel && *cancel) break;
            std::lock_guard<std::mutex> lock(mu);
            ingest_scan_log_line(by_addr, buf);
        }
        pclose(fp);
        scan_done = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds + 1);
    while (!scan_done && std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) break;
        {
            std::lock_guard<std::mutex> lock(mu);
            poll_dbus_devices(adapter, by_addr, cancel);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }

    // One last pass in case discovery is still briefly active.
    {
        std::lock_guard<std::mutex> lock(mu);
        poll_dbus_devices(adapter, by_addr, cancel);
    }

    if (scan_thread.joinable()) scan_thread.join();
    (void)run_capture(("busctl --system call org.bluez '" + adapter +
                       "' org.bluez.Adapter1 StopDiscovery 2>/dev/null")
                          .c_str());

    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }

    // Merge any leftover names from bluetoothctl devices list.
    const std::string text = run_capture("bluetoothctl devices 2>/dev/null");
    if (by_addr.empty() && text.empty()) {
        error = "BLE scan unavailable (bluetoothctl / BlueZ)";
        return false;
    }
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("Device ", 0) != 0) continue;
        BleDevice d;
        d.address = line.substr(7, 17);
        if (line.size() > 25) d.name = trim_copy(line.substr(25));
        if (d.name.empty()) d.name = "(unknown)";
        d.rssi = -127;
        auto it = by_addr.find(d.address);
        if (it == by_addr.end()) {
            // Skip cold devices with no live advert data — they are usually paired classics.
            continue;
        }
        if ((it->second.name.empty() || it->second.name == "(unknown)") && d.name != "(unknown)") {
            it->second.name = d.name;
        }
    }

    for (auto& kv : by_addr) {
        if (kv.second.name.empty()) kv.second.name = "(unknown)";
        devices.push_back(std::move(kv.second));
    }

    if (devices.empty()) {
        error = "no BLE devices (is the adapter powered?)";
        return false;
    }
    return true;
}

bool ble_power_off(std::string& error) {
    error.clear();
    const std::string adapter = adapter_path();
    (void)run_capture(("busctl --system call org.bluez '" + adapter +
                       "' org.bluez.Adapter1 StopDiscovery 2>/dev/null")
                          .c_str());
    const std::string out = run_capture("bluetoothctl power off 2>&1");
    if (out.find("succeeded") == std::string::npos && out.find("Powered: no") == std::string::npos) {
        if (out.empty()) {
            error = "bluetoothctl unavailable";
            return false;
        }
    }
    return true;
}

namespace {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return to_lower(hay).find(to_lower(needle)) != std::string::npos;
}

} // namespace

BleStatusResult ble_status_probe(const char* name_substr, const char* service_uuid_substr, int scan_seconds,
                                 bool* cancel) {
    BleStatusResult r;
    std::vector<BleDevice> devices;
    std::string err;
    if (!ble_scan_once(devices, err, scan_seconds, cancel)) {
        r.error = err.empty() ? "BlueZ unavailable" : err;
        return r;
    }
    const std::string name_f = name_substr ? name_substr : "";
    const std::string svc_f = service_uuid_substr ? service_uuid_substr : "";
    std::vector<BleDevice> hits;
    for (const auto& d : devices) {
        const bool name_ok = name_f.empty() || contains_ci(d.name, name_f);
        const bool svc_ok = svc_f.empty() || contains_ci(d.services, svc_f);
        if (name_ok && svc_ok) hits.push_back(d);
    }
    if (hits.empty()) {
        r.lines.push_back("no matching devices");
        r.ok = true;
        return r;
    }
    std::sort(hits.begin(), hits.end(), [](const BleDevice& a, const BleDevice& b) { return a.rssi > b.rssi; });
    const BleDevice& d = hits.front();
    r.lines.push_back("name: " + d.name);
    r.lines.push_back("addr: " + d.address);
    r.lines.push_back("rssi: " + std::to_string(d.rssi));
    if (!d.services.empty()) r.lines.push_back("svc: " + d.services.substr(0, 48));

    if (cancel && *cancel) {
        r.lines.push_back("connect: cancelled");
        r.ok = true;
        return r;
    }

    const std::string cmd = "bluetoothctl --timeout 5 connect " + d.address + " 2>&1";
    const std::string out = run_capture(cmd.c_str());
    const bool connected = out.find("Connection successful") != std::string::npos ||
                           out.find("Connected: yes") != std::string::npos ||
                           out.find("successful") != std::string::npos;
    r.lines.push_back(connected ? "connect: ok (read-only)" : "connect: failed");
    if (!connected && !out.empty()) {
        std::string first = out;
        const auto nl = first.find('\n');
        if (nl != std::string::npos) first = first.substr(0, nl);
        if (first.size() > 60) first.resize(60);
        r.lines.push_back(first);
    }
    (void)run_capture(("bluetoothctl disconnect " + d.address + " 2>/dev/null").c_str());
    r.ok = true;
    return r;
}

} // namespace platform

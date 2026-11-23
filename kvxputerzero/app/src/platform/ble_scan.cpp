#include "ble_scan.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return to_lower(hay).find(to_lower(needle)) != std::string::npos;
}

std::string dbus_string_value(const std::string& prop) {
    const auto q = prop.find('"');
    if (q == std::string::npos) return {};
    const auto q2 = prop.find('"', q + 1);
    if (q2 == std::string::npos) return {};
    return prop.substr(q + 1, q2 - q - 1);
}

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
    if (std::isdigit(static_cast<unsigned char>(s[end]))) {
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

std::string bytes_to_hex(const uint8_t* p, size_t n) {
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        char hx[3];
        std::snprintf(hx, sizeof(hx), "%02x", p[i]);
        out += hx;
    }
    return out;
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

std::string parse_manufacturer_data_dbus(const std::string& prop) {
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

// Mirror Cardputer parsePayloadInto(): fill name / mfg / 16-bit service UUID from raw AD.
void parse_payload_into(BleDevice& d) {
    const uint8_t* p = d.adv_payload.data();
    const size_t len = d.adv_payload.size();
    size_t i = 0;
    bool have_svc = false;
    while (i + 1 < len) {
        const uint8_t dlen = p[i];
        if (dlen == 0) break;
        if (i + 1 + dlen > len) break;
        const uint8_t type = p[i + 1];
        const uint8_t* v = p + i + 2;
        const uint8_t vlen = static_cast<uint8_t>(dlen - 1);
        if ((type == 0x08 || type == 0x09) && vlen && (d.name.empty() || d.name == "(unknown)")) {
            d.name.assign(reinterpret_cast<const char*>(v), vlen);
            while (!d.name.empty() && (d.name.back() == ' ' || d.name.back() == '\0')) d.name.pop_back();
            if (d.name.empty()) d.name = "(unknown)";
        } else if (type == 0xFF && vlen) {
            // Same bytes Cardputer stores in ScannerAdvSnap.mfg
            const std::string entry = bytes_to_hex(v, vlen);
            if (d.mfg_hex.empty()) d.mfg_hex = entry;
            else if (d.mfg_hex.find(entry) == std::string::npos) {
                d.mfg_hex.push_back(';');
                d.mfg_hex += entry;
            }
        } else if ((type == 0x02 || type == 0x03) && vlen >= 2) {
            for (uint8_t j = 0; j + 1 < vlen; j = static_cast<uint8_t>(j + 2)) {
                const uint16_t u = static_cast<uint16_t>(v[j] | (static_cast<uint16_t>(v[j + 1]) << 8));
                char hex[5];
                std::snprintf(hex, sizeof(hex), "%04X", u);
                if (d.services.find(hex) == std::string::npos) {
                    if (!d.services.empty()) d.services.push_back(' ');
                    d.services += hex;
                }
                // Also record Bluetooth base UUID form BlueZ uses.
                char full[40];
                std::snprintf(full, sizeof(full), "0000%04x-0000-1000-8000-00805f9b34fb", u);
                if (!contains_ci(d.services, full)) {
                    d.services.push_back(' ');
                    d.services += full;
                }
                have_svc = true;
                (void)have_svc;
            }
        } else if (type == 0x0A && vlen >= 1) {
            d.tx_power = std::to_string(static_cast<int>(static_cast<int8_t>(v[0]))) + " dBm";
        } else if (type == 0x19 && vlen >= 2) {
            const int app = v[0] | (v[1] << 8);
            d.appearance = std::to_string(app);
        }
        i += static_cast<size_t>(dlen) + 1;
    }
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
    if (!uu.empty()) {
        if (d.services.empty()) d.services = uu;
        else {
            std::istringstream iss(uu);
            std::string tok;
            while (iss >> tok) {
                if (d.services.find(tok) == std::string::npos) {
                    d.services.push_back(' ');
                    d.services += tok;
                }
            }
        }
    }
    const std::string mfg = parse_manufacturer_data_dbus(get_prop(path, "ManufacturerData"));
    if (!mfg.empty()) {
        if (d.mfg_hex.empty()) d.mfg_hex = mfg;
        else if (d.mfg_hex.find(mfg) == std::string::npos) {
            d.mfg_hex.push_back(';');
            d.mfg_hex += mfg;
        }
    }
    const std::string sd = get_prop(path, "ServiceData");
    if (!sd.empty() && sd.find("Failed") == std::string::npos) {
        const std::string keys = parse_uuids(sd);
        if (!keys.empty()) {
            std::istringstream iss(keys);
            std::string tok;
            while (iss >> tok) {
                if (d.services.find(tok) == std::string::npos) {
                    if (!d.services.empty()) d.services.push_back(' ');
                    d.services += tok;
                }
            }
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
    if (!d.tx_power.empty()) dst.tx_power = d.tx_power;
    if (!d.appearance.empty()) dst.appearance = d.appearance;
    if (!d.services.empty()) {
        std::istringstream iss(d.services);
        std::string tok;
        while (iss >> tok) {
            if (dst.services.find(tok) == std::string::npos) {
                if (!dst.services.empty()) dst.services.push_back(' ');
                dst.services += tok;
            }
        }
    }
    if (!d.mfg_hex.empty()) {
        std::istringstream iss(d.mfg_hex);
        std::string tok;
        while (std::getline(iss, tok, ';')) {
            if (tok.empty()) continue;
            if (dst.mfg_hex.find(tok) == std::string::npos) {
                if (!dst.mfg_hex.empty()) dst.mfg_hex.push_back(';');
                dst.mfg_hex += tok;
            }
        }
    }
    if (!d.adv_payload.empty()) {
        // Keep the richest payload we have seen.
        if (d.adv_payload.size() >= dst.adv_payload.size()) dst.adv_payload = std::move(d.adv_payload);
    }
}

std::string mac_from_bdaddr(const bdaddr_t& a) {
    char mac[18];
    // HCI reports little-endian BD_ADDR; print big-endian like BlueZ / firmware.
    std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", a.b[5], a.b[4], a.b[3], a.b[2], a.b[1],
                  a.b[0]);
    return mac;
}

// Sniff LE advertising reports produced while BlueZ is discovering. Does NOT issue
// HCI scan-enable commands (those race with bluetoothd); only reads events.
void hci_sniff_loop(std::map<std::string, BleDevice>& by_addr, std::mutex& mu, std::atomic<bool>& stop,
                    bool* cancel) {
    const int dev_id = hci_get_route(nullptr);
    if (dev_id < 0) return;
    const int dd = hci_open_dev(dev_id);
    if (dd < 0) return;

    struct hci_filter of {};
    socklen_t olen = sizeof(of);
    getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen);

    struct hci_filter nf {};
    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf));

    while (!stop && !(cancel && *cancel)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(dd, &fds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        const int sel = select(dd + 1, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        uint8_t buf[HCI_MAX_EVENT_SIZE];
        const int len = static_cast<int>(read(dd, buf, sizeof(buf)));
        if (len < 4) continue;
        if (buf[0] != HCI_EVENT_PKT) continue;
        if (buf[1] != EVT_LE_META_EVENT) continue;
        // buf[2] = plen; meta starts at buf[3]
        if (len < 5) continue;
        const auto* meta = reinterpret_cast<evt_le_meta_event*>(buf + 3);
        if (meta->subevent != EVT_LE_ADVERTISING_REPORT) continue;
        const uint8_t num = meta->data[0];
        const uint8_t* ptr = meta->data + 1;
        const uint8_t* end = buf + len;
        for (uint8_t n = 0; n < num; ++n) {
            if (ptr + sizeof(le_advertising_info) > end) break;
            const auto* info = reinterpret_cast<const le_advertising_info*>(ptr);
            if (info->data + info->length + 1 > end) break; // + rssi
            BleDevice d;
            d.address = mac_from_bdaddr(info->bdaddr);
            d.name = "(unknown)";
            d.rssi = static_cast<int>(static_cast<int8_t>(info->data[info->length]));
            d.addr_type = (info->bdaddr_type == 0x00) ? "public" : "random";
            d.adv_payload.assign(info->data, info->data + info->length);
            parse_payload_into(d);
            {
                std::lock_guard<std::mutex> lock(mu);
                merge_device(by_addr, std::move(d));
            }
            ptr = info->data + info->length + 1;
        }
    }

    setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
    hci_close_dev(dd);
}

struct ScanLogState {
    std::string pending_addr;
    int pending_mfg_key{-1};
    bool in_mfg_value{false};
    std::vector<uint8_t> mfg_bytes;
};

void flush_pending_mfg(ScanLogState& st, std::map<std::string, BleDevice>& by_addr) {
    if (st.pending_addr.size() < 17 || st.pending_mfg_key < 0 || st.mfg_bytes.empty()) {
        st.in_mfg_value = false;
        st.mfg_bytes.clear();
        return;
    }
    char cid[8];
    std::snprintf(cid, sizeof(cid), "%02x%02x", st.pending_mfg_key & 0xff, (st.pending_mfg_key >> 8) & 0xff);
    std::string entry = cid;
    entry += bytes_to_hex(st.mfg_bytes.data(), st.mfg_bytes.size());
    BleDevice d;
    d.address = st.pending_addr;
    d.name = "(unknown)";
    d.rssi = -127;
    d.mfg_hex = entry;
    // Reconstruct a minimal AD type 0xFF payload so Flipper/AirTag helpers can use adv_payload.
    d.adv_payload.push_back(static_cast<uint8_t>(1 + 2 + st.mfg_bytes.size()));
    d.adv_payload.push_back(0xFF);
    d.adv_payload.push_back(static_cast<uint8_t>(st.pending_mfg_key & 0xff));
    d.adv_payload.push_back(static_cast<uint8_t>((st.pending_mfg_key >> 8) & 0xff));
    d.adv_payload.insert(d.adv_payload.end(), st.mfg_bytes.begin(), st.mfg_bytes.end());
    merge_device(by_addr, std::move(d));
    st.in_mfg_value = false;
    st.mfg_bytes.clear();
}

bool parse_hex_byte_line(const std::string& t, std::vector<uint8_t>& out) {
    std::istringstream hs(t);
    std::string tok;
    bool any = false;
    while (hs >> tok) {
        // bluetoothctl may append ASCII dump after hex; stop at non-hex token.
        if (tok.size() > 2) break;
        bool ok = true;
        for (char c : tok) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                ok = false;
                break;
            }
        }
        if (!ok || tok.empty()) break;
        try {
            out.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
            any = true;
        } catch (...) {
            break;
        }
    }
    return any;
}

void ingest_scan_log_line(ScanLogState& st, std::map<std::string, BleDevice>& by_addr, const std::string& raw) {
    const std::string sl = strip_ansi(raw);
    const std::string t = trim_copy(sl);

    // Continuation of ManufacturerData.Value hex dump (no "Device" prefix).
    if (st.in_mfg_value) {
        if (t.empty() || t.find("Device ") != std::string::npos) {
            flush_pending_mfg(st, by_addr);
        } else if (!parse_hex_byte_line(t, st.mfg_bytes)) {
            flush_pending_mfg(st, by_addr);
        } else {
            return;
        }
    }

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

    if (sl.find("ManufacturerData.Key:") != std::string::npos ||
        sl.find("ManufacturerData Key:") != std::string::npos) {
        flush_pending_mfg(st, by_addr);
        st.pending_addr = addr;
        st.pending_mfg_key = -1;
        st.mfg_bytes.clear();
        st.in_mfg_value = false;
        const auto kpos = sl.find("0x");
        const auto kpos2 = sl.find("0X");
        const auto kp = (kpos != std::string::npos) ? kpos : kpos2;
        if (kp != std::string::npos) {
            try {
                st.pending_mfg_key = static_cast<int>(std::stoul(sl.substr(kp), nullptr, 0));
            } catch (...) {
                st.pending_mfg_key = -1;
            }
        } else if (parse_int_token(sl, st.pending_mfg_key)) {
            // fall through — may have parsed wrong number; prefer hex above
        }
        merge_device(by_addr, std::move(d));
        return;
    }

    if (sl.find("ManufacturerData.Value:") != std::string::npos ||
        sl.find("ManufacturerData Value:") != std::string::npos) {
        st.pending_addr = addr;
        st.in_mfg_value = true;
        st.mfg_bytes.clear();
        // Same-line hex after the colon, if any.
        const auto cpos = sl.find("Value:");
        if (cpos != std::string::npos) {
            const std::string after = trim_copy(sl.substr(cpos + 6));
            if (!after.empty()) parse_hex_byte_line(after, st.mfg_bytes);
        }
        merge_device(by_addr, std::move(d));
        return;
    }

    const auto rpos = sl.find("RSSI:");
    if (rpos != std::string::npos) {
        int rssi = -127;
        if (parse_int_token(sl.substr(rpos + 5), rssi)) d.rssi = rssi;
    }

    const auto uuid_pos = sl.find("UUIDs:");
    if (uuid_pos != std::string::npos) {
        d.services = trim_copy(sl.substr(uuid_pos + 6));
    }

    const auto name_pos = sl.find("Name:");
    if (name_pos != std::string::npos) {
        d.name = trim_copy(sl.substr(name_pos + 5));
    } else if (rest.size() > 18 && sl.find("NEW") != std::string::npos && sl.find("RSSI:") == std::string::npos &&
               sl.find("UUIDs:") == std::string::npos) {
        d.name = trim_copy(rest.substr(18));
        if (d.name.empty()) d.name = "(unknown)";
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
        if (d.address.size() >= 17 &&
            (d.rssi > -127 || !d.mfg_hex.empty() || !d.services.empty() || by_addr.count(d.address))) {
            merge_device(by_addr, std::move(d));
        }
    }
}

std::string blescan_helper_path() {
    static const char* kCandidates[] = {
        "/usr/lib/kvxputerzero/kvxputerzero-blescan",
        "/usr/local/lib/kvxputerzero/kvxputerzero-blescan",
    };
    for (const char* p : kCandidates) {
        struct stat st {};
        if (stat(p, &st) == 0 && (st.st_mode & S_IXUSR)) return p;
    }
    static const char* kDev[] = {
        "./kvxputerzero-blescan",
        "./Debug/kvxputerzero-blescan",
        "Debug/kvxputerzero-blescan",
    };
    for (const char* p : kDev) {
        struct stat st {};
        if (stat(p, &st) == 0 && (st.st_mode & S_IXUSR)) return p;
    }
    return {};
}

bool parse_hex_payload(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        try {
            out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        } catch (...) {
            out.clear();
            return false;
        }
    }
    return true;
}

bool ble_scan_via_helper(std::vector<BleDevice>& devices, std::string& error, int seconds, bool* cancel) {
    const std::string helper = blescan_helper_path();
    if (helper.empty()) {
        error = "no blescan helper";
        return false;
    }
    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }
    const std::string cmd = helper + " " + std::to_string(seconds) + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        error = "blescan helper failed to start";
        return false;
    }
    std::map<std::string, BleDevice> by_addr;
    char buf[1024];
    bool ok_line = false;
    while (fgets(buf, sizeof(buf), fp)) {
        if (cancel && *cancel) break;
        std::string line = trim_copy(buf);
        if (line.rfind("ADV ", 0) == 0) {
            std::istringstream iss(line.substr(4));
            std::string mac, payload_hex;
            int rssi = -127;
            unsigned addr_type = 1;
            if (!(iss >> mac >> rssi >> addr_type >> payload_hex)) continue;
            if (mac.size() < 17) continue;
            BleDevice d;
            d.address = mac;
            d.name = "(unknown)";
            d.rssi = rssi;
            d.addr_type = (addr_type == 0) ? "public" : "random";
            if (!parse_hex_payload(payload_hex, d.adv_payload)) continue;
            parse_payload_into(d);
            merge_device(by_addr, std::move(d));
        } else if (line.rfind("OK ", 0) == 0) {
            ok_line = true;
        } else if (line.rfind("ERR ", 0) == 0) {
            error = line.substr(4);
        }
    }
    const int rc = pclose(fp);
    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }
    if (rc != 0 && by_addr.empty()) {
        if (error.empty()) error = "blescan helper failed (reinstall package)";
        return false;
    }
    for (auto& kv : by_addr) {
        if (kv.second.name.empty()) kv.second.name = "(unknown)";
        devices.push_back(std::move(kv.second));
    }
    if (devices.empty()) {
        error = ok_line ? "no BLE adverts" : "blescan helper produced no ADV lines";
        return false;
    }
    error.clear();
    return true;
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

    // Cardputer-equivalent path: raw AD payloads via privileged helper.
    {
        std::string helper_err;
        if (ble_scan_via_helper(devices, helper_err, seconds, cancel)) return true;
        if (cancel && *cancel) {
            error = "cancelled";
            return false;
        }
        // Fall through to BlueZ discovery if helper is missing / unprivileged.
        devices.clear();
    }

    const std::string adapter = adapter_path();
    (void)run_capture(("busctl --system set-property org.bluez '" + adapter +
                       "' org.bluez.Adapter1 Powered b true 2>/dev/null")
                          .c_str());
    (void)run_capture(("busctl --system call org.bluez '" + adapter +
                       "' org.bluez.Adapter1 SetDiscoveryFilter a{sv} 2 Transport s le DuplicateData b true "
                       "2>/dev/null")
                          .c_str());

    std::map<std::string, BleDevice> by_addr;
    std::mutex mu;
    std::atomic<bool> scan_done{false};
    std::atomic<bool> sniff_stop{false};

    // HCI sniff reads the same LE advertising reports BlueZ receives (raw AD = NimBLE payload).
    std::thread sniff_thread([&]() { hci_sniff_loop(by_addr, mu, sniff_stop, cancel); });

    std::thread scan_thread([&]() {
        const std::string cmd = "bluetoothctl --timeout " + std::to_string(seconds) + " scan on 2>&1";
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            scan_done = true;
            return;
        }
        ScanLogState st;
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) {
            if (cancel && *cancel) break;
            std::lock_guard<std::mutex> lock(mu);
            ingest_scan_log_line(st, by_addr, buf);
        }
        {
            std::lock_guard<std::mutex> lock(mu);
            flush_pending_mfg(st, by_addr);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    {
        std::lock_guard<std::mutex> lock(mu);
        poll_dbus_devices(adapter, by_addr, cancel);
    }

    if (scan_thread.joinable()) scan_thread.join();
    sniff_stop = true;
    if (sniff_thread.joinable()) sniff_thread.join();

    (void)run_capture(("busctl --system call org.bluez '" + adapter +
                       "' org.bluez.Adapter1 StopDiscovery 2>/dev/null")
                          .c_str());

    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }

    const std::string text = run_capture("bluetoothctl devices 2>/dev/null");
    if (by_addr.empty() && text.empty()) {
        error = "BLE scan unavailable (bluetoothctl / BlueZ)";
        return false;
    }
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("Device ", 0) != 0) continue;
        const std::string addr = line.substr(7, 17);
        auto it = by_addr.find(addr);
        if (it == by_addr.end()) continue;
        if (line.size() > 25) {
            const std::string name = trim_copy(line.substr(25));
            if ((it->second.name.empty() || it->second.name == "(unknown)") && !name.empty()) {
                it->second.name = name;
            }
        }
    }

    for (auto& kv : by_addr) {
        if (kv.second.name.empty()) kv.second.name = "(unknown)";
        // If we only have mfg_hex / services from D-Bus, still try to synthesize payload helpers.
        if (kv.second.adv_payload.empty() && !kv.second.mfg_hex.empty()) {
            // leave empty — matchers also use mfg_hex / services
        }
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

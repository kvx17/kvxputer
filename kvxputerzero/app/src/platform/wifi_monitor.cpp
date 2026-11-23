#include "wifi_monitor.h"

#include "shell_util.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

constexpr uint8_t kChannels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr int kNch = 11;

std::string config_dir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) {
            const fs::path root = fs::path(xdg) / "kvxputerzero";
            fs::create_directories(root, ec);
            return root.string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) {
            const fs::path root = fs::path(home) / ".config" / "kvxputerzero";
            fs::create_directories(root, ec);
            return root.string();
        }
    }
    const fs::path root = fs::path(".") / "kvxputerzero-config";
    fs::create_directories(root, ec);
    return root.string();
}

std::string config_file(const char* name) {
    return (std::filesystem::path(config_dir()) / name).string();
}

std::string read_file_trim(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string sysfs_driver(const std::string& ifname) {
    const auto link = std::filesystem::path("/sys/class/net") / ifname / "device" / "driver";
    std::error_code ec;
    if (!std::filesystem::exists(link, ec)) return {};
    auto target = std::filesystem::read_symlink(link, ec);
    if (ec) return {};
    return target.filename().string();
}

void fill_usb_info(const std::string& ifname, std::string& product, std::string& vid_pid) {
    product.clear();
    vid_pid.clear();
    namespace fs = std::filesystem;
    fs::path device = fs::path("/sys/class/net") / ifname / "device";
    std::error_code ec;
    for (int i = 0; i < 8; ++i) {
        if (!fs::exists(device, ec)) break;
        const auto id_vendor = read_file_trim(device / "idVendor");
        const auto id_product = read_file_trim(device / "idProduct");
        if (!id_vendor.empty() && !id_product.empty()) {
            vid_pid = id_vendor + ":" + id_product;
            product = read_file_trim(device / "product");
            if (product.empty()) product = "USB " + vid_pid;
            return;
        }
        device = device.parent_path();
    }
}

bool iface_name_ok(const std::string& name) {
    if (name.empty() || name.size() > 15) return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

struct PhyInfo {
    std::string name;
    bool monitor{false};
};

std::vector<PhyInfo> parse_iw_phy(const std::string& text) {
    std::vector<PhyInfo> out;
    PhyInfo cur;
    bool in_modes = false;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("Wiphy ", 0) == 0) {
            if (!cur.name.empty()) out.push_back(cur);
            cur = {};
            cur.name = line.substr(6);
            while (!cur.name.empty() && cur.name.back() == ' ') cur.name.pop_back();
            in_modes = false;
            continue;
        }
        if (line.find("Supported interface modes:") != std::string::npos) {
            in_modes = true;
            continue;
        }
        if (in_modes) {
            if (!line.empty() && line[0] == '\t' && line.find('\t', 1) == std::string::npos &&
                line.find("Supported") == std::string::npos) {
                // next section at single-indent
                if (line.size() > 1 && line[1] != '\t' && line.find(':') != std::string::npos) {
                    in_modes = false;
                }
            }
            if (in_modes && line.find("monitor") != std::string::npos) cur.monitor = true;
            if (in_modes && !line.empty() && line[0] != '\t' && line[0] != ' ') in_modes = false;
        }
    }
    if (!cur.name.empty()) out.push_back(cur);
    return out;
}

struct DevInfo {
    std::string ifname;
    std::string phy;
};

std::vector<DevInfo> parse_iw_dev(const std::string& text) {
    std::vector<DevInfo> out;
    DevInfo cur;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // "phy#0"
        if (line.rfind("phy#", 0) == 0) {
            if (!cur.ifname.empty()) out.push_back(cur);
            cur = {};
            cur.phy = "phy" + line.substr(4);
            continue;
        }
        // "\tInterface wlan0"
        const auto pos = line.find("Interface ");
        if (pos != std::string::npos) {
            if (!cur.ifname.empty()) out.push_back(cur);
            DevInfo next;
            next.phy = cur.phy;
            next.ifname = line.substr(pos + 10);
            while (!next.ifname.empty() && (next.ifname.back() == '\r' || next.ifname.back() == ' '))
                next.ifname.pop_back();
            cur = next;
        }
    }
    if (!cur.ifname.empty()) out.push_back(cur);
    return out;
}

const PhyInfo* find_phy(const std::vector<PhyInfo>& phys, const std::string& name) {
    for (const auto& p : phys) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

} // namespace

std::vector<WifiMonitorIface> list_monitor_ifaces(std::string& error) {
    error.clear();
    std::string phy_out, dev_out;
    if (run_shell("iw phy 2>/dev/null", phy_out) != 0 || phy_out.empty()) {
        error = "iw unavailable or no wireless phys";
        return {};
    }
    run_shell("iw dev 2>/dev/null", dev_out);

    const auto phys = parse_iw_phy(phy_out);
    const auto devs = parse_iw_dev(dev_out);
    std::vector<WifiMonitorIface> out;

    for (const auto& d : devs) {
        WifiMonitorIface row;
        row.ifname = d.ifname;
        row.phy = d.phy;
        row.driver = sysfs_driver(d.ifname);
        fill_usb_info(d.ifname, row.usb_product, row.usb_vid_pid);

        const PhyInfo* phy = find_phy(phys, d.phy);
        const bool mon = phy && phy->monitor;

        if (row.driver == "brcmfmac" && !mon) {
            row.selectable = false;
            row.unavailable_reason = "onboard brcmfmac (no monitor)";
        } else if (row.driver.find("sr9800") != std::string::npos ||
                   row.driver.find("sr9900") != std::string::npos ||
                   row.driver == "cdc_ether") {
            row.selectable = false;
            row.unavailable_reason = "Ethernet (not wireless)";
        } else if (!mon) {
            row.selectable = false;
            row.unavailable_reason = "phy lacks monitor mode";
        } else if (row.driver == "brcmfmac") {
            // Stock image: do not use even if somehow advertised.
            row.selectable = false;
            row.unavailable_reason = "onboard brcmfmac (blocked)";
        } else {
            row.selectable = true;
        }
        out.push_back(std::move(row));
    }

    // Also list phys with no interface yet? Plan says one row per wireless interface.
    if (out.empty() && error.empty()) {
        error = "No monitor-capable USB adapter. Host mode switch must be toward the hub, "
                "and the stick's driver must already be loaded.";
    }
    return out;
}

std::string load_monitor_iface() {
    return read_file_trim(config_file("wifi_monitor_iface"));
}

void save_monitor_iface(const std::string& ifname) {
    const auto path = config_file("wifi_monitor_iface");
    std::ofstream out(path, std::ios::trunc);
    if (out) out << ifname << '\n';
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
}

bool monitor_iface_is_onboard(const std::string& ifname) {
    return ifname.empty() || ifname == "onboard" || ifname == "-";
}

bool monitor_iface_valid(const std::string& ifname, std::string& error) {
    error.clear();
    if (monitor_iface_is_onboard(ifname)) return true;
    if (!iface_name_ok(ifname)) {
        error = "invalid interface name";
        return false;
    }
    std::string enum_err;
    const auto list = list_monitor_ifaces(enum_err);
    for (const auto& r : list) {
        if (r.ifname == ifname) {
            if (!r.selectable) {
                error = r.unavailable_reason.empty() ? "iface not selectable" : r.unavailable_reason;
                return false;
            }
            return true;
        }
    }
    error = enum_err.empty() ? "saved adapter missing" : enum_err;
    return false;
}

std::string find_wifimon_helper() {
    const char* candidates[] = {
        "/usr/libexec/kvxputerzero/kvxputerzero-wifimon",
        "/usr/lib/kvxputerzero/kvxputerzero-wifimon",
        "/usr/bin/kvxputerzero-wifimon",
    };
    for (const char* c : candidates) {
        if (::access(c, X_OK) == 0) return c;
    }
    // Next to the running binary (dev builds).
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = 0;
        std::filesystem::path p(self);
        auto sibling = p.parent_path() / "kvxputerzero-wifimon";
        if (::access(sibling.c_str(), X_OK) == 0) return sibling.string();
    }
    return {};
}

std::string wifi_ap_mac_str(const uint8_t bssid[6]) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
                  bssid[3], bssid[4], bssid[5]);
    return buf;
}

std::string wifi_ap_auth_label(const WifiApEntry& ap) {
    const bool privacy = (ap.capab & 0x0010) != 0;
    if (!privacy && !ap.has_wpa && !ap.has_rsn) return "Open";
    if (ap.has_sae) return "WPA3";
    if (ap.has_rsn) return "WPA2";
    if (ap.has_wpa) return "WPA";
    if (privacy) return "WEP/Privacy";
    return "Unknown";
}

void wifi_analyzer_reset(WifiAnalyzerState& st) {
    st = {};
    st.dwell_ms = 350;
    st.nav_ch = 1;
    st.sweep_ch = 1;
    for (auto& r : st.rssi) r = -128;
}

void wifi_analyzer_set_dwell(WifiAnalyzerState& st, int dwell_ms) {
    dwell_ms = std::clamp(dwell_ms, 150, 1000);
    dwell_ms = (dwell_ms / 100) * 100;
    if (dwell_ms < 150) dwell_ms = 150;
    st.dwell_ms = static_cast<uint16_t>(dwell_ms);
}

void wifi_analyzer_toggle_lock(WifiAnalyzerState& st) {
    if (st.lock_ch == st.nav_ch) st.lock_ch = 0;
    else st.lock_ch = st.nav_ch;
}

void wifi_analyzer_finish_dwell(WifiAnalyzerState& st, uint32_t frame_bytes, uint32_t pkt_count,
                                int8_t peak_rssi, uint32_t elapsed_ms) {
    if (elapsed_ms < 20) elapsed_ms = 20;
    const uint32_t airtime_us = (frame_bytes * 8UL) / 6UL + pkt_count * 60UL;
    const uint32_t dwell_us = elapsed_ms * 1000UL;
    uint32_t load = dwell_us ? (airtime_us * 100UL / dwell_us) : 0;
    if (load > 100) load = 100;

    const uint8_t ch = st.sweep_ch;
    if (ch >= 1 && ch <= 11) {
        st.load[ch] = static_cast<uint8_t>(load);
        if (st.load[ch] > st.peak[ch]) st.peak[ch] = st.load[ch];
        st.rssi[ch] = (peak_rssi == -128) ? 0 : peak_rssi;
    }

    if (!st.lock_ch) {
        st.sweep_idx = (st.sweep_idx + 1) % kNch;
        st.sweep_ch = kChannels[st.sweep_idx];
    } else {
        st.sweep_ch = st.lock_ch;
    }
}

namespace {

bool parse_mac(const std::string& s, uint8_t out[6]) {
    unsigned v[6]{};
    if (std::sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(v[i]);
    return true;
}

} // namespace

void wifi_analyzer_apply_scan(WifiAnalyzerState& st, const std::vector<WifiAp>& aps) {
    // Preserve nav / dwell / lock / peaks across scans.
    const uint8_t nav = st.nav_ch;
    const uint16_t dwell = st.dwell_ms;
    const uint8_t lock = st.lock_ch;
    const auto peak = st.peak;

    for (auto& a : st.aps) a = {};
    st.ap_count = 0;
    for (int i = 0; i < 12; ++i) {
        st.load[i] = 0;
        st.rssi[i] = -128;
        st.peak[i] = peak[i];
    }

    uint16_t ch_count[12] = {};
    auto fill_ap = [](WifiApEntry& ap, const WifiAp& src, uint8_t ch) -> bool {
        ap = {};
        if (!parse_mac(src.bssid, ap.bssid)) return false;
        ap.used = true;
        ap.channel = ch;
        ap.rssi = static_cast<int8_t>(src.rssi);
        const size_t n = std::min<size_t>(32, src.ssid.size());
        if (n) {
            std::memcpy(ap.ssid, src.ssid.data(), n);
            ap.ssid[n] = 0;
            ap.ssid_len = static_cast<uint8_t>(n);
        }
        ap.hidden = src.ssid.empty();
        const std::string& sec = src.security_raw.empty() ? src.security : src.security_raw;
        ap.has_sae = sec.find("WPA3") != std::string::npos || sec.find("SAE") != std::string::npos;
        ap.has_rsn = ap.has_sae || sec.find("WPA2") != std::string::npos || sec.find("RSN") != std::string::npos;
        ap.has_wpa = sec.find("WPA1") != std::string::npos ||
                     (sec.find("WPA") != std::string::npos && !ap.has_rsn);
        ap.capab = src.open ? 0 : 0x10;
        return true;
    };

    for (const auto& src : aps) {
        if (src.channel < 1 || src.channel > 11) continue;
        const uint8_t ch = static_cast<uint8_t>(src.channel);
        ch_count[ch]++;
        if (src.rssi > st.rssi[ch]) st.rssi[ch] = static_cast<int8_t>(src.rssi);

        if (st.ap_count < WifiAnalyzerState::kApMax) {
            if (fill_ap(st.aps[st.ap_count], src, ch)) st.ap_count++;
            continue;
        }
        int weakest = 0;
        for (int a = 1; a < WifiAnalyzerState::kApMax; ++a) {
            if (st.aps[a].rssi < st.aps[weakest].rssi) weakest = a;
        }
        if (src.rssi <= st.aps[weakest].rssi) continue;
        (void)fill_ap(st.aps[weakest], src, ch);
    }

    for (int i = 0; i < kNch; ++i) {
        const uint8_t ch = kChannels[i];
        uint32_t load = ch_count[ch] * 12u;
        if (load > 100) load = 100;
        st.load[ch] = static_cast<uint8_t>(load);
        if (st.load[ch] > st.peak[ch]) st.peak[ch] = st.load[ch];
        if (st.rssi[ch] == -128) st.rssi[ch] = 0;
    }

    st.nav_ch = nav;
    st.dwell_ms = dwell;
    st.lock_ch = lock;
    if (lock) st.sweep_ch = lock;
    else {
        st.sweep_idx = (st.sweep_idx + 1) % kNch;
        st.sweep_ch = kChannels[st.sweep_idx];
    }
}

namespace {

void parse_ies(WifiApEntry& ap, const uint8_t* ie, uint8_t ie_len) {
    ap.has_wpa = false;
    ap.has_rsn = false;
    ap.has_sae = false;
    int pos = 0;
    while (pos + 1 < ie_len) {
        const uint8_t tag = ie[pos];
        const uint8_t tlen = ie[pos + 1];
        if (pos + 2 + tlen > ie_len) break;
        const uint8_t* val = ie + pos + 2;
        if (tag == 0) {
            if (tlen == 0) {
                ap.hidden = true;
            } else {
                const uint8_t n = tlen > 32 ? 32 : tlen;
                std::memcpy(ap.ssid, val, n);
                ap.ssid[n] = 0;
                ap.ssid_len = n;
                if (ap.hidden || ap.revealed[0] == 0) {
                    std::memcpy(ap.revealed, ap.ssid, n + 1);
                }
            }
        } else if (tag == 3 && tlen >= 1) {
            ap.channel = val[0];
        } else if (tag == 0x30) {
            ap.has_rsn = true;
            if (tlen >= 8) {
                for (int i = 0; i + 3 < tlen; ++i) {
                    if (val[i] == 0x00 && val[i + 1] == 0x0F && val[i + 2] == 0xAC && val[i + 3] == 0x08) {
                        ap.has_sae = true;
                        break;
                    }
                }
            }
        } else if (tag == 0xDD && tlen >= 4) {
            if (val[0] == 0x00 && val[1] == 0x50 && val[2] == 0xF2 && val[3] == 0x01) ap.has_wpa = true;
        }
        pos += 2 + tlen;
    }
}

} // namespace

void wifi_analyzer_ingest_frame(WifiAnalyzerState& st, const uint8_t* frame, size_t len, int8_t rssi_dbm,
                                uint8_t radiotap_ch, uint32_t& out_bytes, uint32_t& out_pkts,
                                int8_t& out_peak_rssi) {
    out_bytes += static_cast<uint32_t>(len);
    out_pkts += 1;
    if (rssi_dbm > out_peak_rssi) out_peak_rssi = rssi_dbm;

    if (len < 36) return;
    const uint8_t ftype = frame[0];
    if (ftype != 0x80 && ftype != 0x50) return;

    uint8_t bssid[6];
    std::memcpy(bssid, frame + 16, 6);
    const uint16_t beacon_interval =
        static_cast<uint16_t>(frame[32]) | (static_cast<uint16_t>(frame[33]) << 8);
    const uint16_t capab = static_cast<uint16_t>(frame[34]) | (static_cast<uint16_t>(frame[35]) << 8);

    int ie_off = 36;
    int ie_avail = static_cast<int>(len) - ie_off;
    if (ie_avail < 0) ie_avail = 0;
    if (ie_avail > 96) ie_avail = 96;

    int found = -1;
    for (int a = 0; a < WifiAnalyzerState::kApMax; ++a) {
        if (!st.aps[a].used) continue;
        if (std::memcmp(st.aps[a].bssid, bssid, 6) == 0) {
            found = a;
            break;
        }
    }
    if (found < 0) {
        if (st.ap_count >= WifiAnalyzerState::kApMax) {
            int weakest = 0;
            for (int a = 1; a < WifiAnalyzerState::kApMax; ++a) {
                if (st.aps[a].rssi < st.aps[weakest].rssi) weakest = a;
            }
            found = weakest;
        } else {
            for (int a = 0; a < WifiAnalyzerState::kApMax; ++a) {
                if (!st.aps[a].used) {
                    found = a;
                    st.ap_count++;
                    break;
                }
            }
        }
    }
    if (found < 0) return;

    WifiApEntry& ap = st.aps[found];
    const bool was_new = !ap.used;
    ap.used = true;
    std::memcpy(ap.bssid, bssid, 6);
    ap.rssi = rssi_dbm;
    if (radiotap_ch) ap.channel = radiotap_ch;
    ap.beacon_interval = beacon_interval;
    ap.capab = capab;
    if (was_new) {
        ap.ssid[0] = 0;
        ap.ssid_len = 0;
        ap.revealed[0] = 0;
        ap.hidden = false;
    }
    if (ie_avail > 0) parse_ies(ap, frame + ie_off, static_cast<uint8_t>(ie_avail));
}

bool wifi_parse_radiotap(const uint8_t* pkt, size_t len, size_t& header_len, int8_t& rssi_dbm,
                         uint8_t& channel) {
    header_len = 0;
    rssi_dbm = -128;
    channel = 0;
    if (len < 8) return false;
    if (pkt[0] != 0) return false; // version
    const uint16_t it_len = static_cast<uint16_t>(pkt[2]) | (static_cast<uint16_t>(pkt[3]) << 8);
    if (it_len < 8 || it_len > len) return false;
    header_len = it_len;

    uint32_t present = static_cast<uint32_t>(pkt[4]) | (static_cast<uint32_t>(pkt[5]) << 8) |
                       (static_cast<uint32_t>(pkt[6]) << 16) | (static_cast<uint32_t>(pkt[7]) << 24);
    size_t off = 8;
    // Skip extended present bitmasks
    uint32_t cur = present;
    while (cur & (1u << 31)) {
        if (off + 4 > it_len) return false;
        cur = static_cast<uint32_t>(pkt[off]) | (static_cast<uint32_t>(pkt[off + 1]) << 8) |
              (static_cast<uint32_t>(pkt[off + 2]) << 16) | (static_cast<uint32_t>(pkt[off + 3]) << 24);
        off += 4;
    }

    auto align = [&](size_t a) {
        const size_t m = off % a;
        if (m) off += a - m;
    };

    // Fields in radiotap order for bit 0..15 we care about
    if (present & (1u << 0)) { // TSFT
        align(8);
        off += 8;
    }
    if (present & (1u << 1)) { // Flags
        align(1);
        off += 1;
    }
    if (present & (1u << 2)) { // Rate
        align(1);
        off += 1;
    }
    if (present & (1u << 3)) { // Channel
        align(2);
        if (off + 4 <= it_len) {
            const uint16_t freq = static_cast<uint16_t>(pkt[off]) | (static_cast<uint16_t>(pkt[off + 1]) << 8);
            if (freq >= 2412 && freq <= 2484) {
                channel = static_cast<uint8_t>((freq - 2407) / 5);
                if (freq == 2484) channel = 14;
            }
        }
        off += 4;
    }
    if (present & (1u << 4)) { // FHSS
        align(2);
        off += 2;
    }
    if (present & (1u << 5)) { // Antenna signal (dBm)
        align(1);
        if (off < it_len) rssi_dbm = static_cast<int8_t>(pkt[off]);
        off += 1;
    }
    return true;
}

std::vector<std::string> wifi_ap_row_labels(const WifiAnalyzerState& st, uint8_t filter_ch) {
    std::vector<std::string> rows;
    for (int i = 0; i < WifiAnalyzerState::kApMax; ++i) {
        if (!st.aps[i].used) continue;
        if (filter_ch && st.aps[i].channel != filter_ch) continue;
        const char* ssid = st.aps[i].ssid_len == 0 ? "<hidden>" : st.aps[i].ssid;
        char line[80];
        std::snprintf(line, sizeof(line), "ch%u %s %ddBm", static_cast<unsigned>(st.aps[i].channel), ssid,
                      static_cast<int>(st.aps[i].rssi));
        rows.emplace_back(line);
    }
    return rows;
}

int wifi_ap_index_at_row(const WifiAnalyzerState& st, uint8_t filter_ch, int row) {
    int n = 0;
    for (int i = 0; i < WifiAnalyzerState::kApMax; ++i) {
        if (!st.aps[i].used) continue;
        if (filter_ch && st.aps[i].channel != filter_ch) continue;
        if (n == row) return i;
        ++n;
    }
    return -1;
}

std::vector<std::pair<std::string, std::string>> wifi_ap_detail_fields(const WifiApEntry& ap) {
    std::vector<std::pair<std::string, std::string>> f;
    f.push_back({"SSID", ap.ssid_len == 0 ? "<hidden>" : ap.ssid});
    if (ap.hidden && ap.revealed[0]) f.push_back({"Revealed", ap.revealed});
    f.push_back({"BSSID", wifi_ap_mac_str(ap.bssid)});
    f.push_back({"Channel", std::to_string(ap.channel)});
    f.push_back({"RSSI", std::to_string(ap.rssi) + " dBm"});
    f.push_back({"Auth", wifi_ap_auth_label(ap)});
    f.push_back({"Beacon int", std::to_string(ap.beacon_interval) + " TU"});
    f.push_back({"Hidden", (ap.hidden || ap.ssid_len == 0) ? "yes" : "no"});
    return f;
}

bool wifi_analyzer_export_csv(const WifiAnalyzerState& st, std::string& path_out, std::string& error) {
    error.clear();
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &tmv);
    path_out = (std::filesystem::path(config_dir()) / (std::string("wifi.analyzer-") + stamp + ".csv")).string();
    std::ofstream out(path_out);
    if (!out) {
        error = "cannot write CSV";
        return false;
    }
    out << "section,ch,load,peak,rssi\n";
    for (int ch = 1; ch <= 11; ++ch) {
        out << "channel," << ch << ',' << static_cast<int>(st.load[ch]) << ','
            << static_cast<int>(st.peak[ch]) << ',' << static_cast<int>(st.rssi[ch]) << '\n';
    }
    out << "\nsection,ssid,bssid,ch,rssi,auth,hidden\n";
    std::vector<int> order;
    for (int i = 0; i < WifiAnalyzerState::kApMax; ++i) {
        if (st.aps[i].used) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return st.aps[a].rssi > st.aps[b].rssi; });
    for (int i : order) {
        const auto& ap = st.aps[i];
        out << "ap,\"" << (ap.ssid_len ? ap.ssid : "") << "\"," << wifi_ap_mac_str(ap.bssid) << ','
            << static_cast<int>(ap.channel) << ',' << static_cast<int>(ap.rssi) << ','
            << wifi_ap_auth_label(ap) << ',' << ((ap.hidden || ap.ssid_len == 0) ? "true" : "false") << '\n';
    }
    return true;
}

WifiMonitorSession::~WifiMonitorSession() { stop(); }

bool WifiMonitorSession::start(const std::string& ifname, std::string& error) {
    error.clear();
    stop();
    if (!iface_name_ok(ifname)) {
        error = "invalid interface name";
        return false;
    }
    const std::string helper = find_wifimon_helper();
    if (helper.empty()) {
        error = "kvxputerzero-wifimon missing (reinstall kvxputerzero package)";
        return false;
    }

    int in_pipe[2]{-1, -1};
    int out_pipe[2]{-1, -1};
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) {
        error = "pipe failed";
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork failed";
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::execl(helper.c_str(), "kvxputerzero-wifimon", ifname.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    child_ = pid;
    stdin_fd_ = in_pipe[1];
    stdout_fd_ = out_pipe[0];
    fcntl(stdout_fd_, F_SETFL, fcntl(stdout_fd_, F_GETFL, 0) | O_NONBLOCK);
    running_ = true;
    pending_.clear();

    // Wait briefly for "OK\n" or error line; keep any leftover for frame reads.
    std::string banner;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[256];
        const ssize_t n = ::read(stdout_fd_, buf, sizeof(buf));
        if (n > 0) {
            banner.append(buf, buf + n);
            if (banner.find('\n') != std::string::npos) break;
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
        ::usleep(20000);
    }
    const auto nl = banner.find('\n');
    std::string first = (nl == std::string::npos) ? banner : banner.substr(0, nl);
    if (nl != std::string::npos && nl + 1 < banner.size()) {
        pending_.assign(banner.begin() + static_cast<std::ptrdiff_t>(nl + 1), banner.end());
    }
    while (!first.empty() && (first.back() == '\r' || first.back() == ' ')) first.pop_back();
    if (first != "OK") {
        error = first.empty()
                    ? ("helper failed (reinstall package if privileges missing): " + helper)
                    : first;
        stop();
        return false;
    }
    return true;
}

bool WifiMonitorSession::read_exact(uint8_t* dst, size_t len, int timeout_ms, std::string& error) {
    size_t got = 0;
    while (got < len && !pending_.empty()) {
        dst[got++] = pending_.front();
        pending_.erase(pending_.begin());
    }
    while (got < len) {
        pollfd pfd{};
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, timeout_ms);
        if (pr == 0) {
            error = "timeout";
            return false;
        }
        if (pr < 0) {
            error = "poll failed";
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            error = "helper closed";
            return false;
        }
        const ssize_t n = ::read(stdout_fd_, dst + got, len - got);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            error = "helper EOF";
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        error = "read failed";
        return false;
    }
    return true;
}

void WifiMonitorSession::restore_child() {
    if (stdin_fd_ >= 0) {
        const char q[] = "Q\n";
        (void)::write(stdin_fd_, q, sizeof(q) - 1);
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (child_ > 0) {
        int status = 0;
        for (int i = 0; i < 50; ++i) {
            const pid_t r = ::waitpid(child_, &status, WNOHANG);
            if (r == child_) break;
            if (i == 40) ::kill(child_, SIGTERM);
            if (i == 45) ::kill(child_, SIGKILL);
            ::usleep(20000);
        }
        child_ = -1;
    }
    running_ = false;
}

void WifiMonitorSession::stop() {
    if (!running_ && child_ < 0 && stdin_fd_ < 0 && stdout_fd_ < 0) return;
    restore_child();
}

bool WifiMonitorSession::set_channel(uint8_t ch) {
    if (!running_ || stdin_fd_ < 0) return false;
    if (ch < 1 || ch > 11) return false;
    char cmd[32];
    const int n = std::snprintf(cmd, sizeof(cmd), "C %u\n", static_cast<unsigned>(ch));
    return ::write(stdin_fd_, cmd, static_cast<size_t>(n)) == n;
}

bool WifiMonitorSession::read_frame(std::vector<uint8_t>& out, int timeout_ms, std::string& error) {
    out.clear();
    error.clear();
    if (!running_ || stdout_fd_ < 0) {
        error = "not running";
        return false;
    }
    if (pending_.empty()) {
        pollfd pfd{};
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, timeout_ms);
        if (pr == 0) return false;
        if (pr < 0) {
            error = "poll failed";
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            error = "helper closed";
            return false;
        }
    }

    uint8_t hdr[4];
    if (!read_exact(hdr, 4, timeout_ms, error)) {
        if (error == "timeout") {
            error.clear();
            return false;
        }
        return false;
    }
    const uint32_t flen = static_cast<uint32_t>(hdr[0]) | (static_cast<uint32_t>(hdr[1]) << 8) |
                          (static_cast<uint32_t>(hdr[2]) << 16) | (static_cast<uint32_t>(hdr[3]) << 24);
    if (flen == 0 || flen > 65536) {
        error = "bad frame length";
        return false;
    }
    out.resize(flen);
    if (!read_exact(out.data(), flen, timeout_ms, error)) return false;
    return true;
}

} // namespace platform

#include "wifi_scan.h"

#include <chrono>
#include <cstdio>
#include <sstream>

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

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

int freq_to_channel(int mhz) {
    if (mhz == 2484) return 14;
    if (mhz >= 2412 && mhz <= 2472) return (mhz - 2407) / 5;
    if (mhz >= 5160 && mhz <= 5885) return (mhz - 5000) / 5;
    return -1;
}

int channel_from_fields(const std::string& chan, const std::string& freq) {
    try {
        const int ch = std::stoi(chan);
        if (ch >= 1 && ch <= 196) return ch;
    } catch (...) {
    }
    try {
        return freq_to_channel(std::stoi(freq));
    } catch (...) {
        return -1;
    }
}

bool nmcli_ok(const std::string& cmd, std::string& error) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        error = "nmcli unavailable";
        return false;
    }
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    const int rc = pclose(fp);
    if (rc != 0) {
        error = out.empty() ? "nmcli failed" : out;
        return false;
    }
    return true;
}

} // namespace

bool nmcli_error(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const size_t end = text.find('\n', i);
        const std::string line = text.substr(i, end == std::string::npos ? std::string::npos : end - i);
        i = end == std::string::npos ? text.size() : end + 1;
        if (line.empty()) continue;
        return line.rfind("Error", 0) == 0;
    }
    return false;
}

std::vector<WifiAp> wifi_scan(std::string& error, bool* cancel) {
    error.clear();
    if (cancel && *cancel) {
        error = "cancelled";
        return {};
    }
    // `list` alone returns NetworkManager's cache. While associated that cache is
    // often only the channel of the network we are joined to. --rescan yes asks
    // the radio to visit the other channels. Don't do it on every poll.
    using clock = std::chrono::steady_clock;
    static clock::time_point last_rescan{};
    const auto now = clock::now();
    const bool force = last_rescan.time_since_epoch().count() == 0 || now - last_rescan >= std::chrono::seconds(8);
    // CHAN and FREQ come before BSSID so a colon inside the MAC cannot shift the channel.
    const char* fields = "nmcli -w 10 -e yes -t -f CHAN,FREQ,SIGNAL,SSID,BSSID,SECURITY device wifi list --rescan ";
    std::string text;
    if (force) {
        last_rescan = now;
        text = run_capture((std::string(fields) + "yes 2>&1").c_str());
        if (nmcli_error(text)) text.clear();
    }
    if (text.empty()) {
        text = run_capture((std::string(fields) + "no 2>&1").c_str());
    }
    if (text.empty() || nmcli_error(text)) {
        error = text.empty() ? "Wi-Fi scan unavailable (nmcli)" : text;
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
        return {};
    }
    std::vector<WifiAp> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (cancel && *cancel) break;
        if (line.empty()) continue;
        if (line[0] != ':' && (line[0] < '0' || line[0] > '9')) continue;
        WifiAp ap;
        std::vector<std::string> parts;
        std::string cur;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == ':') {
                cur.push_back(':');
                ++i;
            } else if (line[i] == ':') {
                parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(line[i]);
            }
        }
        parts.push_back(cur);
        if (parts.size() < 5) continue;
        ap.channel = channel_from_fields(parts[0], parts[1]);
        try {
            // nmcli SIGNAL is 0-100; approximate RSSI
            const int pct = std::stoi(parts[2]);
            ap.rssi = -100 + pct / 2;
        } catch (...) {
            ap.rssi = -127;
        }
        ap.ssid = parts[3];
        ap.bssid = parts[4];
        std::string raw;
        for (size_t i = 5; i < parts.size(); ++i) {
            if (!raw.empty()) raw.push_back(':');
            raw += parts[i];
        }
        ap.security_raw = raw;
        ap.open = raw.empty() || raw == "--" || raw == " ";
        ap.security = ap.open ? "open" : "other";
        out.push_back(std::move(ap));
    }
    return out;
}

bool wifi_connect_open(const std::string& ssid, std::string& error) {
    error.clear();
    if (ssid.empty()) {
        error = "empty SSID";
        return false;
    }
    const std::string cmd = "nmcli device wifi connect " + shell_quote(ssid) + " 2>&1";
    return nmcli_ok(cmd, error);
}

bool wifi_connect_psk(const std::string& ssid, const std::string& psk, std::string& error) {
    error.clear();
    if (ssid.empty()) {
        error = "empty SSID";
        return false;
    }
    if (psk.empty()) {
        error = "empty password";
        return false;
    }
    // Password stays on the nmcli argv; do not write it to a world-readable file.
    const std::string cmd = "nmcli device wifi connect " + shell_quote(ssid) + " password " +
                            shell_quote(psk) + " 2>&1";
    return nmcli_ok(cmd, error);
}

bool wifi_disconnect(std::string& error) {
    error.clear();
    // Prefer the active Wi-Fi device; fall back to "wifi" device type.
    const std::string text = run_capture("nmcli -t -f DEVICE,TYPE,STATE device status 2>/dev/null");
    std::string wifi_dev;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // DEVICE:TYPE:STATE
        std::string device, type, state;
        std::string cur;
        int field = 0;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == ':') {
                if (field == 0) device = cur;
                else if (field == 1) type = cur;
                else if (field == 2) state = cur;
                cur.clear();
                ++field;
            } else {
                cur.push_back(line[i]);
            }
        }
        if (field == 2) state = cur;
        if (type == "wifi") {
            wifi_dev = device;
            if (state.find("connected") != std::string::npos) break;
        }
    }
    if (wifi_dev.empty()) {
        error = "no Wi-Fi device";
        return false;
    }
    return nmcli_ok("nmcli device disconnect " + shell_quote(wifi_dev) + " 2>&1", error);
}

bool wifi_radio_off(std::string& error) {
    error.clear();
    return nmcli_ok("nmcli radio wifi off 2>&1", error);
}

bool wifi_start_hotspot(const std::string& ssid, const std::string& psk, std::string& error) {
    error.clear();
    if (ssid.empty()) {
        error = "empty SSID";
        return false;
    }
    if (psk.size() < 8) {
        error = "password min 8 chars";
        return false;
    }
    // Creates/activates a connection named "Hotspot" by default.
    const std::string cmd = "nmcli device wifi hotspot ssid " + shell_quote(ssid) + " password " +
                            shell_quote(psk) + " 2>&1";
    return nmcli_ok(cmd, error);
}

bool wifi_stop_hotspot(std::string& error) {
    error.clear();
    // Prefer the connection nmcli names "Hotspot"; fall back to any wifi AP mode connection.
    if (nmcli_ok("nmcli -w 8 connection down Hotspot 2>&1", error)) return true;
    const std::string text =
        run_capture("nmcli -t -f NAME,TYPE,DEVICE connection show --active 2>/dev/null");
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // NAME:TYPE:DEVICE — TYPE may be 802-11-wireless
        const auto c1 = line.find(':');
        if (c1 == std::string::npos) continue;
        const auto c2 = line.find(':', c1 + 1);
        if (c2 == std::string::npos) continue;
        const std::string name = line.substr(0, c1);
        const std::string type = line.substr(c1 + 1, c2 - c1 - 1);
        if (type.find("wireless") == std::string::npos && type.find("wifi") == std::string::npos) continue;
        if (name.empty()) continue;
        error.clear();
        if (nmcli_ok("nmcli -w 8 connection down " + shell_quote(name) + " 2>&1", error)) return true;
    }
    if (error.empty()) error = "no hotspot connection";
    return false;
}

} // namespace platform

#include "wifi_scan.h"

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

std::vector<WifiAp> wifi_scan(std::string& error, bool* cancel) {
    error.clear();
    if (cancel && *cancel) {
        error = "cancelled";
        return {};
    }
    // nmcli -t -f SSID,BSSID,CHAN,SIGNAL,SECURITY device wifi list
    const std::string text =
        run_capture("nmcli -t -f SSID,BSSID,CHAN,SIGNAL,SECURITY device wifi list 2>/dev/null");
    if (text.empty()) {
        error = "Wi-Fi scan unavailable (nmcli)";
        return {};
    }
    std::vector<WifiAp> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (cancel && *cancel) break;
        if (line.empty()) continue;
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
        ap.ssid = parts[0];
        ap.bssid = parts[1];
        try {
            ap.channel = std::stoi(parts[2]);
        } catch (...) {
            ap.channel = -1;
        }
        try {
            // nmcli SIGNAL is 0-100; approximate RSSI
            const int pct = std::stoi(parts[3]);
            ap.rssi = -100 + pct / 2;
        } catch (...) {
            ap.rssi = -127;
        }
        std::string raw;
        for (size_t i = 4; i < parts.size(); ++i) {
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

} // namespace platform

#include "rf_settings.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace platform {
namespace {

namespace fs = std::filesystem;

std::string config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) return std::string(xdg) + "/kvxputerzero";
    }
    if (const char* h = std::getenv("HOME")) {
        if (*h) return std::string(h) + "/.config/kvxputerzero";
    }
    return "/tmp/kvxputerzero";
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

void parse_hz_list(const std::string& v, std::vector<uint32_t>& out) {
    out.clear();
    std::istringstream iss(v);
    std::string tok;
    while (std::getline(iss, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) continue;
        out.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 10)));
    }
}

std::string join_hz_list(const std::vector<uint32_t>& v) {
    std::ostringstream oss;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) oss << ',';
        oss << v[i];
    }
    return oss.str();
}

} // namespace

std::string rf_dir() {
    const fs::path p = fs::path(config_dir()) / "rf";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string rf_format_mhz(uint32_t hz) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", hz / 1000000.0);
    return buf;
}

std::string rf_auto_name(uint32_t hz, const char* mod_tag) {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%u_%s_%04d%02d%02d_%02d%02d%02d.sub", hz, mod_tag ? mod_tag : "OOK",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

RfSettings load_rf_settings() {
    RfSettings s;
    // Legacy single-line freq
    {
        std::ifstream in(config_dir() + "/rf_freq_hz.txt");
        std::string v;
        if (in && std::getline(in, v)) {
            v = trim(v);
            if (!v.empty()) s.freq_hz = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        }
    }
    const std::string path = (fs::path(config_dir()) / "rf" / "settings.txt").string();
    std::ifstream in(path);
    if (!in) return s;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "freq_hz") s.freq_hz = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "step_hz") s.step_hz = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "ref_dbm") s.ref_level_dbm = std::atoi(val.c_str());
        else if (key == "floor_dbm") s.floor_dbm = std::atoi(val.c_str());
        else if (key == "threshold_dbm") s.threshold_dbm = std::atoi(val.c_str());
        else if (key == "region") s.region = (val == "US" || val == "us") ? RfRegion::Us : RfRegion::Eu;
        else if (key == "modulation") s.modulation = (val == "FSK" || val == "fsk") ? RfModHint::Fsk : RfModHint::Ook;
        else if (key == "pa") {
            const int p = std::atoi(val.c_str());
            s.pa = static_cast<Cc1101PaLevel>(std::clamp(p, 0, 3));
        } else if (key == "tx_ms") s.tx_duration_ms = std::clamp(std::atoi(val.c_str()), 1, 2000);
        else if (key == "record_max_ms") s.record_max_ms = std::clamp(std::atoi(val.c_str()), 500, 30000);
        else if (key == "recents") parse_hz_list(val, s.recents);
        else if (key == "favourites") parse_hz_list(val, s.favourites);
    }
    return s;
}

void save_rf_settings(const RfSettings& s) {
    (void)rf_dir();
    {
        std::ofstream out(config_dir() + "/rf_freq_hz.txt", std::ios::trunc);
        if (out) out << s.freq_hz << "\n";
    }
    const std::string path = (fs::path(config_dir()) / "rf" / "settings.txt").string();
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "freq_hz=" << s.freq_hz << "\n";
    out << "step_hz=" << s.step_hz << "\n";
    out << "ref_dbm=" << s.ref_level_dbm << "\n";
    out << "floor_dbm=" << s.floor_dbm << "\n";
    out << "threshold_dbm=" << s.threshold_dbm << "\n";
    out << "region=" << (s.region == RfRegion::Us ? "US" : "EU") << "\n";
    out << "modulation=" << (s.modulation == RfModHint::Fsk ? "FSK" : "OOK") << "\n";
    out << "pa=" << static_cast<int>(s.pa) << "\n";
    out << "tx_ms=" << s.tx_duration_ms << "\n";
    out << "record_max_ms=" << s.record_max_ms << "\n";
    out << "recents=" << join_hz_list(s.recents) << "\n";
    out << "favourites=" << join_hz_list(s.favourites) << "\n";
}

void rf_push_recent(RfSettings& s, uint32_t hz) {
    s.recents.erase(std::remove(s.recents.begin(), s.recents.end(), hz), s.recents.end());
    s.recents.insert(s.recents.begin(), hz);
    if (s.recents.size() > 8) s.recents.resize(8);
}

bool rf_tx_allowed(RfRegion region, uint32_t hz, std::string& reason) {
    reason.clear();
    if (region == RfRegion::Eu) {
        if ((hz >= 433050000u && hz <= 434790000u) || (hz >= 868000000u && hz <= 868600000u)) return true;
        reason = "EU TX: 433.05–434.79 or 868.0–868.6 MHz only";
        return false;
    }
    // US
    if (hz >= 902000000u && hz <= 928000000u) return true;
    reason = "US TX: 902–928 MHz only";
    return false;
}

Cc1101PaLevel rf_clamp_pa(RfRegion region, Cc1101PaLevel want) {
    (void)region;
    // Keep TX power modest regardless of region.
    if (want == Cc1101PaLevel::Max) return Cc1101PaLevel::Mid;
    return want;
}

} // namespace platform

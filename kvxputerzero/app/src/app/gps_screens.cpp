#include "gps_screens.h"

#include "audio.h"
#include "backlight.h"
#include "ble_scan.h"
#include "device_status.h"
#include "doc_screen.h"
#include "gps_fix.h"
#include "gps_wd_config.h"
#include "info_screen.h"
#include "options_list_screen.h"
#include "prompt_screen.h"
#include "result_list_screen.h"
#include "run_screen.h"
#include "shell_util.h"
#include "user_paths.h"
#include "wardrive_screen.h"
#include "wifi_scan.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unordered_set>
#include <vector>

namespace app {
namespace {

namespace fs = std::filesystem;
using model::ToolId;
using screen::MenuEntry;
using VM = viewmodel::BaseViewModel;
using Screen = std::unique_ptr<screen::KvxScreen>;
using Cancel = std::atomic<bool>;
using Log = std::function<void(const std::string&)>;
using Status = std::function<void(const std::string&)>;
using Worker = screen::RunScreen::WorkerFn;

struct Stage {
    std::string name;
    std::string arg;
};

Stage parse_stage(const std::string& p) {
    const auto c = p.find(':');
    if (c == std::string::npos) return {p, {}};
    return {p.substr(0, c), p.substr(c + 1)};
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string sfmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::string& path, const std::string& text, mode_t mode = 0644) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    out.close();
#if !defined(_WIN32)
    ::chmod(path.c_str(), mode);
#else
    (void)mode;
#endif
    return true;
}

void nav(VM& vm, ToolId id, const std::string& stage = {}, const std::string& arg = {}) {
    const auto prev = vm.current_page();
    std::string param = stage;
    if (!arg.empty()) {
        if (!param.empty()) param += ":";
        param += arg;
    }
    vm.open_tool(id, param);
    if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
}

Screen make_options(VM& vm, AssetManager& assets, const std::string& title, std::vector<MenuEntry> entries) {
    if (entries.empty()) entries.push_back({"(empty)", [&vm]() { vm.go_back(); }});
    return std::make_unique<screen::OptionsListScreen>(vm, assets, title, std::move(entries));
}

Screen make_info(VM& vm, AssetManager& assets, const std::string& title, const std::string& text) {
    return std::make_unique<screen::DocScreen>(vm, assets, title, text);
}

Screen make_prompt(VM& vm, AssetManager& assets, const std::string& title, const std::string& hint,
                   const std::string& initial, screen::PromptScreen::ConfirmFn fn) {
    return std::make_unique<screen::PromptScreen>(vm, assets, title, hint, initial, std::move(fn));
}

Screen make_run(VM& vm, AssetManager& assets, const std::string& title, Worker worker) {
    return std::make_unique<screen::RunScreen>(vm, assets, title, std::move(worker));
}

Screen make_results(VM& vm, AssetManager& assets, const std::string& title, std::vector<std::string> lines) {
    return std::make_unique<screen::ResultListScreen>(vm, assets, title, std::move(lines));
}

bool* cancel_ptr(Cancel& c) { return reinterpret_cast<bool*>(&c); }

bool sleep_cancel(Cancel& cancel, int ms) {
    int left = ms;
    while (left > 0 && !cancel) {
        const int step = std::min(left, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        left -= step;
    }
    return !cancel;
}

std::string gps_dir() {
    const fs::path p = fs::path(tools::config_dir()) / "gps";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string gps_file(const char* name) { return (fs::path(gps_dir()) / name).string(); }

std::string captures_dir() {
    const std::string saved = trim(read_text_file(tools::config_file("captures_path.txt")));
    if (!saved.empty()) return saved;
    const fs::path p = fs::path(tools::config_dir()) / "captures";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string tracks_dir() {
    const fs::path p = fs::path(gps_dir()) / "tracks";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string load_source() {
    const std::string v = trim(read_text_file(gps_file("source.txt")));
    return v.empty() ? "auto" : v;
}

std::string load_nmea_tty() {
    std::string v = trim(read_text_file(gps_file("nmea_tty.txt")));
    if (!v.empty()) return v;
    v = trim(read_text_file(tools::config_file("gps_nmea_tty.txt")));
    return v.empty() ? "/dev/serial0" : v;
}

int load_baud() {
    const std::string v = trim(read_text_file(gps_file("baud.txt")));
    if (v.empty()) return 115200;
    try {
        const int b = std::stoi(v);
        return b > 0 ? b : 115200;
    } catch (...) {
        return 115200;
    }
}

std::string load_hardware_raw() {
    const std::string v = trim(read_text_file(gps_file("hardware.txt")));
    return v.empty() ? "unit_v11" : v;
}

platform::GpsHardware load_hardware() { return platform::gps_hardware_from_string(load_hardware_raw()); }

platform::WdConfig load_wd_cfg() {
    platform::WdConfig c;
    platform::wd_config_load(gps_file("wardriverconfig.txt"), c);
    return c;
}

bool save_wd_cfg(const platform::WdConfig& c) { return platform::wd_config_save(gps_file("wardriverconfig.txt"), c); }

std::string wigle_token() { return trim(read_text_file(gps_file("wigle_token.txt"))); }

bool start_gps_session(platform::GpsSession& session, std::string& err) {
    return session.start(platform::gps_source_from_string(load_source()), load_nmea_tty(), load_baud(), err,
                         load_hardware());
}

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371000.0;
    const double p1 = lat1 * M_PI / 180.0;
    const double p2 = lat2 * M_PI / 180.0;
    const double dphi = (lat2 - lat1) * M_PI / 180.0;
    const double dl = (lon2 - lon1) * M_PI / 180.0;
    const double a = std::sin(dphi / 2) * std::sin(dphi / 2) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

int channel_freq(int ch) {
    if (ch >= 1 && ch <= 13) return 2407 + ch * 5;
    if (ch == 14) return 2484;
    if (ch >= 36 && ch <= 165) return 5000 + ch * 5;
    return 0;
}

std::string csv_escape(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    if (s.find_first_of(",\"") == std::string::npos) return s;
    std::string quoted = "\"";
    for (char c : s) {
        if (c == '"') quoted += "\"\"";
        else quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::string next_wd_filename(const std::string& dir) {
    int max_n = 0;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const std::string n = de.path().filename().string();
        if (n.size() < 8 || n.substr(0, 3) != "wd_" || de.path().extension() != ".csv") continue;
        try {
            max_n = std::max(max_n, std::stoi(n.substr(3)));
        } catch (...) {
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "wd_%03d.csv", max_n + 1);
    return buf;
}

std::string auth_from_ap(const platform::WifiAp& ap) {
    if (ap.open) return "Open";
    const std::string& raw = ap.security_raw.empty() ? ap.security : ap.security_raw;
    if (raw.find("WPA3") != std::string::npos) return "WPA3";
    if (raw.find("WPA2") != std::string::npos) return "WPA2";
    if (raw.find("WPA") != std::string::npos) return "WPA";
    if (raw.find("WEP") != std::string::npos) return "WEP";
    return raw.empty() ? "Unknown" : raw;
}

std::string sec_mark(const platform::WifiAp& ap) {
    if (ap.open) return "OPN";
    const std::string a = auth_from_ap(ap);
    if (a.size() <= 4) return a;
    return a.substr(0, 4);
}

struct LogFix {
    bool can_log{false};
    double lat{0};
    double lon{0};
    double alt{0};
    float accuracy_m{999};
    int sats{0};
    bool accuracy_ok{false};
    bool has_fix{false};
    std::string datetime;
    std::string status_note;
};

LogFix resolve_log_fix(const platform::GpsSessionState& st, const platform::WdConfig& cfg, bool& ever_fix,
                        double& last_lat, double& last_lon) {
    LogFix out;
    out.sats = st.sats;
    out.accuracy_m = st.hdop > 0 ? static_cast<float>(st.hdop * 2.5) : 999.f;
    out.accuracy_ok = out.accuracy_m <= cfg.accuracy_threshold_m && st.sats >= cfg.min_sats;
    out.has_fix = st.valid && st.age_ms < 15000;
    const std::string now = st.time_utc.empty() ? std::string("—") : st.time_utc;

    if (!st.open && st.bytes == 0) {
        out.status_note = "GPS: not connected";
    } else if (!out.has_fix) {
        out.status_note = "GPS: detecting location";
    } else if (out.accuracy_ok) {
        out.status_note = "GPS: fix OK";
    } else {
        out.status_note = "GPS: logging (weak/no HDOP)";
    }

    if (cfg.gps_log_mode == "fix_only") {
        if (out.has_fix && out.accuracy_ok && st.sats >= cfg.min_sats) {
            out.can_log = true;
            out.lat = st.lat;
            out.lon = st.lon;
            out.alt = st.alt_m;
            ever_fix = true;
            last_lat = st.lat;
            last_lon = st.lon;
        }
    } else if (cfg.gps_log_mode == "last_known") {
        if (out.has_fix && out.accuracy_ok) {
            out.can_log = true;
            out.lat = st.lat;
            out.lon = st.lon;
            out.alt = st.alt_m;
            ever_fix = true;
            last_lat = st.lat;
            last_lon = st.lon;
        } else if (ever_fix) {
            out.can_log = true;
            out.lat = last_lat;
            out.lon = last_lon;
        }
    } else { // zero_gps
        out.can_log = true;
        if (out.has_fix) {
            out.lat = st.lat;
            out.lon = st.lon;
            out.alt = st.alt_m;
            ever_fix = true;
            last_lat = st.lat;
            last_lon = st.lon;
        } else {
            out.status_note = "Logging @ 0,0 (no fix)";
        }
    }
    out.datetime = now;
    return out;
}

// --- WiGLE upload via curl ---------------------------------------------------

int count_distinct_first_seen(const std::string& path) {
    std::ifstream in(path);
    if (!in) return 0;
    std::string line;
    std::set<std::string> seen;
    bool header = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            header = false;
            if (line.find("WigleWifi") != std::string::npos) continue;
            if (line.find("MAC,SSID") != std::string::npos) continue;
        }
        // FirstSeen is column 4 (0-based 3) in WiGLE CSV
        int col = 0;
        size_t start = 0;
        bool in_q = false;
        for (size_t i = 0; i <= line.size(); ++i) {
            const bool end = i == line.size();
            const char c = end ? ',' : line[i];
            if (!end && c == '"') {
                in_q = !in_q;
                continue;
            }
            if (end || (c == ',' && !in_q)) {
                if (col == 3) {
                    seen.insert(line.substr(start, i - start));
                    break;
                }
                ++col;
                start = i + 1;
            }
        }
    }
    return static_cast<int>(seen.size());
}

bool is_wigle_csv(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (!std::getline(in, line)) return false;
    return line.find("WigleWifi") != std::string::npos;
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

struct UploadResult {
    int uploaded{0};
    int thin{0};
    int failed{0};
    std::string message;
};

UploadResult wigle_upload_folder(const std::string& folder, const platform::WdConfig& cfg, Cancel* cancel) {
    UploadResult r;
    const std::string token = wigle_token();
    if (token.empty()) {
        r.message = "Wigle token not found\nSet in WD Config > WiGLE token";
        return r;
    }
    std::string which;
    if (platform::run_shell("command -v curl", which) != 0 || trim(which).empty()) {
        r.message = "curl not found";
        return r;
    }
    const fs::path uploaded = fs::path(folder) / "uploaded";
    const fs::path thin = uploaded / "thin";
    std::error_code ec;
    fs::create_directories(thin, ec);

    const std::string auth_cfg = gps_file("wigle_curl_auth.tmp");
    write_text_file(auth_cfg, "header = \"Authorization: Basic " + token + "\"\n", 0600);

    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(folder, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const std::string n = de.path().filename().string();
        if (n == "stats.csv") continue;
        if (de.path().extension() != ".csv" && de.path().extension() != ".CSV") continue;
        if (!is_wigle_csv(de.path().string())) continue;
        files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        if (cancel && *cancel) break;
        const std::string name = path.filename().string();
        if (cfg.min_sweeps > 0) {
            const int sweeps = count_distinct_first_seen(path.string());
            if (sweeps < cfg.min_sweeps) {
                fs::rename(path, thin / name, ec);
                r.thin++;
                continue;
            }
        }
        std::string upload_path = path.string();
        std::string upload_name = name;
        bool used_gz = false;
        fs::path gz_tmp;
        if (cfg.compress) {
            gz_tmp = fs::path(gps_dir()) / (name + ".gz.tmp");
            std::string gout;
            const int gre =
                platform::run_shell("gzip -c " + shell_quote(path.string()) + " > " + shell_quote(gz_tmp.string()),
                                    gout);
            if (gre == 0 && fs::exists(gz_tmp)) {
                upload_path = gz_tmp.string();
                upload_name = name + ".gz";
                used_gz = true;
            }
        }
        const std::string out_body = (fs::path(gps_dir()) / "wigle_resp.tmp").string();
        const std::string cmd = "curl -sS -K " + shell_quote(auth_cfg) + " -F " +
                                shell_quote("file=@" + upload_path + ";filename=" + upload_name) + " -o " +
                                shell_quote(out_body) + " " + shell_quote("https://api.wigle.net/api/v2/file/upload");
        std::string cout;
        const int crc = platform::run_shell(cmd, cout);
        const std::string body = read_text_file(out_body);
        const bool ok = crc == 0 && body.find("\"success\":true") != std::string::npos;
        if (used_gz) fs::remove(gz_tmp, ec);
        if (ok) {
            r.uploaded++;
            if (cfg.delete_after_upload) fs::remove(path, ec);
            else fs::rename(path, uploaded / name, ec);
        } else {
            r.failed++;
        }
    }
    fs::remove(auth_cfg, ec);
    fs::remove(fs::path(gps_dir()) / "wigle_resp.tmp", ec);
    r.message = sfmt("%d up, %d thin, %d fail", r.uploaded, r.thin, r.failed);
    if (!cfg.compress) {
        // nothing
    }
    return r;
}

void maybe_auto_upload(const platform::WdConfig& cfg, const Status& status, Cancel& cancel) {
    if (!cfg.auto_upload_on_launch || cfg.upload_ssid.empty() || wigle_token().empty()) return;
    status("Auto-upload check...");
    std::string err;
    const auto aps = platform::wifi_scan(err, cancel_ptr(cancel));
    bool found = false;
    for (const auto& ap : aps) {
        if (ap.ssid == cfg.upload_ssid) {
            found = true;
            break;
        }
    }
    if (!found) {
        status("Upload SSID not seen");
        sleep_cancel(cancel, 800);
        return;
    }
    if (!platform::wifi_connect_psk(cfg.upload_ssid, cfg.upload_password, err)) {
        status("Upload WiFi failed");
        sleep_cancel(cancel, 800);
        return;
    }
    const auto r = wigle_upload_folder(captures_dir(), cfg, &cancel);
    status(r.message);
    sleep_cancel(cancel, 1000);
}

// --- Menus -------------------------------------------------------------------

Screen make_wardriving_menu(VM& vm, AssetManager& assets) {
    std::vector<MenuEntry> e;
    e.push_back({"Scan WiFi Networks", [&vm]() { vm.open_tool(ToolId::GpsScanWifi); }});
    e.push_back({"Scan BLE Devices", [&vm]() { vm.open_tool(ToolId::GpsScanBle); }});
    e.push_back({"Scan Both", [&vm]() { vm.open_tool(ToolId::GpsScanBoth); }});
    e.push_back({"View Captures", [&vm]() { vm.open_tool(ToolId::GpsCaptures); }});
    e.push_back({"WD Config", [&vm]() { vm.open_tool(ToolId::GpsWdConfig); }});
    e.push_back({"Wardriving Master", [&vm]() { vm.open_tool(ToolId::GpsWdMaster); }});
    return make_options(vm, assets, "Wardriving", std::move(e));
}

Screen make_config_menu(VM& vm, AssetManager& assets) {
    std::vector<MenuEntry> e;
    e.push_back({"GPS Hardware", [&vm]() { vm.open_tool(ToolId::GpsHardware); }});
    e.push_back({"GPS Source", [&vm]() { vm.open_tool(ToolId::GpsSource); }});
    e.push_back({"NMEA device path", [&vm]() { vm.open_tool(ToolId::GpsNmeaPath); }});
    e.push_back({"Baudrate", [&vm]() { vm.open_tool(ToolId::GpsBaud); }});
    e.push_back({"GPS Status", [&vm]() { vm.open_tool(ToolId::GpsStatus); }});
    e.push_back({"Captures folder", [&vm]() { vm.open_tool(ToolId::GpsFolder); }});
    return make_options(vm, assets, "GPS Config", std::move(e));
}

Screen make_hardware(VM& vm, AssetManager& assets) {
    auto set = [&vm](const char* v) {
        write_text_file(gps_file("hardware.txt"), std::string(v) + "\n");
        vm.go_back();
    };
    std::vector<MenuEntry> e;
    e.push_back({"Unit GPS v1.1 (Grove)", [set]() { set("unit_v11"); }});
    e.push_back({"LoRa Cap GNSS", [set]() { set("cap_lora"); }});
    return make_options(vm, assets,
                        std::string("Hardware (") + platform::gps_hardware_label(load_hardware()) + ")",
                        std::move(e));
}

Screen make_source(VM& vm, AssetManager& assets) {
    auto set = [&vm](const char* v) {
        write_text_file(gps_file("source.txt"), std::string(v) + "\n");
        vm.go_back();
    };
    std::vector<MenuEntry> e;
    e.push_back({"Auto (gpsd then NMEA)", [set]() { set("auto"); }});
    e.push_back({"gpsd (127.0.0.1:2947)", [set]() { set("gpsd"); }});
    e.push_back({"NMEA tty", [set]() { set("nmea"); }});
    return make_options(vm, assets, "GPS Source (" + load_source() + ")", std::move(e));
}

Screen make_nmea_path(VM& vm, AssetManager& assets) {
    return make_prompt(vm, assets, "NMEA device path", "/dev/serial0", load_nmea_tty(),
                       [&vm](const std::string& v) {
                           const std::string p = trim(v);
                           if (!p.empty()) write_text_file(gps_file("nmea_tty.txt"), p + "\n");
                           vm.go_back();
                       });
}

Screen make_baud(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "custom") {
        return make_prompt(vm, assets, "Baudrate", "bits/s", std::to_string(load_baud()),
                           [&vm](const std::string& v) {
                               const std::string t = trim(v);
                               if (!t.empty()) write_text_file(gps_file("baud.txt"), t + "\n");
                               vm.go_back();
                           });
    }
    auto set = [&vm](int b) {
        write_text_file(gps_file("baud.txt"), std::to_string(b) + "\n");
        vm.go_back();
    };
    std::vector<MenuEntry> e;
    e.push_back({"9600", [set]() { set(9600); }});
    e.push_back({"115200", [set]() { set(115200); }});
    e.push_back({"Custom...", [&vm]() { nav(vm, ToolId::GpsBaud, "custom"); }});
    return make_options(vm, assets, "Baud (" + std::to_string(load_baud()) + ")", std::move(e));
}

Screen make_folder(VM& vm, AssetManager& assets) {
    return make_prompt(vm, assets, "Captures folder", "directory containing .csv captures", captures_dir(),
                       [&vm](const std::string& v) {
                           const std::string p = trim(v);
                           if (!p.empty()) write_text_file(tools::config_file("captures_path.txt"), p + "\n");
                           vm.go_back();
                       });
}

Screen make_status(VM& vm, AssetManager& assets) {
    return std::make_unique<screen::InfoScreen>(
        vm, assets, "GPS Status",
        [](Cancel& cancel, const screen::InfoScreen::PublishFn& publish) {
            platform::GpsSession session;
            std::string err;
            const bool ok = start_gps_session(session, err);
            auto idle = std::chrono::steady_clock::now();
            while (!cancel) {
                const auto st = session.state();
                screen::InfoScreen::Fields fields;
                fields.push_back({"Hardware", st.hardware_label.empty()
                                                     ? platform::gps_hardware_label(load_hardware())
                                                     : st.hardware_label});
                fields.push_back({"Source", st.source_label.empty() ? load_source() : st.source_label});
                fields.push_back({"Path", st.path.empty() ? load_nmea_tty() : st.path});
                fields.push_back({"Baud", std::to_string(st.baud ? st.baud : load_baud())});
                fields.push_back({"Open", st.open_result.empty() ? (ok ? "starting" : err) : st.open_result});
                fields.push_back({"Bytes", std::to_string(st.bytes)});
                fields.push_back({"Sentences", std::to_string(st.sentences)});
                fields.push_back({"Talker", st.last_talker.empty() ? "—" : st.last_talker});
                fields.push_back({"Quality", std::to_string(st.quality)});
                fields.push_back({"Sats used/view", sfmt("%d / %d", st.sats, st.sats_in_view)});
                fields.push_back({"HDOP", st.hdop < 0 ? "—" : sfmt("%.1f", st.hdop)});
                fields.push_back({"Altitude", sfmt("%.1f m", st.alt_m)});
                if (st.valid) {
                    fields.push_back({"Latitude", sfmt("%.6f", st.lat)});
                    fields.push_back({"Longitude", sfmt("%.6f", st.lon)});
                } else {
                    fields.push_back({"Fix", "No fix yet"});
                }
                fields.push_back({"UTC", st.time_utc.empty() ? "—" : st.time_utc});
                fields.push_back({"NMEA ok/bad", sfmt("%llu / %llu", (unsigned long long)st.checksum_ok,
                                                     (unsigned long long)st.checksum_bad)});
                fields.push_back({"Error", st.last_error.empty() ? "—" : st.last_error});
                if (st.bytes == 0 &&
                    std::chrono::steady_clock::now() - idle > std::chrono::seconds(5)) {
                    fields.push_back({"Hint", "Check Source, path, baud"});
                    fields.push_back({"Typical", "/dev/serial0 @ 115200"});
                }
                std::string pill = "NOFIX";
                view::StatusPillKind kind = view::StatusPillKind::Missing;
                if (!ok && !session.running()) {
                    pill = st.last_error.empty() ? "offline" : "error";
                    kind = view::StatusPillKind::Error;
                } else if (st.valid) {
                    pill = "FIX";
                    kind = view::StatusPillKind::Ok;
                } else if (st.bytes > 0 || st.sats > 0) {
                    pill = "ACQ";
                    kind = view::StatusPillKind::Busy;
                }
                publish(std::move(fields), pill, kind);
                if (!ok && !session.running()) {
                    sleep_cancel(cancel, 500);
                    continue;
                }
                sleep_cancel(cancel, 400);
            }
            session.stop();
        });
}

// --- WD Config ---------------------------------------------------------------

Screen make_wd_config(VM& vm, AssetManager& assets, const Stage& st) {
    platform::WdConfig cfg = load_wd_cfg();
    auto reload_nav = [&vm]() { nav(vm, ToolId::GpsWdConfig); };

    if (st.name == "set_mode") {
        cfg.gps_log_mode = st.arg;
        save_wd_cfg(cfg);
        return make_info(vm, assets, "WD Config", "gps_log_mode = " + st.arg);
    }
    if (st.name == "set_scan") {
        cfg.scan_mode = st.arg;
        save_wd_cfg(cfg);
        return make_info(vm, assets, "WD Config", "scan_mode = " + st.arg);
    }
    if (st.name == "num") {
        // arg = field|current
        const auto bar = st.arg.find('|');
        const std::string field = bar == std::string::npos ? st.arg : st.arg.substr(0, bar);
        const std::string cur = bar == std::string::npos ? "" : st.arg.substr(bar + 1);
        return make_prompt(vm, assets, field, "number", cur, [&vm, field](const std::string& v) {
            platform::WdConfig c = load_wd_cfg();
            const std::string t = trim(v);
            if (!t.empty()) {
                try {
                    const int n = std::stoi(t);
                    if (field == "min_sats") c.min_sats = std::clamp(n, 1, 20);
                    else if (field == "accuracy_m") c.accuracy_threshold_m = static_cast<float>(n);
                    else if (field == "gmt") c.gmt_offset_hours = std::clamp(n, -12, 14);
                    else if (field == "active_dwell") c.active_dwell_ms = std::clamp(n, 10, 2000);
                    else if (field == "passive_dwell") c.passive_dwell_ms = std::clamp(n, 10, 2000);
                    else if (field == "scan_delay") c.scan_delay_ms = std::clamp(n, 50, 5000);
                    else if (field == "tx_power") c.wifi_tx_power = std::clamp(n, 8, 84);
                    else if (field == "mac_rotate") c.mac_randomize_interval = std::clamp(n, 0, 100);
                    else if (field == "low_bat") c.low_battery_pct = std::clamp(n, 0, 50);
                    else if (field == "stats_iv") c.stats_interval_s = std::max(10, n);
                    else if (field == "min_sweeps") c.min_sweeps = std::clamp(n, 0, 9999);
                } catch (...) {
                }
                save_wd_cfg(c);
            }
            nav(vm, ToolId::GpsWdConfig);
        });
    }
    if (st.name == "str") {
        const auto bar = st.arg.find('|');
        const std::string field = bar == std::string::npos ? st.arg : st.arg.substr(0, bar);
        const std::string cur = bar == std::string::npos ? "" : st.arg.substr(bar + 1);
        return make_prompt(vm, assets, field, field, cur, [&vm, field](const std::string& v) {
            platform::WdConfig c = load_wd_cfg();
            if (field == "country") {
                c.wifi_country_code = trim(v);
                for (char& ch : c.wifi_country_code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            } else if (field == "upload_ssid") c.upload_ssid = trim(v);
            else if (field == "upload_pass") c.upload_password = v;
            else if (field == "wigle_token") {
                write_text_file(gps_file("wigle_token.txt"), trim(v) + "\n", 0600);
            }
            save_wd_cfg(c);
            nav(vm, ToolId::GpsWdConfig);
        });
    }
    if (st.name == "toggle") {
        if (st.arg == "mac_oui") cfg.mac_spoof_oui = !cfg.mac_spoof_oui;
        else if (st.arg == "beep_new") cfg.beep_on_new = !cfg.beep_on_new;
        else if (st.arg == "beep_blocked") cfg.beep_on_blocked = !cfg.beep_on_blocked;
        else if (st.arg == "beep_geo") cfg.beep_on_geofence = !cfg.beep_on_geofence;
        else if (st.arg == "stats") cfg.stats_enabled = !cfg.stats_enabled;
        else if (st.arg == "auto_up") cfg.auto_upload_on_launch = !cfg.auto_upload_on_launch;
        else if (st.arg == "gzip") cfg.compress = !cfg.compress;
        else if (st.arg == "del_after") cfg.delete_after_upload = !cfg.delete_after_upload;
        save_wd_cfg(cfg);
        return make_wd_config(vm, assets, {});
    }
    if (st.name == "clear") {
        cfg.excluded_ssids.clear();
        cfg.excluded_bssids.clear();
        cfg.geofence_boxes.clear();
        save_wd_cfg(cfg);
        return make_info(vm, assets, "WD Config", "Exclude lists cleared");
    }
    if (st.name == "save") {
        save_wd_cfg(cfg);
        return make_info(vm, assets, "WD Config", "Saved\n" + gps_file("wardriverconfig.txt"));
    }
    if (st.name == "upload") {
        return make_run(vm, assets, "WiGLE Upload", [](Cancel& cancel, const Log& log, const Status& status) {
            status("Uploading...");
            const auto r = wigle_upload_folder(captures_dir(), load_wd_cfg(), &cancel);
            log(r.message);
        });
    }
    if (st.name == "modes") {
        std::vector<MenuEntry> e;
        e.push_back({"fix_only", [&vm]() { nav(vm, ToolId::GpsWdConfig, "set_mode", "fix_only"); }});
        e.push_back({"zero_gps", [&vm]() { nav(vm, ToolId::GpsWdConfig, "set_mode", "zero_gps"); }});
        e.push_back({"last_known", [&vm]() { nav(vm, ToolId::GpsWdConfig, "set_mode", "last_known"); }});
        return make_options(vm, assets, "GPS Log mode", std::move(e));
    }
    if (st.name == "scans") {
        std::vector<MenuEntry> e;
        for (const char* m : {"active_all", "active_hop", "passive_all", "passive_hop"}) {
            e.push_back({m, [&vm, m]() { nav(vm, ToolId::GpsWdConfig, "set_scan", m); }});
        }
        return make_options(vm, assets, "Scan mode", std::move(e));
    }

    auto num = [&vm](const char* label, const char* field, int cur) {
        return MenuEntry{std::string(label) + ": " + std::to_string(cur),
                         [&vm, field, cur]() {
                             nav(vm, ToolId::GpsWdConfig, "num", std::string(field) + "|" + std::to_string(cur));
                         }};
    };
    auto str = [&vm](const char* label, const char* field, const std::string& cur) {
        return MenuEntry{std::string(label) + ": " + (cur.empty() ? "(empty)" : cur),
                         [&vm, field, cur]() { nav(vm, ToolId::GpsWdConfig, "str", std::string(field) + "|" + cur); }};
    };
    auto tog = [&vm](const char* label, const char* field, bool on) {
        return MenuEntry{std::string(label) + ": " + (on ? "ON" : "OFF"),
                         [&vm, field]() { nav(vm, ToolId::GpsWdConfig, "toggle", field); }};
    };

    std::vector<MenuEntry> e;
    e.push_back({"GPS mode: " + cfg.gps_log_mode, [&vm]() { nav(vm, ToolId::GpsWdConfig, "modes"); }});
    e.push_back(num("Min sats", "min_sats", cfg.min_sats));
    e.push_back(num("Acc m", "accuracy_m", static_cast<int>(cfg.accuracy_threshold_m)));
    e.push_back(num("GMT offset", "gmt", cfg.gmt_offset_hours));
    e.push_back({"Scan: " + cfg.scan_mode, [&vm]() { nav(vm, ToolId::GpsWdConfig, "scans"); }});
    e.push_back(num("Active dwell", "active_dwell", cfg.active_dwell_ms));
    e.push_back(num("Passive dwell", "passive_dwell", cfg.passive_dwell_ms));
    e.push_back(num("Scan delay", "scan_delay", cfg.scan_delay_ms));
    e.push_back(str("Country (not applied)", "country", cfg.wifi_country_code));
    e.push_back(num("TX power (not applied)", "tx_power", cfg.wifi_tx_power));
    e.push_back(num("MAC rotate (not applied)", "mac_rotate", cfg.mac_randomize_interval));
    e.push_back(tog("MAC OUI spoof (not applied)", "mac_oui", cfg.mac_spoof_oui));
    e.push_back(tog("Beep new AP", "beep_new", cfg.beep_on_new));
    e.push_back(tog("Beep blocked", "beep_blocked", cfg.beep_on_blocked));
    e.push_back(tog("Beep geofence", "beep_geo", cfg.beep_on_geofence));
    e.push_back(num("Low bat %", "low_bat", cfg.low_battery_pct));
    e.push_back(tog("Stats CSV", "stats", cfg.stats_enabled));
    e.push_back(num("Stats interval s", "stats_iv", cfg.stats_interval_s));
    e.push_back(tog("Auto upload", "auto_up", cfg.auto_upload_on_launch));
    e.push_back(str("Upload SSID", "upload_ssid", cfg.upload_ssid));
    e.push_back(str("Upload pass", "upload_pass", cfg.upload_password.empty() ? "" : "********"));
    e.push_back(str("WiGLE token", "wigle_token", wigle_token().empty() ? "" : "********"));
    e.push_back(tog("Gzip upload", "gzip", cfg.compress));
    e.push_back(tog("Delete after", "del_after", cfg.delete_after_upload));
    e.push_back(num("Min sweeps", "min_sweeps", cfg.min_sweeps));
    e.push_back({"Clear exclude lists", [&vm]() { nav(vm, ToolId::GpsWdConfig, "clear"); }});
    e.push_back({"Upload now", [&vm]() { nav(vm, ToolId::GpsWdConfig, "upload"); }});
    e.push_back({"Save", [&vm]() { nav(vm, ToolId::GpsWdConfig, "save"); }});
    (void)reload_nav;
    return make_options(vm, assets, "WD Config", std::move(e));
}

// --- View Captures -----------------------------------------------------------

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (in_q && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else {
                in_q = !in_q;
            }
            continue;
        }
        if (c == ',' && !in_q) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

Screen make_captures(VM& vm, AssetManager& assets, const Stage& st) {
    const std::string dir = captures_dir();
    if (st.name == "detail") {
        // arg = file|key
        const auto bar = st.arg.find('|');
        const std::string file = bar == std::string::npos ? st.arg : st.arg.substr(0, bar);
        const std::string key = bar == std::string::npos ? "" : st.arg.substr(bar + 1);
        const std::string path = (fs::path(dir) / file).string();
        std::ifstream in(path);
        std::string line;
        std::vector<std::string> lines;
        bool skip_meta = true;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (skip_meta) {
                if (line.find("WigleWifi") != std::string::npos) continue;
                if (line.find("MAC,SSID") != std::string::npos) {
                    skip_meta = false;
                    continue;
                }
            }
            auto cols = split_csv_line(line);
            if (cols.size() < 4) continue;
            if (cols[0] == key) {
                lines.push_back("SSID: " + (cols.size() > 1 ? cols[1] : ""));
                lines.push_back("BSSID: " + cols[0]);
                lines.push_back("Auth: " + (cols.size() > 2 ? cols[2] : ""));
                lines.push_back("FirstSeen: " + (cols.size() > 3 ? cols[3] : ""));
                lines.push_back("Channel: " + (cols.size() > 4 ? cols[4] : ""));
                lines.push_back("RSSI: " + (cols.size() > 6 ? cols[6] : ""));
                lines.push_back("Lat: " + (cols.size() > 7 ? cols[7] : ""));
                lines.push_back("Lon: " + (cols.size() > 8 ? cols[8] : ""));
                lines.push_back("Type: " + (cols.size() > 13 ? cols[13] : ""));
                break;
            }
        }
        if (lines.empty()) lines.push_back("Not found");
        return make_results(vm, assets, key.empty() ? file : key, std::move(lines));
    }
    if (st.name == "view") {
        if (st.arg.find('/') != std::string::npos || st.arg.find("..") != std::string::npos) {
            return make_info(vm, assets, "Capture", "Bad file name");
        }
        const std::string path = (fs::path(dir) / st.arg).string();
        if (is_wigle_csv(path)) {
            std::ifstream in(path);
            std::string line;
            std::map<std::string, std::vector<std::string>> unique;
            bool skip = true;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (skip) {
                    if (line.find("WigleWifi") != std::string::npos) continue;
                    if (line.find("MAC,SSID") != std::string::npos) {
                        skip = false;
                        continue;
                    }
                }
                auto cols = split_csv_line(line);
                if (cols.size() < 7) continue;
                if (cols.size() > 13 && cols[13] == "FLAG") continue;
                if (!unique.count(cols[0])) unique[cols[0]] = cols;
            }
            std::vector<MenuEntry> e;
            for (const auto& kv : unique) {
                const auto& c = kv.second;
                const std::string label = (c.size() > 1 ? c[1] : kv.first) + "  " + kv.first;
                const std::string file = st.arg;
                const std::string key = kv.first;
                e.push_back({label, [&vm, file, key]() { nav(vm, ToolId::GpsCaptures, "detail", file + "|" + key); }});
            }
            if (e.empty()) e.push_back({"(no networks)", [&vm]() { vm.go_back(); }});
            return make_options(vm, assets, st.arg, std::move(e));
        }
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string l;
        while (std::getline(in, l) && lines.size() < 500) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
        return make_results(vm, assets, st.arg, std::move(lines));
    }
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2) || de.path().extension() != ".csv") continue;
        names.push_back(de.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    std::vector<MenuEntry> e;
    for (const auto& n : names) {
        e.push_back({n, [&vm, n]() { nav(vm, ToolId::GpsCaptures, "view", n); }});
    }
    if (e.empty()) e.push_back({"No .csv in " + dir, [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "GPS Captures", std::move(e));
}

// --- Live Wardrive -----------------------------------------------------------

struct WdCtrls {
    std::atomic<bool> pause_log{false};
    std::atomic<bool> wifi_stopped{false};
    std::atomic<bool> flag_pulse{false};
    std::atomic<bool> muted{false};
    std::atomic<bool> show_help{false};
    std::atomic<int> blank_pct{-1};
};

Screen make_live(VM& vm, AssetManager& assets, const std::string& mode) {
    const std::string name_col = mode == "ble" ? "Device" : "SSID";
    auto ctrls = std::make_shared<WdCtrls>();
    auto summary_text = std::make_shared<std::string>();

    screen::WardriveScreen::KeyFn on_key = [ctrls](uint32_t key) -> bool {
        char c = 0;
        if (key >= 'A' && key <= 'Z') c = static_cast<char>(key - 'A' + 'a');
        else if (key >= 'a' && key <= 'z') c = static_cast<char>(key);
        else return false;
        switch (c) {
        case 'h': ctrls->show_help = true; return true;
        case 'p': ctrls->pause_log = !ctrls->pause_log.load(); return true;
        case 'x': ctrls->wifi_stopped = !ctrls->wifi_stopped.load(); return true;
        case 'f': ctrls->flag_pulse = true; return true;
        case 's': ctrls->muted = !ctrls->muted.load(); return true;
        case 'b': {
            int32_t cur = 50;
            platform::read_brightness_percent(cur);
            if (ctrls->blank_pct < 0) {
                ctrls->blank_pct = cur;
                platform::write_brightness_percent(0);
            } else {
                platform::write_brightness_percent(ctrls->blank_pct.load());
                ctrls->blank_pct = -1;
            }
            return true;
        }
        case 'q': return false; // Esc path via cancel
        default: return false;
        }
    };

    screen::WardriveScreen::DoneFn on_done = [&vm, summary_text]() {
        const std::string text = summary_text->empty() ? "Session ended." : *summary_text;
        write_text_file(gps_file("last_summary.txt"), text);
        nav(vm, ToolId::None, "info", text);
    };

    return std::make_unique<screen::WardriveScreen>(
        vm, assets, "Wardrive", name_col,
        [mode, ctrls, summary_text](Cancel& cancel, const screen::WardriveScreen::RowsFn& set_rows,
                                   const screen::WardriveScreen::StatusFn& status) {
            const platform::WdConfig cfg = load_wd_cfg();
            maybe_auto_upload(cfg, status, cancel);
            if (cancel) return;

            platform::GpsSession session;
            std::string gerr;
            (void)start_gps_session(session, gerr);

            const std::string dir = captures_dir();
            const std::string fname = next_wd_filename(dir);
            const std::string out_path = (fs::path(dir) / fname).string();
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                status("Cannot write capture");
                return;
            }
            out << "WigleWifi-1.6,appRelease=kvxputerzero,model=kvxputerzero,release=kvxputerzero,"
                   "device=CardputerZero,display=SPI TFT,board=Linux,brand=kvxputer,star=Sol,body=4,subBody=1\n";
            out << "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,"
                   "AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n";
            out.flush();

            struct Seen {
                int hits{0};
                int rssi{-127};
                int best{-127};
                std::string first;
                std::string last;
                double lat{0};
                double lon{0};
                bool located{false};
                screen::WardriveRow row;
            };
            std::map<std::string, Seen> seen;
            std::unordered_set<std::string> unique;
            uint32_t wifi_logged = 0, ble_logged = 0, blocked = 0, flags = 0, sweeps = 0, write_errs = 0;
            double distance_m = 0;
            bool ever_fix = false;
            double last_lat = 0, last_lon = 0;
            double prev_lat = 0, prev_lon = 0;
            bool have_prev = false;
            auto last_stats = std::chrono::steady_clock::now();
            std::ofstream stats;
            if (cfg.stats_enabled) {
                stats.open((fs::path(dir) / "stats.csv").string(), std::ios::app);
                if (stats.tellp() == 0) stats << "event,time,rows,wifi,ble,unique,battery\n";
                stats << "init,—,0,0,0,0,-\n";
            }

            auto beep_if = [&](bool want) {
                if (!want || ctrls->muted) return;
                std::string e;
                platform::beep(e);
            };

            auto write_row = [&](const std::string& mac, const std::string& ssid, const std::string& auth,
                                 const std::string& when, int ch, int freq, int rssi, const LogFix& fix,
                                 const std::string& mfg, const std::string& type) {
                if (ctrls->pause_log) return;
                if (!fix.can_log && cfg.gps_log_mode == "fix_only") return;
                if (!fix.can_log && cfg.gps_log_mode == "last_known") return;
                const bool skip_geo = (cfg.gps_log_mode == "zero_gps" && !(fix.has_fix && fix.accuracy_ok));
                if (!platform::wd_allow_log(cfg, ssid, mac, fix.lat, fix.lon, skip_geo)) {
                    blocked++;
                    beep_if(cfg.beep_on_blocked || cfg.beep_on_geofence);
                    return;
                }
                out << mac << ",\"" << csv_escape(ssid) << "\",[" << auth << "],\"" << when << "\"," << ch << ','
                    << freq << ',' << rssi << ',' << fix.lat << ',' << fix.lon << ',' << fix.alt << ','
                    << fix.accuracy_m << ",," << mfg << ',' << type << '\n';
                if (!out) write_errs++;
                if (type == "WIFI") wifi_logged++;
                else if (type == "BLE") ble_logged++;
                else if (type == "FLAG") flags++;
                unique.insert(mac);
            };

            auto publish = [&]() {
                std::vector<const Seen*> order;
                for (const auto& kv : seen) order.push_back(&kv.second);
                std::sort(order.begin(), order.end(), [](const Seen* a, const Seen* b) {
                    if (a->rssi != b->rssi) return a->rssi > b->rssi;
                    return a->row.name < b->row.name;
                });
                std::vector<screen::WardriveRow> rows;
                for (const Seen* s : order) {
                    rows.push_back(s->row);
                    if (rows.size() >= 120) break;
                }
                set_rows(std::move(rows));
            };

            auto shown = [](const std::string& s) { return s.empty() ? std::string("—") : s; };

            while (!cancel) {
                if (ctrls->show_help.exchange(false)) {
                    status("H help  P pause  X wifi  F flag  S mute  B blank  hold Tab 3s blanks  Esc");
                }
                const auto st = session.state();
                LogFix fix = resolve_log_fix(st, cfg, ever_fix, last_lat, last_lon);
                if (fix.has_fix && fix.can_log && (fix.lat != 0 || fix.lon != 0)) {
                    if (have_prev) distance_m += haversine_m(prev_lat, prev_lon, fix.lat, fix.lon);
                    prev_lat = fix.lat;
                    prev_lon = fix.lon;
                    have_prev = true;
                }

                if (ctrls->flag_pulse.exchange(false)) {
                    write_row("FLAG", "FLAG", "FLAG", fix.datetime, 0, 0, 0, fix, "", "FLAG");
                    beep_if(true);
                }

                const auto bat = platform::read_device_status();
                if (cfg.low_battery_pct > 0 && bat.battery_present && bat.battery_percent >= 0 &&
                    bat.battery_percent <= cfg.low_battery_pct) {
                    status("Low battery — exiting");
                    break;
                }

                if ((mode == "wifi" || mode == "both") && !ctrls->wifi_stopped) {
                    std::string err;
                    const auto aps = platform::wifi_scan(err, cancel_ptr(cancel));
                    for (const auto& ap : aps) {
                        const std::string id = ap.bssid.empty() ? ap.ssid : ap.bssid;
                        if (id.empty()) continue;
                        const std::string key = "w:" + id;
                        Seen& s = seen[key];
                        const bool is_new = s.hits == 0;
                        ++s.hits;
                        s.rssi = ap.rssi;
                        if (is_new || ap.rssi > s.best) s.best = ap.rssi;
                        s.last = fix.datetime;
                        if (is_new) s.first = s.last;
                        if (fix.can_log && (fix.lat != 0 || fix.lon != 0 || cfg.gps_log_mode == "zero_gps")) {
                            s.lat = fix.lat;
                            s.lon = fix.lon;
                            s.located = fix.has_fix || cfg.gps_log_mode == "zero_gps";
                        }
                        const std::string auth = auth_from_ap(ap);
                        screen::WardriveRow row;
                        row.key = key;
                        row.name = ap.ssid.empty() ? "(hidden)" : ap.ssid;
                        row.channel = ap.channel < 0 ? "-" : std::to_string(ap.channel);
                        row.rssi = std::to_string(ap.rssi);
                        row.mark = sec_mark(ap);
                        row.details = {
                            {"SSID", row.name},
                            {"BSSID", shown(ap.bssid)},
                            {"Channel", row.channel},
                            {"RSSI", sfmt("%d dBm", ap.rssi)},
                            {"Security", auth},
                            {"Seen", std::to_string(s.hits)},
                            {"Latitude", s.located ? sfmt("%.5f", s.lat) : "0"},
                            {"Longitude", s.located ? sfmt("%.5f", s.lon) : "0"},
                            {"GPS", fix.status_note},
                        };
                        s.row = std::move(row);
                        write_row(ap.bssid.empty() ? id : ap.bssid, ap.ssid, auth, fix.datetime, ap.channel,
                                  channel_freq(ap.channel), ap.rssi, fix, "", "WIFI");
                        if (is_new) beep_if(cfg.beep_on_new);
                    }
                }
                if (cancel) break;
                if (mode == "ble" || mode == "both") {
                    std::vector<platform::BleDevice> devices;
                    std::string err;
                    if (platform::ble_scan_once(devices, err, 3, cancel_ptr(cancel)) || !devices.empty()) {
                        for (const auto& d : devices) {
                            if (d.address.empty()) continue;
                            const std::string key = "b:" + d.address;
                            Seen& s = seen[key];
                            const bool is_new = s.hits == 0;
                            ++s.hits;
                            s.rssi = d.rssi;
                            if (is_new || d.rssi > s.best) s.best = d.rssi;
                            s.last = fix.datetime;
                            if (is_new) s.first = s.last;
                            screen::WardriveRow row;
                            row.key = key;
                            row.name = d.name.empty() ? d.address : d.name;
                            row.channel = "-";
                            row.rssi = std::to_string(d.rssi);
                            row.mark = "BLE";
                            row.details = {
                                {"Name", shown(d.name)},
                                {"Address", d.address},
                                {"RSSI", sfmt("%d dBm", d.rssi)},
                                {"Mfg", shown(d.mfg_hex)},
                                {"Seen", std::to_string(s.hits)},
                                {"GPS", fix.status_note},
                            };
                            s.row = std::move(row);
                            write_row(d.address, d.name, "BLE", fix.datetime, 0, 0, d.rssi, fix, d.mfg_hex, "BLE");
                            if (is_new) beep_if(cfg.beep_on_new);
                        }
                    }
                }
                sweeps++;
                publish();
                out.flush();

                status(sfmt("%s  sats %d  rows %u  wifi %u  ble %u  uniq %zu  sw %u  %.2fkm%s%s  (OS scan)",
                            fix.status_note.c_str(), fix.sats, wifi_logged + ble_logged + flags, wifi_logged,
                            ble_logged, unique.size(), sweeps, distance_m / 1000.0,
                            ctrls->pause_log ? " PAUSE" : "", ctrls->wifi_stopped ? " WIFI-OFF" : ""));

                if (cfg.stats_enabled && stats &&
                    std::chrono::steady_clock::now() - last_stats >
                        std::chrono::seconds(std::max(10, cfg.stats_interval_s))) {
                    last_stats = std::chrono::steady_clock::now();
                    stats << "tick,—," << (wifi_logged + ble_logged) << ',' << wifi_logged << ',' << ble_logged
                          << ',' << unique.size() << ",-\n";
                    stats.flush();
                }

                if (!sleep_cancel(cancel, cfg.scan_delay_ms)) break;
            }

            out.flush();
            std::error_code fec;
            const auto bytes = fs::file_size(out_path, fec);
            if (stats) {
                stats << "exit,—," << (wifi_logged + ble_logged) << ',' << wifi_logged << ',' << ble_logged << ','
                      << unique.size() << ",-\n";
            }
            session.stop();
            if (ctrls->blank_pct >= 0) platform::write_brightness_percent(ctrls->blank_pct.load());

            std::ostringstream sum;
            sum << "File: " << fname << "\n";
            sum << "Rows: " << (wifi_logged + ble_logged + flags) << "\n";
            sum << "WiFi: " << wifi_logged << "  BLE: " << ble_logged << "\n";
            sum << "Unique: " << unique.size() << "\n";
            sum << "Sweeps: " << sweeps << "\n";
            sum << "Dist: " << sfmt("%.2f km", distance_m / 1000.0) << "\n";
            sum << "Blocked: " << blocked << "\n";
            if (write_errs) sum << "Write errs: " << write_errs << "\n";
            sum << "On disk: " << bytes << " B\n";
            if (wifi_logged + ble_logged == 0) sum << "\nWarning: header-only / no rows\n";
            if (bytes < 200) sum << "Warning: file looks too small\n";
            *summary_text = sum.str();
            status(sfmt("Saved %s", fname.c_str()));
        },
        std::move(on_key), std::move(on_done));
}

// After live screen returns, show summary if present — handled by opening GpsLive with stage summary
// via a shared path: make_live itself returns WardriveScreen; tool dispatcher shows summary via
// GpsLive stage "summary" is not automatic. Instead wrap: when mode run finishes user presses Esc
// and we open an info doc. WardriveScreen leave goes back — so stash summary in a config file.
// Simpler: write summary to gps/last_summary.txt and open it when GpsScan* returns... can't.
// Use ToolId stage after: worker writes last_summary.txt; Key Esc already leaves. Add a menu
// entry "Last summary". Better: make_scan opens live, and we also register that Esc path in
// WardriveScreen can't open another tool easily.
// Practical approach: write last_summary.txt; when opening Scan again or from Wardriving menu
// show nothing. After leave, user can open View Captures. Plan asks for summary card on exit.
// Patch: custom leave via KeyFn 'q' that sets cancel — WardriveScreen Esc calls leave().
// We'll write last_summary and have make_scan return a chained screen using a wrapper.
// Easiest UX matching plan: after WardriveScreen destructor... can't.
// Use GpsLive stage run then when opened with empty stage after run — no.
// I'll make Scan tools open live, and on worker end write last_summary.txt; make_wardriving_menu
// can show "Last session" entry. Also open summary via nav when key 'q' — but Esc doesn't.
// Extend WardriveScreen worker_done: when flush sees done, if summary file exists, we could
// change leave to open summary. Add optional on_done callback to WardriveScreen.

Screen make_scan(VM& vm, AssetManager& assets, const char* mode) {
    // Write summary to file; show as Doc after by using a two-step: run live as stage "run",
    // then stage "done" reads summary. User must Esc from live first which goes back to menu.
    // Plan: "on exit show a summary card". Implement by making WardriveScreen call an optional
    // DoneFn. Quick patch to wardrive_screen.
    return make_live(vm, assets, mode);
}

// --- Site Survey -------------------------------------------------------------

Screen make_site_survey(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "help") {
        return make_info(vm, assets,
                         "Site Survey Help",
                         "Channel airtime survey with GPS stamps.\n\n"
                         "Writes CSV under captures/:\n"
                         "  survey_<timestamp>.csv\n\n"
                         "Columns:\n"
                         "  time,lat,lon,alt,ch,load_pct,peak_rssi,ap_count\n\n"
                         "load_pct is n/a on Zero (no promiscuous airtime).\n"
                         "Without a fix, lat/lon are 0.\n"
                         "Esc stops and saves.\n"
                         "Distinct from Wardriving (WiGLE AP rows).");
    }
    if (st.name == "start") {
        return make_run(vm, assets, "Site Survey", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::GpsSession session;
            std::string err;
            (void)start_gps_session(session, err);
            const std::string dir = captures_dir();
            const auto now = std::chrono::system_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            const std::string path = (fs::path(dir) / sfmt("survey_%lld.csv", (long long)ms)).string();
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                log("Create CSV fail");
                return;
            }
            out << "time,lat,lon,alt,ch,load_pct,peak_rssi,ap_count\n";
            int rows = 0;
            while (!cancel) {
                const auto st = session.state();
                std::string werr;
                const auto aps = platform::wifi_scan(werr, cancel_ptr(cancel));
                struct ChAgg {
                    int peak{-127};
                    int count{0};
                };
                std::map<int, ChAgg> by_ch;
                for (const auto& ap : aps) {
                    if (ap.channel < 0) continue;
                    auto& a = by_ch[ap.channel];
                    a.count++;
                    if (ap.rssi > a.peak) a.peak = ap.rssi;
                }
                const double lat = st.valid ? st.lat : 0;
                const double lon = st.valid ? st.lon : 0;
                const double alt = st.valid ? st.alt_m : 0;
                const std::string t = st.time_utc.empty() ? std::to_string(ms + rows) : st.time_utc;
                for (const auto& kv : by_ch) {
                    out << t << ',' << lat << ',' << lon << ',' << alt << ',' << kv.first << ",n/a," << kv.second.peak
                        << ',' << kv.second.count << '\n';
                    rows++;
                }
                if (by_ch.empty()) {
                    out << t << ',' << lat << ',' << lon << ',' << alt << ",0,n/a,-127,0\n";
                    rows++;
                }
                out.flush();
                status(sfmt("ch groups %zu  rows %d", by_ch.size(), rows));
                if (!sleep_cancel(cancel, 1000)) break;
            }
            session.stop();
            log(sfmt("Saved %d rows", rows));
            log(path);
        });
    }
    std::vector<MenuEntry> e;
    e.push_back({"Help", [&vm]() { nav(vm, ToolId::GpsSiteSurvey, "help"); }});
    e.push_back({"Start", [&vm]() { nav(vm, ToolId::GpsSiteSurvey, "start"); }});
    return make_options(vm, assets, "Site Survey", std::move(e));
}

// --- GPS Tracker -------------------------------------------------------------

Screen make_tracker(VM& vm, AssetManager& assets) {
    return std::make_unique<screen::InfoScreen>(
        vm, assets, "GPS Track",
        [](Cancel& cancel, const screen::InfoScreen::PublishFn& publish) {
            platform::GpsSession session;
            std::string err;
            if (!start_gps_session(session, err)) {
                screen::InfoScreen::Fields fields;
                fields.push_back({"Error", err.empty() ? "No GPS on desktop" : err});
                fields.push_back({"HW", platform::gps_hardware_label(load_hardware())});
                fields.push_back({"Fix", "sudo -n /usr/lib/kvxputerzero/sysfs-led grove_5v_out 1"});
                fields.push_back({"Also", "stop scrolld; Hardware=Unit GPS v1.1"});
                publish(std::move(fields), "power", view::StatusPillKind::Error);
                while (!cancel) sleep_cancel(cancel, 400);
                return;
            }
            std::string filename;
            std::ofstream gpx;
            bool header = false;
            int points = 0;
            double distance = 0;
            double cur_lat = 0, cur_lon = 0;
            bool have_pos = false;
            auto last_pt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            auto last_byte_check = std::chrono::steady_clock::now();
            uint64_t last_bytes = 0;
            int idle_secs = 0;

            auto close_gpx = [&]() {
                if (!gpx) return;
                gpx << "    </trkseg>\n  </trk>\n</gpx>\n";
                gpx.close();
            };

            while (!cancel) {
                const auto st = session.state();
                if (st.bytes != last_bytes) {
                    last_bytes = st.bytes;
                    idle_secs = 0;
                    last_byte_check = std::chrono::steady_clock::now();
                } else if (std::chrono::steady_clock::now() - last_byte_check > std::chrono::seconds(1)) {
                    last_byte_check = std::chrono::steady_clock::now();
                    idle_secs++;
                }
                if (idle_secs >= 8 && st.bytes == 0) {
                    screen::InfoScreen::Fields fields;
                    fields.push_back({"Error", "No UART data"});
                    fields.push_back({"Hint", "Config > GPS Hardware + Unit GPS"});
                    fields.push_back({"Also", "stop scrolld / set Source NMEA"});
                    publish(std::move(fields), "NOFIX", view::StatusPillKind::Error);
                    break;
                }

                const bool fresh = st.valid && st.age_ms < 5000;
                if (fresh && std::chrono::steady_clock::now() - last_pt >= std::chrono::seconds(2)) {
                    if (have_pos) distance += haversine_m(cur_lat, cur_lon, st.lat, st.lon);
                    cur_lat = st.lat;
                    cur_lon = st.lon;
                    have_pos = true;
                    last_pt = std::chrono::steady_clock::now();
                    if (filename.empty() && !st.date_utc.empty() && st.date_utc.size() >= 4) {
                        int year = 0;
                        try {
                            year = std::stoi(st.date_utc.substr(0, 4));
                        } catch (...) {
                        }
                        if (year >= 2020) {
                            // YYMMDD_HHMMSS_gps_tracker.gpx
                            std::string ts = st.date_utc.substr(2, 2) + st.date_utc.substr(5, 2) +
                                             st.date_utc.substr(8, 2);
                            std::string tm = "000000";
                            if (st.time_utc.size() >= 8) {
                                tm.clear();
                                for (char c : st.time_utc) {
                                    if (std::isdigit(static_cast<unsigned char>(c))) tm.push_back(c);
                                }
                                if (tm.size() > 6) tm = tm.substr(0, 6);
                                while (tm.size() < 6) tm.push_back('0');
                            }
                            filename = ts + "_" + tm + "_gps_tracker.gpx";
                        }
                    }
                    if (filename.empty()) {
                        const std::time_t t = std::time(nullptr);
                        std::tm tmv{};
                        gmtime_r(&t, &tmv);
                        char buf[64];
                        std::strftime(buf, sizeof(buf), "%y%m%d_%H%M%S_gps_tracker.gpx", &tmv);
                        filename = buf;
                    }
                    if (!header) {
                        gpx.open((fs::path(tracks_dir()) / filename).string(), std::ios::binary | std::ios::trunc);
                        if (gpx) {
                            gpx << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
                            gpx << "<gpx version=\"1.1\" creator=\"kvxputerzero\" "
                                   "xmlns=\"http://www.topografix.com/GPX/1/1\">\n";
                            gpx << "  <trk>\n    <name>kvxputerzero Route</name>\n    <trkseg>\n";
                            header = true;
                        }
                    }
                    if (gpx) {
                        gpx << sfmt("      <trkpt lat=\"%.6f\" lon=\"%.6f\">\n", st.lat, st.lon);
                        gpx << sfmt("        <ele>%.1f</ele>\n", st.alt_m);
                        if (st.hdop > 0) gpx << sfmt("        <hdop>%.1f</hdop>\n", st.hdop);
                        gpx << sfmt("        <sat>%d</sat>\n", st.sats);
                        gpx << "      </trkpt>\n";
                        gpx.flush();
                        points++;
                    }
                }

                screen::InfoScreen::Fields fields;
                fields.push_back({"Hardware", st.hardware_label.empty()
                                                     ? platform::gps_hardware_label(load_hardware())
                                                     : st.hardware_label});
                fields.push_back({"UART", sfmt("%s @ %d", st.path.c_str(), st.baud)});
                fields.push_back({"Open", st.open_result.empty() ? st.source_label : st.open_result});
                fields.push_back({"Chars / NMEA", sfmt("%llu / %llu", (unsigned long long)st.bytes,
                                                      (unsigned long long)st.sentences)});
                fields.push_back({"Sats used/view", sfmt("%d / %d", st.sats, st.sats_in_view)});
                fields.push_back({"Fix quality", std::to_string(st.quality)});
                fields.push_back({"RMC status", std::string(1, st.rmc_status)});
                if (st.valid) {
                    fields.push_back({"Latitude", sfmt("%.6f", st.lat)});
                    fields.push_back({"Longitude", sfmt("%.6f", st.lon)});
                } else {
                    fields.push_back({"Fix", "No fix yet"});
                    fields.push_back({"Hint", st.sats <= 0 ? "Need clear sky" : "Acquiring..."});
                }
                if (!st.date_utc.empty()) fields.push_back({"Date", st.date_utc});
                else fields.push_back({"Date/Time", "Waiting..."});
                fields.push_back({"Time", st.time_utc.empty() ? "—" : st.time_utc});
                fields.push_back({"HDOP", st.hdop < 0 ? "—" : sfmt("%.1f", st.hdop)});
                fields.push_back({"Altitude", sfmt("%.1f", st.alt_m)});
                fields.push_back({"NMEA ok/bad", sfmt("%llu / %llu", (unsigned long long)st.checksum_ok,
                                                     (unsigned long long)st.checksum_bad)});
                if (points > 0) {
                    fields.push_back({"Track points", std::to_string(points)});
                    fields.push_back({"Distance", sfmt("%.2f km", distance / 1000.0)});
                    fields.push_back({"File", filename});
                }
                fields.push_back({"Keys", "Esc exit"});

                std::string pill = "NOFIX";
                view::StatusPillKind kind = view::StatusPillKind::Missing;
                if (st.valid) {
                    pill = "FIX";
                    kind = view::StatusPillKind::Ok;
                } else if (st.sats > 0 || st.bytes > 0) {
                    pill = "ACQ";
                    kind = view::StatusPillKind::Busy;
                }
                publish(std::move(fields), pill, kind);
                sleep_cancel(cancel, 400);
            }
            close_gpx();
            session.stop();
        });
}

} // namespace

std::unique_ptr<screen::KvxScreen> make_gps_tool_screen(viewmodel::BaseViewModel& vm, AssetManager& assets,
                                                        model::ToolId id, const std::string& param) {
    const Stage st = parse_stage(param);
    switch (id) {
    case ToolId::GpsWardrivingMenu: return make_wardriving_menu(vm, assets);
    case ToolId::GpsConfigMenu: return make_config_menu(vm, assets);
    case ToolId::GpsSource: return make_source(vm, assets);
    case ToolId::GpsHardware: return make_hardware(vm, assets);
    case ToolId::GpsNmeaPath: return make_nmea_path(vm, assets);
    case ToolId::GpsBaud: return make_baud(vm, assets, st);
    case ToolId::GpsFolder: return make_folder(vm, assets);
    case ToolId::GpsStatus: return make_status(vm, assets);
    case ToolId::GpsWdConfig: return make_wd_config(vm, assets, st);
    case ToolId::GpsWdMaster:
        return make_info(vm, assets, "Wardriving Master",
                         "Listens for ESP-NOW slaves. Not available on Cardputer Zero.");
    case ToolId::GpsCaptures: return make_captures(vm, assets, st);
    case ToolId::GpsScanWifi: return make_scan(vm, assets, "wifi");
    case ToolId::GpsScanBle: return make_scan(vm, assets, "ble");
    case ToolId::GpsScanBoth: return make_scan(vm, assets, "both");
    case ToolId::GpsLive: {
        if (st.name == "run") return make_scan(vm, assets, st.arg.empty() ? "both" : st.arg.c_str());
        std::vector<MenuEntry> e;
        e.push_back({"Wi-Fi", [&vm]() { nav(vm, ToolId::GpsLive, "run", "wifi"); }});
        e.push_back({"BLE", [&vm]() { nav(vm, ToolId::GpsLive, "run", "ble"); }});
        e.push_back({"Both", [&vm]() { nav(vm, ToolId::GpsLive, "run", "both"); }});
        return make_options(vm, assets, "Live Wardrive", std::move(e));
    }
    case ToolId::GpsSiteSurvey: return make_site_survey(vm, assets, st);
    case ToolId::GpsTracker: return make_tracker(vm, assets);
    default: return nullptr;
    }
}

} // namespace app

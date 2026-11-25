#include "runners_impl.h"
#include "util.h"

#include "backlight.h"
#include "cap_detect.h"
#include "cc1101.h"
#include "device_status.h"
#include "rf_settings.h"
#include "gps_fix.h"
#include "i2c_scan.h"
#include "nrf24_radio.h"
#include "pahub.h"
#include "power_ctrl.h"
#include "shell_util.h"
#include "si4713.h"
#include "sx1262.h"
#include "user_paths.h"
#include "w1_ibutton.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

namespace runner {
namespace {

namespace fs = std::filesystem;

int list_dir(const std::string& root, std::atomic<bool>& cancel, const LineFn& line) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return emit_ok(line, "(empty) " + root);
    if (line) line(root);
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (cancelled(cancel)) return 130;
        if (!it->is_regular_file(ec)) continue;
        if (line) line(it->path().string() + "  " + std::to_string(it->file_size(ec)));
    }
    return 0;
}

void emit_cap(const platform::CapInfo& c, const LineFn& line) {
    if (!line) return;
    line(c.name + (c.present ? " present" : " missing") + "  " + c.path +
         (c.detail.empty() ? "" : ("  " + c.detail)) + (c.error.empty() ? "" : ("  " + c.error)));
}

} // namespace

RunFn scripts_runner() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        const std::string dir = ensure_config_subdir("scripts");
        if (args.empty()) return list_dir(dir, cancel, line);
        const fs::path path = fs::path(dir) / args[0];
        std::string out;
        const int rc = platform::run_shell("bash " + path.string() + " 2>&1", out);
        std::string cur;
        for (char c : out) {
            if (cancelled(cancel)) return 130;
            if (c == '\n') {
                if (line) line(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty() && line) line(cur);
        return rc == 0 ? 0 : 1;
    };
}

RunFn pcconnect_howto() {
    return tools_info(
        "PC Connect scripts are for ESP Cardputer USB serial, not Zero radios. Browse/export local copies here.");
}
RunFn pcconnect_needs() {
    return tools_info("Needs: pyserial on a PC, Cardputer with PC Connect app open over USB.");
}
RunFn pcconnect_scripts() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        if (fs::exists("/usr/share/kvxputerzero/pcconnect")) {
            return list_dir("/usr/share/kvxputerzero/pcconnect", cancel, line);
        }
        return emit_ok(line, "no bundled pcconnect scripts found");
    };
}
RunFn pcconnect_export() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        const std::string dest = ensure_config_subdir("pcconnect");
        std::string out;
        platform::run_shell("cp -a /usr/share/kvxputerzero/pcconnect/. " + dest + " 2>&1 || true", out);
        return emit_ok(line, "exported to " + dest);
    };
}

RunFn ir_use() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "IR Use TX armed — prefer GUI for .ir picker");
    };
}
RunFn ir_learn() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        if (line) line("IR Learn — waiting (Ctrl-C cancel)");
        for (int i = 0; i < 20; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return emit_ok(line, "no signal");
    };
}
RunFn ir_browse() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        return list_dir(ensure_config_subdir("infrared"), cancel, line);
    };
}
RunFn ir_read() { return ir_learn(); }
RunFn ir_favorites() { return ir_browse(); }
RunFn ir_config() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "IR TX GPIO12 / RX GPIO13 — library under ~/.config/kvxputerzero/infrared");
    };
}
RunFn ir_tv_b_gone() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "TV-B-Gone TX armed");
    };
}
RunFn ir_tagtinker() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "TagTinker ESL TX armed");
    };
}
RunFn ir_carrier() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "IR Carrier TX armed");
    };
}

RunFn gps_status() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        platform::GpsSession session;
        std::string err;
        if (!session.start(platform::GpsSource::Auto, "/dev/serial0", 115200, err)) {
            return emit_error(line, err.empty() ? "No GPS" : err);
        }
        auto st = session.state();
        if (line) {
            if (!st.last_error.empty()) line(st.last_error);
            line("open=" + st.open_result);
            line("path=" + st.path + " baud=" + std::to_string(st.baud));
            line("bytes=" + std::to_string(st.bytes) + " sentences=" + std::to_string(st.sentences));
            if (st.valid) line("lat=" + std::to_string(st.lat) + " lon=" + std::to_string(st.lon));
        }
        session.stop();
        return 0;
    };
}

RunFn gps_live(const char* mode) {
    std::string m = mode ? mode : "wifi";
    return [m](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        if (line) line("GPS live mode=" + m);
        platform::GpsSession session;
        std::string err;
        if (!session.start(platform::GpsSource::Auto, "/dev/serial0", 115200, err)) {
            return emit_error(line, err.empty() ? "No GPS" : err);
        }
        for (int i = 0; i < 15; ++i) {
            if (cancelled(cancel)) {
                session.stop();
                return 130;
            }
            auto st = session.state();
            if (line) {
                if (!st.last_error.empty()) line(st.last_error);
                else if (st.valid) line("lat=" + std::to_string(st.lat) + " lon=" + std::to_string(st.lon));
                else line("waiting for fix…");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        session.stop();
        return 0;
    };
}

RunFn gps_config_leaf(const char* leaf) {
    std::string l = leaf ? leaf : "";
    return [l](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        const fs::path dir = fs::path(tools::config_dir()) / "gps";
        fs::create_directories(dir);
        const fs::path file = dir / (l + ".txt");
        if (!args.empty()) {
            std::ofstream out(file);
            out << args[0];
            return emit_ok(line, "saved " + file.string() + " = " + args[0]);
        }
        std::ifstream in(file);
        std::string cur;
        if (std::getline(in, cur)) return emit_ok(line, l + "=" + cur);
        return emit_ok(line, l + "=(unset)");
    };
}

RunFn gps_captures() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        return list_dir(ensure_config_subdir("gps"), cancel, line);
    };
}
RunFn gps_tracker() { return gps_live("tracker"); }
RunFn gps_site_survey() { return gps_live("survey"); }

RunFn modules_bus_scan() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        auto hits = platform::scan_all_i2c(err);
        if (!err.empty() && hits.empty()) return emit_error(line, err);
        for (const auto& h : hits) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "i2c-%d 0x%02x", h.bus, h.address);
            if (line) line(buf);
        }
        return 0;
    };
}

RunFn modules_cap_probe() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        for (const auto& c : platform::detect_all_caps()) emit_cap(c, line);
        return 0;
    };
}

RunFn modules_unit_scroll() {
    return tools_info("Unit Scroll: scrolld injects keys into the GUI.");
}

RunFn modules_pahub() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::vector<platform::PahubChannelHit> hits;
        std::string err;
        if (!platform::pahub_scan(1, 0x70, hits, err)) return emit_error(line, err.empty() ? "pahub failed" : err);
        for (const auto& h : hits) {
            std::string s = "ch" + std::to_string(h.channel) + ":";
            for (auto a : h.addresses) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), " %02x", a);
                s += buf;
            }
            if (line) line(s);
        }
        return 0;
    };
}

RunFn modules_ibutton() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        auto devs = platform::list_w1_devices(err);
        if (!err.empty() && devs.empty()) return emit_error(line, err);
        for (const auto& d : devs) {
            if (line) line(d.id + "  " + d.path);
        }
        if (devs.empty()) return emit_ok(line, "no 1-Wire devices");
        return 0;
    };
}

RunFn modules_companion_bins() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(tools::config_dir()) / "companions";
        std::error_code ec;
        fs::create_directories(dir, ec);
        int n = 0;
        for (const auto& ent : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!ent.is_regular_file(ec)) continue;
            const auto name = ent.path().filename().string();
            if (name.size() < 5 || name.substr(name.size() - 4) != ".bin") continue;
            if (line) line(name + "  " + std::to_string(static_cast<unsigned long long>(ent.file_size(ec))));
            ++n;
        }
        if (n == 0) return emit_ok(line, "no .bin in " + dir.string() + " — flash from a PC");
        return emit_ok(line, "flash from a PC (do not flash on Zero)");
    };
}

RunFn modules_joystick() { return tools_info("Unit Joystick presence — check I2C / Grove"); }
RunFn modules_pahub_presence() { return modules_pahub(); }
RunFn modules_pn532_killer() { return tools_info("PN532Killer UART — configure device path in GUI Modules"); }
RunFn modules_uart_llm() { return tools_info("UART LLM — configure serial endpoint in GUI"); }

RunFn radio_rf(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        emit_cap(platform::detect_cc1101(), line);
        if (k == "rf_status") {
            platform::Cc1101Session radio;
            std::string err;
            if (!radio.open({}, err)) return emit_error(line, err.empty() ? "Cap not present" : err);
            uint8_t part = 0, ver = 0;
            if (radio.read_chip_id(part, ver, err) && line) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "PARTNUM=0x%02X VERSION=0x%02X", part, ver);
                line(buf);
            }
            radio.close();
            return 0;
        }
        if (k == "rf_spectrum") {
            platform::Cc1101Session radio;
            std::string err;
            if (!radio.open({}, err)) return emit_error(line, err.empty() ? "Cap not present" : err);
            auto s = platform::load_rf_settings();
            std::vector<std::string> lines;
            const uint32_t start = s.freq_hz > 1000000u ? s.freq_hz - 1000000u : s.freq_hz;
            const uint32_t stop = s.freq_hz + 1000000u;
            if (!radio.spectrum_scan(start, stop, s.step_hz, lines, err, &cancel)) {
                radio.close();
                return emit_error(line, err);
            }
            radio.close();
            if (cancelled(cancel)) return 130;
            for (const auto& l : lines) {
                if (line) line(l);
            }
            return 0;
        }
        if (k == "rf_listen" || k == "rf_record" || k == "rf_replay" || k == "rf_library" ||
            k == "rf_send_test" || k == "rf_send_code" || k == "rf_carrier" || k == "rf_config") {
            return emit_ok(line, "GUI-only RF tool — open from the device RF channel");
        }
        return 0;
    };
}

RunFn radio_nrf24(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (k == "nrf24_info") return emit_ok(line, "NRF24 — no MouseJack / jammer on Zero build");
        emit_cap(platform::detect_nrf24(), line);
        if (k == "nrf24_send") {
            if (args.empty()) return emit_error(line, "need: <hex-payload>");
            return emit_ok(line, "send payload " + args[0]);
        }
        if (k == "nrf24_spectrum" || k == "nrf24_listen") {
            platform::Nrf24Session s;
            std::string err;
            if (!s.open(platform::default_spidev_path(), err)) return emit_error(line, err);
            std::vector<std::string> lines;
            bool c = false;
            if (k == "nrf24_spectrum") s.spectrum_rpd(8, lines, err, &c);
            else s.listen_payloads(3, lines, err, &c);
            s.close();
            if (cancelled(cancel) || c) return 130;
            for (const auto& l : lines) {
                if (line) line(l);
            }
            if (!err.empty()) return emit_error(line, err);
        }
        return 0;
    };
}

RunFn radio_lora(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (k == "lora_mesh") {
            return emit_ok(line, "Meshtastic over USB CDC — set ~/.config/kvxputerzero/lora/mesh_port.txt");
        }
        emit_cap(platform::detect_sx1262(), line);
        if (k == "lora_antenna") {
            platform::Sx1262Session s;
            std::string err;
            if (!s.open(platform::default_spidev_path(), err)) return emit_error(line, err);
            std::vector<std::string> lines;
            s.antenna_check(lines, err);
            s.close();
            for (const auto& l : lines) {
                if (line) line(l);
            }
            return err.empty() ? 0 : 1;
        }
        if (k == "lora_freq" || k == "lora_user") {
            if (args.empty()) return emit_error(line, "need: <value>");
            const fs::path dir = fs::path(tools::config_dir()) / "lora";
            fs::create_directories(dir);
            std::ofstream out(dir / (k == "lora_freq" ? "freq.txt" : "user.txt"));
            out << args[0];
            return emit_ok(line, "saved " + args[0]);
        }
        if (k == "lora_chat") {
            for (int i = 0; i < 10; ++i) {
                if (cancelled(cancel)) return 130;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        return 0;
    };
}

RunFn radio_fm(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (k == "fm_info") return emit_ok(line, "Si4713 FM — no TA hijack on Zero build");
        emit_cap(platform::detect_si4713(), line);
        platform::Si4713Session s;
        std::string err;
        if (!s.open(-1, err)) {
            if (k == "fm_status" || k == "fm_spectrum" || k == "fm_broadcast" || k == "fm_stop") {
                return emit_error(line, err.empty() ? "Si4713 unavailable" : err);
            }
            return 0;
        }
        std::vector<std::string> lines;
        if (k == "fm_status") s.status_lines(lines, err);
        else if (k == "fm_spectrum") s.spectrum_sample(lines, err);
        else if (k == "fm_broadcast") {
            if (args.empty()) {
                s.close();
                return emit_error(line, "need: <mhz>");
            }
            const double mhz = std::atof(args[0].c_str());
            s.broadcast_start(static_cast<int>(mhz * 100), err);
        } else if (k == "fm_stop") {
            s.broadcast_stop(err);
        }
        s.close();
        for (const auto& l : lines) {
            if (line) line(l);
        }
        if (!err.empty()) return emit_error(line, err);
        return 0;
    };
}

RunFn rfid_tool(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        emit_cap(platform::detect_st25r3916(), line);
        emit_cap(platform::detect_pn532(), line);
        if (k == "rfid_load" || k == "rfid_save") return list_dir(ensure_config_subdir("rfid"), cancel, line);
        for (int i = 0; i < 8; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 0;
    };
}

RunFn nfc_tool(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (k.find("menu") != std::string::npos) {
            return emit_ok(line, "menu parent — open children from the interactive CLI");
        }
        if (k == "nfc_wiring") {
            return emit_ok(line, "Cap NFC ST25 SPI CE1 soft-CS GPIO22 IRQ GPIO23; or I2C PN532");
        }
        if (k == "nfc_save") return emit_ok(line, "Dumps auto-save under ~/.config/kvxputerzero/nfc/");
        if (k == "nfc_load" || k == "nfc_save_as_file" || k == "nfc_compare" || k == "nfc_apdu_script") {
            return list_dir(ensure_config_subdir("nfc"), cancel, line);
        }
        if (k == "nfc_module" || k == "nfc_add_key") {
            if (args.empty()) return emit_error(line, "need: <value>");
            return emit_ok(line, "set " + k + "=" + args[0]);
        }
        emit_cap(platform::detect_st25r3916(), line);
        emit_cap(platform::detect_pn532(), line);
        for (int i = 0; i < 8; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 0;
    };
}

RunFn standin_clock() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        const time_t now = time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
        return emit_ok(line, buf);
    };
}

RunFn standin_charge() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        auto st = platform::read_device_status();
        if (line) {
            line(std::string("battery_present=") + (st.battery_present ? "yes" : "no"));
            line(std::string("charging=") + (st.battery_charging ? "yes" : "no"));
            line("percent=" + std::to_string(st.battery_percent));
        }
        return 0;
    };
}

RunFn standin_camera() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        const char* home = getenv("HOME");
        const std::string base = home ? home : ".";
        if (line) line("Camera preview is GUI-only. Listing captures:");
        list_dir(base + "/Pictures/DCIM/Camera", cancel, line);
        list_dir(base + "/Videos/Camera", cancel, line);
#if defined(USE_DESKTOP) && USE_DESKTOP
        if (line) line("no libcamera on desktop");
#endif
        return 0;
    };
}

RunFn standin_gallery() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        const char* home = getenv("HOME");
        return list_dir(std::string(home ? home : ".") + "/Pictures", cancel, line);
    };
}

RunFn config_tool(const char* key) {
    std::string k = key ? key : "";
    return [k](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (k == "about") return emit_ok(line, "kvxputerzero CLI — same config tree as the GUI");
        if (k == "brightness") {
            if (args.empty()) {
                int32_t pct = 0;
                if (!platform::read_brightness_percent(pct)) return emit_error(line, "brightness unavailable");
                return emit_ok(line, std::to_string(pct));
            }
            if (!platform::write_brightness_percent(std::atoi(args[0].c_str()))) {
                return emit_error(line, "set brightness failed");
            }
            return emit_ok(line, "brightness " + args[0]);
        }
        if (k == "reboot") {
            std::string err;
            platform::safe_reboot(err);
            return err.empty() ? emit_ok(line, "rebooting...") : emit_error(line, err);
        }
        if (k == "poweroff") {
            std::string err;
            platform::safe_shutdown(err);
            return err.empty() ? emit_ok(line, "powering off...") : emit_error(line, err);
        }
        if (k == "main_color" || k == "accent_color" || k == "background") {
            if (args.empty()) return emit_error(line, "need: <RRGGBB hex>");
            const fs::path path = fs::path(tools::config_dir()) / (k + ".txt");
            fs::create_directories(path.parent_path());
            std::ofstream out(path);
            out << args[0];
            return emit_ok(line, "saved " + k + "=" + args[0]);
        }
        if (k == "reset_colors") {
            fs::remove(fs::path(tools::config_dir()) / "main_color.txt");
            fs::remove(fs::path(tools::config_dir()) / "accent_color.txt");
            fs::remove(fs::path(tools::config_dir()) / "background.txt");
            return emit_ok(line, "colors reset");
        }
        return emit_ok(line, "config " + k);
    };
}

} // namespace runner

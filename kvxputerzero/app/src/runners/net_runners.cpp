#include "runners_impl.h"
#include "util.h"

#include "ble_scan.h"
#include "ldap_read.h"
#include "net_info.h"
#include "shell_util.h"
#include "sip_options.h"
#include "tcp_probe.h"
#include "user_paths.h"
#include "recon_scan.h"
#include "usb_probe.h"
#include "wifi_monitor.h"
#include "wifi_scan.h"

#include <chrono>
#include <cstdio>
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
    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (cancelled(cancel)) return 130;
        if (line) line(std::string("  ") + ent.path().filename().string());
    }
    return 0;
}

int run_wifi_scan(bool open_only, std::atomic<bool>& cancel, const LineFn& line) {
    bool c = false;
    std::string err;
    auto aps = platform::wifi_scan(err, &c);
    if (cancelled(cancel) || c) return 130;
    if (!err.empty() && aps.empty()) return emit_error(line, err);
    for (const auto& ap : aps) {
        if (open_only && !ap.open) continue;
        if (line) {
            line(ap.ssid + "  ch" + std::to_string(ap.channel) + "  " + std::to_string(ap.rssi) + "dBm  " +
                 (ap.security_raw.empty() ? ap.security : ap.security_raw) + "  " + ap.bssid);
        }
    }
    return 0;
}

int dump_shell(const std::string& cmd, std::atomic<bool>& cancel, const LineFn& line) {
    std::string out;
    const int rc = platform::run_shell(cmd, out);
    std::string cur;
    for (char ch : out) {
        if (cancelled(cancel)) return 130;
        if (ch == '\n') {
            if (line) line(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty() && line) line(cur);
    return rc == 0 ? 0 : 1;
}

} // namespace

RunFn net_wifi_scan(bool open_only) {
    return [open_only](const std::vector<std::string>&, std::atomic<bool>& c, const LineFn& l) {
        return run_wifi_scan(open_only, c, l);
    };
}

RunFn net_wifi_connect() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <ssid> [psk]");
        std::string err;
        bool ok = args.size() == 1 ? platform::wifi_connect_open(args[0], err)
                                   : platform::wifi_connect_psk(args[0], args[1], err);
        if (!ok) return emit_error(line, err.empty() ? "connect failed" : err);
        return emit_ok(line, "connected to " + args[0]);
    };
}

RunFn net_wifi_disconnect() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        const bool radio_off = !args.empty() && (args[0] == "off" || args[0] == "radio_off");
        const bool ok = radio_off ? platform::wifi_radio_off(err) : platform::wifi_disconnect(err);
        if (!ok) return emit_error(line, err.empty() ? "disconnect failed" : err);
        return emit_ok(line, radio_off ? "radio off" : "disconnected");
    };
}

RunFn net_wifi_ap() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (args.size() == 1 && (args[0] == "stop" || args[0] == "down")) {
            std::string err;
            if (!platform::wifi_stop_hotspot(err)) return emit_error(line, err.empty() ? "stop failed" : err);
            return emit_ok(line, "hotspot stopped");
        }
        if (args.size() < 2) return emit_error(line, "need: <ssid> <psk>  or  stop");
        std::string err;
        if (!platform::wifi_start_hotspot(args[0], args[1], err))
            return emit_error(line, err.empty() ? "hotspot failed" : err);
        return emit_ok(line, "hotspot up: " + args[0]);
    };
}

RunFn net_jam_detect() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "Jam Detect is GUI-only (channel bars + threshold alert)");
    };
}

RunFn net_deauth_ids() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        const std::string saved = platform::load_monitor_iface();
        if (platform::monitor_iface_is_onboard(saved)) {
            return emit_error(line, "pick a USB monitor adapter (wifi_monitor_adapter)");
        }
        std::string err;
        if (!platform::run_deauth_ids(saved, cancel, line, err)) {
            return emit_error(line, err.empty() ? "deauth ids failed" : err);
        }
        return 0;
    };
}

RunFn net_badusb_hunter() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        const auto devices = platform::list_usb_devices(err);
        if (!err.empty() && devices.empty()) return emit_error(line, err);
        if (devices.empty()) return emit_ok(line, "no USB devices");
        for (const auto& d : devices) {
            std::string s = d.sys_name + "  " + d.vendor_id + ":" + d.product_id;
            if (!d.product.empty()) s += "  " + d.product;
            if (d.suspicious) s += "  ! " + d.suspicion_reason;
            if (line) line(s);
        }
        return 0;
    };
}

RunFn net_wifi_ap_info() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        auto ifaces = platform::list_net_ifaces(err);
        if (!err.empty() && ifaces.empty()) return emit_error(line, err);
        for (const auto& i : ifaces) {
            if (i.kind != "wifi") continue;
            if (line) {
                line(i.name + "  " + i.state + "  " + i.ipv4 + "/" + std::to_string(i.prefix) + "  gw " +
                     i.gateway);
            }
        }
        return 0;
    };
}

RunFn net_scan_hosts() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        std::string err;
        auto iface = platform::default_route_iface(err);
        if (!args.empty()) iface = platform::find_iface(args[0], err);
        if (iface.name.empty()) return emit_error(line, err.empty() ? "no iface" : err);
        if (line) line("iface " + iface.name + "  " + iface.ipv4);
        return dump_shell("ip -4 neigh show 2>/dev/null | head -n 64", cancel, line);
    };
}

RunFn net_listen_tcp() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <port>");
        if (line) line("Listen TCP on " + args[0] + " — full interactive session is in the GUI");
        for (int i = 0; i < 5; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 0;
    };
}

RunFn net_client_tcp(bool telnet) {
    return [telnet](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host> [port]");
        std::string host = args[0];
        uint16_t port = telnet ? 23 : 80;
        const auto colon = host.find(':');
        if (colon != std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(host.c_str() + colon + 1));
            host.resize(colon);
        } else if (args.size() >= 2) {
            port = static_cast<uint16_t>(std::atoi(args[1].c_str()));
        }
        auto r = platform::tcp_probe(host, port, 2000, true);
        if (!r.error.empty() && !r.open) return emit_error(line, r.error);
        if (line) {
            line(host + ":" + std::to_string(port) + (r.open ? " open" : " closed"));
            if (!r.banner.empty()) line(r.banner);
        }
        return r.open ? 0 : 1;
    };
}

RunFn net_ping() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host>");
        return dump_shell("ping -c 3 -W 2 " + args[0] + " 2>&1", cancel, line);
    };
}

RunFn net_shell(const char*, const char* prefix) {
    std::string p = prefix ? prefix : "";
    return [p](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need args for " + p);
        std::string cmd = p;
        for (const auto& a : args) {
            cmd.push_back(' ');
            cmd += a;
        }
        cmd += " 2>&1";
        return dump_shell(cmd, cancel, line);
    };
}

RunFn net_upnp() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        return dump_shell("timeout 3 upnpc -l 2>&1 || echo 'upnpc unavailable'", cancel, line);
    };
}

RunFn net_lan_watch() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        for (int i = 0; i < 10; ++i) {
            if (cancelled(cancel)) return 130;
            std::string err;
            auto iface = platform::default_route_iface(err);
            if (line) line(iface.name.empty() ? err : (iface.name + " " + iface.ipv4 + " " + iface.state));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    };
}

RunFn net_eth_link() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        for (const auto& i : platform::list_net_ifaces(err)) {
            if (i.kind != "ethernet") continue;
            if (line) line(i.name + "  " + i.state + "  " + i.ipv4);
        }
        return 0;
    };
}

RunFn net_ble_scan() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        std::vector<platform::BleDevice> devices;
        std::string err;
        bool c = false;
        if (!platform::ble_scan_once(devices, err, 5, &c)) {
            if (cancelled(cancel) || c) return 130;
            return emit_error(line, err.empty() ? "ble scan failed" : err);
        }
        for (const auto& d : devices) {
            if (line) {
                line(d.address + "  " + std::to_string(d.rssi) + "  " +
                     (d.name.empty() ? "(no name)" : d.name));
            }
        }
        return 0;
    };
}

RunFn net_ble_status(const char* name_substr, const char* uuid_substr) {
    std::string n = name_substr ? name_substr : "";
    std::string u = uuid_substr ? uuid_substr : "";
    return [n, u](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        bool c = false;
        auto r = platform::ble_status_probe(n.c_str(), u.c_str(), 5, &c);
        if (cancelled(cancel) || c) return 130;
        for (const auto& s : r.lines) {
            if (line) line(s);
        }
        if (!r.ok) return emit_error(line, r.error.empty() ? "probe failed" : r.error);
        return 0;
    };
}

RunFn net_ble_disconnect() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        std::string err;
        if (!platform::ble_power_off(err)) return emit_error(line, err.empty() ? "ble power off failed" : err);
        return emit_ok(line, "bluetooth powered off");
    };
}

RunFn net_sip_options() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host>");
        bool c = false;
        auto r = platform::sip_options(args[0], 2000, &c);
        if (cancelled(cancel) || c) return 130;
        if (!r.ok) return emit_error(line, r.error.empty() ? r.status_line : r.error);
        return emit_ok(line, r.status_line);
    };
}

RunFn net_ldap_read() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host> [base_dn]");
        const std::string base = args.size() >= 2 ? args[1] : "";
        bool c = false;
        auto r = platform::ldap_read(args[0], base, "", "", 3000, &c);
        if (cancelled(cancel) || c) return 130;
        for (const auto& s : r.lines) {
            if (line) line(s);
        }
        if (!r.ok) return emit_error(line, r.error.empty() ? "ldap failed" : r.error);
        return 0;
    };
}

RunFn net_web_crawler() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host|url>");
        auto paths = platform::read_path_list(tools::config_file("crawler_wordlist.txt"));
        if (paths.empty()) paths = platform::default_crawler_paths();
        const auto r = platform::web_crawl(
            args[0], paths, [&](const std::string& s) { if (line) line(s); },
            [&]() { return cancelled(cancel); });
        if (cancelled(cancel)) return 130;
        return emit_ok(line, "tried " + std::to_string(r.tried) + "  hits " + std::to_string(r.hits));
    };
}

RunFn net_cctv() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        std::string seed = args.empty() ? std::string() : args[0];
        if (seed.empty()) {
            std::string err;
            const auto iface = platform::default_route_iface(err);
            seed = !iface.gateway.empty() ? iface.gateway : iface.ipv4;
        }
        if (seed.empty()) return emit_error(line, "need: <seed-ip> (no default route found)");
        auto paths = platform::read_path_list(tools::config_file("cctv_paths.txt"));
        if (paths.empty()) paths = platform::default_cctv_paths();
        const auto r = platform::http_sweep(
            seed, paths, [&](const std::string& s) { if (line) line(s); },
            [&]() { return cancelled(cancel); });
        if (cancelled(cancel)) return 130;
        return emit_ok(line, "tried " + std::to_string(r.tried) + "  hosts " + std::to_string(r.hits));
    };
}

RunFn net_ssh_hint() { return tools_info("Use the launcher SSH client"); }
RunFn net_files_share_hint() {
    return tools_info("File Share lives under WiFi → File Share (kvxputerzero-cli run wifi file_share)");
}

RunFn net_wireguard() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        std::string cmd = "wg show";
        if (!args.empty()) {
            cmd = "wg";
            for (const auto& a : args) {
                cmd.push_back(' ');
                cmd += a;
            }
        }
        cmd += " 2>&1";
        return dump_shell(cmd, cancel, line);
    };
}

RunFn net_dead_drop() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "Dead Drop root: " + ensure_config_subdir("dead-drop"));
    };
}

RunFn net_honeypot() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        if (line) line("HoneyPot stub — Ctrl-C to stop");
        for (int i = 0; i < 30; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 0;
    };
}

RunFn net_roku() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty() || args[0] == "find") {
            return dump_shell("timeout 3 avahi-browse -rt _roku._tcp 2>&1 | head -n 40", cancel, line);
        }
        return emit_ok(line, "roku interactive keys: use GUI Roku Remote");
    };
}

RunFn net_printer() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <ip>");
        auto r = platform::tcp_probe(args[0], 9100, 1500, false);
        if (line) line(args[0] + ":9100 " + (r.open ? "open" : "closed"));
        return r.open ? 0 : 1;
    };
}

RunFn net_socks4() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (line) line("SOCKS4 on " + (args.empty() ? std::string("1080") : args[0]));
        for (int i = 0; i < 10; ++i) {
            if (cancelled(cancel)) return 130;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 0;
    };
}

RunFn net_file_share() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        const std::string root = ensure_config_subdir("file-share");
        if (line) {
            line("File Share root: " + root);
            line("python3 -m http.server 8080 --directory " + root);
        }
        return 0;
    };
}

RunFn net_wifi_monitor_adapter() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        const fs::path path = fs::path(tools::config_dir()) / "wifi_monitor_iface";
        if (!args.empty()) {
            std::ofstream out(path);
            out << args[0];
            return emit_ok(line, "saved monitor iface: " + args[0]);
        }
        std::ifstream in(path);
        std::string cur;
        if (std::getline(in, cur) && !cur.empty()) return emit_ok(line, "current: " + cur);
        return emit_ok(line, "current: onboard");
    };
}

RunFn net_wifi_analyzer() {
    return [](const std::vector<std::string>&, std::atomic<bool>& c, const LineFn& l) {
        return run_wifi_scan(false, c, l);
    };
}

RunFn net_ap_detail() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <bssid-or-ssid>");
        bool c = false;
        std::string err;
        auto aps = platform::wifi_scan(err, &c);
        if (cancelled(cancel) || c) return 130;
        if (!err.empty() && aps.empty()) return emit_error(line, err);
        for (const auto& ap : aps) {
            if (ap.ssid.find(args[0]) == std::string::npos && ap.bssid.find(args[0]) == std::string::npos) {
                continue;
            }
            if (line) {
                line(ap.ssid + "  " + ap.bssid + "  ch" + std::to_string(ap.channel) + "  " +
                     std::to_string(ap.rssi));
            }
        }
        return 0;
    };
}

RunFn net_loot_console() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        return list_dir(ensure_config_subdir("loot"), cancel, line);
    };
}

RunFn net_check_handshakes() {
    return [](const std::vector<std::string>&, std::atomic<bool>& cancel, const LineFn& line) {
        return list_dir(ensure_config_subdir("handshakes"), cancel, line);
    };
}

RunFn net_ntlm_clean() {
    return [](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, "NTLM files under ~/.config/kvxputerzero/loot — use GUI for clean");
    };
}

RunFn net_smb_status() {
    return [](const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <host>");
        return dump_shell("smbclient -L " + args[0] + " -N 2>&1 | head -n 40", cancel, line);
    };
}

RunFn net_pineap() {
    return [](const std::vector<std::string>&, std::atomic<bool>& c, const LineFn& l) {
        return run_wifi_scan(false, c, l);
    };
}

RunFn net_uart_shell() {
    return [](const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
        if (args.empty()) return emit_error(line, "need: <device> e.g. /dev/ttyUSB0");
        return emit_ok(line, "UART: screen " + args[0] + " 115200  (or GUI UART Shell)");
    };
}

} // namespace runner

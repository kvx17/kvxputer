#include "cap_screens.h"

#include "cap_detect.h"
#include "cap_power.h"
#include "cc1101.h"
#include "nrf24_radio.h"
#include "doc_screen.h"
#include "info_screen.h"
#include "options_list_screen.h"
#include "pahub.h"
#include "pn532_bus.h"
#include "st25r3916.h"
#include "prompt_screen.h"
#include "result_list_screen.h"
#include "run_screen.h"
#include "si4713.h"
#include "spi_device.h"
#include "meshtastic_serial.h"
#include "sx1262.h"
#include "uart_session.h"
#include "user_paths.h"
#include "w1_ibutton.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
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

void nav(VM& vm, ToolId id, const std::string& stage, const std::string& arg = {}) {
    const auto prev = vm.current_page();
    std::string param = stage;
    if (!arg.empty()) param += ":" + arg;
    vm.open_tool(id, param);
    if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
}

Screen make_options(VM& vm, AssetManager& assets, const std::string& title, std::vector<MenuEntry> entries) {
    if (entries.empty()) entries.push_back({"(empty)", [&vm]() { vm.go_back(); }});
    return std::make_unique<screen::OptionsListScreen>(vm, assets, title, std::move(entries));
}

Screen make_results(VM& vm, AssetManager& assets, const std::string& title, std::vector<std::string> lines) {
    return std::make_unique<screen::ResultListScreen>(vm, assets, title, std::move(lines), nullptr);
}

Screen make_info(VM& vm, AssetManager& assets, const std::string& title, const std::string& text) {
    return std::make_unique<screen::DocScreen>(vm, assets, title, text);
}

Screen make_info_card(VM& vm, AssetManager& assets, const std::string& title,
                      screen::InfoScreen::Fields fields, const std::string& status = "OK",
                      view::StatusPillKind kind = view::StatusPillKind::Ok) {
    return std::make_unique<screen::InfoScreen>(vm, assets, title, std::move(fields), status, kind);
}

Screen make_info_card(VM& vm, AssetManager& assets, const std::string& title,
                      screen::InfoScreen::WorkerFn worker) {
    return std::make_unique<screen::InfoScreen>(vm, assets, title, std::move(worker));
}

Screen make_run(VM& vm, AssetManager& assets, const std::string& title, Worker worker,
                screen::RunScreen::DoneFn on_done = {}) {
    return std::make_unique<screen::RunScreen>(vm, assets, title, std::move(worker), std::move(on_done));
}

std::string mesh_node_line(const platform::MeshNode& n) {
    char id[16];
    std::snprintf(id, sizeof(id), "!%08x", static_cast<unsigned>(n.num));
    std::ostringstream oss;
    oss << id;
    if (!n.short_name.empty()) oss << "  " << n.short_name;
    if (!n.long_name.empty()) oss << "  " << n.long_name;
    if (n.last_heard != 0) oss << "  heard " << n.last_heard;
    if (n.has_snr) {
        char snr[16];
        std::snprintf(snr, sizeof(snr), "  snr %.1f", static_cast<double>(n.snr));
        oss << snr;
    }
    if (n.has_rssi) oss << "  rssi " << n.rssi;
    return oss.str();
}

std::string mesh_chat_line(const platform::MeshChatLine& m) {
    std::ostringstream oss;
    oss << (m.from_label.empty() ? "?" : m.from_label) << ": " << m.text;
    if (m.has_snr) {
        char snr[16];
        std::snprintf(snr, sizeof(snr), "  (snr %.1f)", static_cast<double>(m.snr));
        oss << snr;
    }
    if (m.has_rssi) oss << "  (rssi " << m.rssi << ")";
    return oss.str();
}

Screen make_prompt(VM& vm, AssetManager& assets, const std::string& title, const std::string& hint,
                   const std::string& initial, screen::PromptScreen::ConfirmFn fn) {
    return std::make_unique<screen::PromptScreen>(vm, assets, title, hint, initial, std::move(fn));
}

MenuEntry stage_entry(VM& vm, ToolId id, const std::string& label, const std::string& stage,
                      const std::string& arg = {}) {
    return {label, [&vm, id, stage, arg]() { nav(vm, id, stage, arg); }};
}

bool* cancel_ptr(Cancel& c) {
    return reinterpret_cast<bool*>(&c);
}

std::string ensure_subdir(const std::string& name) {
    const fs::path p = fs::path(tools::config_dir()) / name;
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string read_config_value(const char* name, const std::string& def) {
    std::ifstream in(tools::config_file(name));
    if (!in) return def;
    std::string v;
    std::getline(in, v);
    v = trim(v);
    return v.empty() ? def : v;
}

void write_config_value(const char* name, const std::string& v) {
    std::ofstream out(tools::config_file(name), std::ios::trunc);
    if (out) out << v << "\n";
}

std::vector<uint8_t> parse_hex_bytes(const std::string& s) {
    std::vector<uint8_t> out;
    std::string hex;
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c))) hex.push_back(static_cast<char>(std::toupper(c)));
    }
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return out;
}

// --- Cap Probe ---------------------------------------------------------------------------

Screen make_cap_probe(VM& vm, AssetManager& assets) {
    return make_info_card(vm, assets, "Cap Probe", [](Cancel& cancel, screen::InfoScreen::Fields& fields,
                                                      std::string& status, view::StatusPillKind& kind) {
        for (const auto& line : platform::query_cap_power_status().lines) {
            fields.push_back({"power", line});
        }
        std::string err;
        if (!platform::enable_cap_power(platform::CapKindHint::Generic, err, &cancel) && !err.empty()) {
            fields.push_back({"power", err});
        }
        int present = 0;
        for (const auto& c : platform::detect_all_caps()) {
            if (c.present) {
                ++present;
                fields.push_back({c.name, c.path + (c.detail.empty() ? "" : "  " + c.detail)});
            } else {
                fields.push_back({c.name, c.error.empty() ? "missing" : c.error});
            }
        }
        const auto spis = platform::list_spidev();
        if (spis.empty()) fields.push_back({"spidev", "none"});
        for (const auto& p : spis) fields.push_back({"spidev", p});
        platform::release_cap_power(true);
        status = present > 0 ? (std::to_string(present) + " present") : "none found";
        kind = present > 0 ? view::StatusPillKind::Ok : view::StatusPillKind::Missing;
    });
}

// --- RF ----------------------------------------------------------------------------------

Screen make_rf_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    if (id == ToolId::RfStatus) {
        return make_info_card(vm, assets, "Cap Status", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                       std::string& status, view::StatusPillKind& kind) {
            const auto c = platform::detect_cc1101();
            if (!c.present) {
                fields.push_back({"chip", "CC1101"});
                fields.push_back({"status", c.error.empty() ? "not present" : c.error});
                status = "missing";
                kind = view::StatusPillKind::Missing;
                return;
            }
            fields.push_back({"chip", "CC1101"});
            fields.push_back({"path", c.path});
            if (!c.detail.empty()) fields.push_back({"detail", c.detail});
            status = "OK";
            kind = view::StatusPillKind::Ok;
        });
    }
    if (id == ToolId::RfSpectrum) {
        return make_run(vm, assets, "RF Spectrum", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::Cc1101Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            status("scanning 433–435 MHz");
            std::vector<std::string> lines;
            if (!s.spectrum_scan(433000000, 435000000, 100000, lines, err, cancel_ptr(cancel))) {
                log(err);
                return;
            }
            for (const auto& l : lines) log(l);
            s.close();
        });
    }
    if (id == ToolId::RfListen) {
        return make_run(vm, assets, "RF Listen", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::Cc1101Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            const uint32_t hz = static_cast<uint32_t>(std::strtoul(read_config_value("rf_freq_hz.txt", "433920000").c_str(), nullptr, 10));
            s.set_frequency_hz(hz, err);
            status("RSSI listen");
            std::vector<int> samples;
            if (!s.listen_rssi(60, 50, samples, err, cancel_ptr(cancel))) {
                log(err);
                return;
            }
            for (int v : samples) log(std::to_string(v) + " dBm");
            s.close();
        });
    }
    if (id == ToolId::RfRecord) {
        return make_run(vm, assets, "RF Record", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::Cc1101Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            const uint32_t hz = static_cast<uint32_t>(std::strtoul(read_config_value("rf_freq_hz.txt", "433920000").c_str(), nullptr, 10));
            s.set_frequency_hz(hz, err);
            status("recording");
            std::vector<int> samples;
            if (!s.listen_rssi(80, 20, samples, err, cancel_ptr(cancel))) {
                log(err);
                return;
            }
            const std::string dir = ensure_subdir("rf");
            const std::string path = dir + "/rec_" + std::to_string(std::time(nullptr)) + ".sub";
            if (!platform::write_sub_raw(path, hz, samples, err)) {
                log(err);
                return;
            }
            log("Saved " + path);
            s.close();
        });
    }
    if (id == ToolId::RfLibrary || id == ToolId::RfReplay) {
        const std::string dir = ensure_subdir("rf");
        if (st.name == "tx" && !st.arg.empty()) {
            return make_run(vm, assets, "RF TX", [name = st.arg](Cancel&, const Log& log, const Status&) {
                platform::Cc1101Session s;
                std::string err;
                if (!s.open({}, err)) {
                    log(err);
                    return;
                }
                const uint8_t payload[] = {'K', 'V', 'X', 1, 2, 3};
                if (!s.send_test_packet(payload, sizeof(payload), err)) log(err);
                else log("Test TX for " + name + " (FIFO packet)");
                s.close();
            });
        }
        std::vector<std::string> names;
        std::string err;
        platform::list_sub_files(dir, names, err);
        std::vector<MenuEntry> e;
        for (const auto& n : names) e.push_back(stage_entry(vm, id, n, "tx", n));
        if (e.empty()) e.push_back({"(no .sub in rf/)", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, id == ToolId::RfLibrary ? "Protocol Library" : "Replay", std::move(e));
    }
    if (id == ToolId::RfSendTest) {
        return make_run(vm, assets, "RF Send Test", [](Cancel&, const Log& log, const Status&) {
            platform::Cc1101Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            const uint8_t payload[] = {'T', 'E', 'S', 'T'};
            if (!s.send_test_packet(payload, sizeof(payload), err)) log(err);
            else log("Test packet queued");
            s.close();
        });
    }
    return nullptr;
}

// --- NRF24 -------------------------------------------------------------------------------

Screen make_nrf_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    if (id == ToolId::Nrf24Info) {
        return make_info(vm, assets, "NRF24",
                         "Study-only disclaimer.\nNeeds nRF24L01+ on SPI.\nMouseJack / jammer stay out.");
    }
    if (id == ToolId::Nrf24Status) {
        return make_info_card(vm, assets, "nRF24 Status", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                             std::string& status, view::StatusPillKind& kind) {
            const auto c = platform::detect_nrf24();
            if (!c.present) {
                fields.push_back({"chip", "nRF24"});
                fields.push_back({"status", c.error.empty() ? "not present" : c.error});
                status = "missing";
                kind = view::StatusPillKind::Missing;
                return;
            }
            fields.push_back({"name", c.name});
            fields.push_back({"path", c.path});
            if (!c.detail.empty()) fields.push_back({"detail", c.detail});
            status = "OK";
            kind = view::StatusPillKind::Ok;
        });
    }
    if (id == ToolId::Nrf24Spectrum) {
        return make_run(vm, assets, "nRF24 Spectrum", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::Nrf24Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            status("RPD scan");
            std::vector<std::string> lines;
            if (!s.spectrum_rpd(40, lines, err, cancel_ptr(cancel))) {
                log(err);
                return;
            }
            for (const auto& l : lines) log(l);
            s.close();
        });
    }
    if (id == ToolId::Nrf24Listen) {
        return make_run(vm, assets, "nRF24 Listen", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::Nrf24Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            status("listening 8s");
            std::vector<std::string> lines;
            if (!s.listen_payloads(8, lines, err, cancel_ptr(cancel))) {
                log(err);
                return;
            }
            for (const auto& l : lines) log(l);
            s.close();
        });
    }
    if (id == ToolId::Nrf24Send) {
        if (st.name == "run") {
            return make_run(vm, assets, "nRF24 Send", [hex = st.arg](Cancel&, const Log& log, const Status&) {
                platform::Nrf24Session s;
                std::string err;
                if (!s.open({}, err)) {
                    log(err);
                    return;
                }
                const auto bytes = parse_hex_bytes(hex);
                if (bytes.empty()) {
                    log("bad hex");
                    return;
                }
                if (!s.send_payload(bytes.data(), bytes.size(), err)) log(err);
                else log("Payload written (CE GPIO needed for air TX)");
                s.close();
            });
        }
        return make_prompt(vm, assets, "Send Payload", "hex bytes", "010203",
                           [&vm](const std::string& v) { nav(vm, ToolId::Nrf24Send, "run", trim(v)); });
    }
    return nullptr;
}

// --- LoRa --------------------------------------------------------------------------------

Screen make_lora_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    if (id == ToolId::LoraStatus) {
        return make_info_card(vm, assets, "Cap Status", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                          std::string& status, view::StatusPillKind& kind) {
            const auto c = platform::detect_sx1262();
            if (!c.present) {
                fields.push_back({"chip", "SX1262"});
                fields.push_back({"status", c.error.empty() ? "not present" : c.error});
                status = "missing";
                kind = view::StatusPillKind::Missing;
                return;
            }
            fields.push_back({"name", c.name});
            fields.push_back({"path", c.path});
            if (!c.detail.empty()) fields.push_back({"detail", c.detail});
            fields.push_back({"freq", read_config_value("lora_freq_mhz.txt", "868") + " MHz"});
            fields.push_back({"user", read_config_value("lora_user.txt", "kvx")});
            status = "OK";
            kind = view::StatusPillKind::Ok;
        });
    }
    if (id == ToolId::LoraAntenna) {
        return make_run(vm, assets, "Antenna Check", [](Cancel&, const Log& log, const Status&) {
            platform::Sx1262Session s;
            std::string err;
            if (!s.open({}, err)) {
                log(err);
                return;
            }
            std::vector<std::string> lines;
            if (!s.antenna_check(lines, err)) {
                log(err);
                return;
            }
            for (const auto& l : lines) log(l);
            s.close();
        });
    }
    if (id == ToolId::LoraChat) {
        if (st.name == "rx") {
            return make_run(vm, assets, "LoRa RX", [](Cancel& cancel, const Log& log, const Status& status) {
                platform::Sx1262Session s;
                std::string err;
                if (!s.open({}, err)) {
                    log(err);
                    return;
                }
                status("RX 15s");
                std::string out;
                if (!s.receive_text(15000, out, err, cancel_ptr(cancel))) log(err);
                else log("RX: " + out);
                s.close();
            });
        }
        if (st.name == "tx") {
            return make_run(vm, assets, "LoRa TX", [msg = st.arg](Cancel&, const Log& log, const Status&) {
                platform::Sx1262Session s;
                std::string err;
                if (!s.open({}, err)) {
                    log(err);
                    return;
                }
                const std::string user = read_config_value("lora_user.txt", "kvx");
                if (!s.send_text(user + ": " + msg, err)) log(err);
                else log("TX ok");
                s.close();
            });
        }
        if (st.name == "ask") {
            return make_prompt(vm, assets, "LoRa Chat", "message", "",
                               [&vm](const std::string& v) { nav(vm, ToolId::LoraChat, "tx", trim(v)); });
        }
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "Send...", "ask"));
        e.push_back(stage_entry(vm, id, "Receive 15s", "rx"));
        return make_options(vm, assets, "LoRa Chat", std::move(e));
    }
    if (id == ToolId::LoraFreq) {
        if (st.name == "set") {
            write_config_value("lora_freq_mhz.txt", st.arg);
            return make_info(vm, assets, "Frequency", "Saved " + st.arg + " MHz");
        }
        return make_prompt(vm, assets, "LoRa Frequency", "MHz", read_config_value("lora_freq_mhz.txt", "868"),
                           [&vm](const std::string& v) { nav(vm, ToolId::LoraFreq, "set", trim(v)); });
    }
    if (id == ToolId::LoraUser) {
        if (st.name == "set") {
            write_config_value("lora_user.txt", st.arg);
            return make_info(vm, assets, "Username", "Saved " + st.arg);
        }
        return make_prompt(vm, assets, "LoRa Username", "callsign", read_config_value("lora_user.txt", "kvx"),
                           [&vm](const std::string& v) { nav(vm, ToolId::LoraUser, "set", trim(v)); });
    }
    if (id == ToolId::LoraMeshHint) {
        if (st.name == "menu") {
            std::vector<MenuEntry> e;
            e.push_back(stage_entry(vm, id, "Nodes", "nodes"));
            e.push_back(stage_entry(vm, id, "Chat", "chat"));
            e.push_back(stage_entry(vm, id, "Port", "port"));
            return make_options(vm, assets, "Meshtastic", std::move(e));
        }
        if (st.name == "portset") {
            ensure_subdir("lora");
            write_config_value("lora/mesh_port.txt", st.arg);
            return make_info(vm, assets, "Meshtastic Port",
                             st.arg.empty() ? "Cleared mesh_port.txt" : ("Saved " + st.arg));
        }
        if (st.name == "port") {
            return make_prompt(vm, assets, "Mesh Port", "/dev/ttyACM0 or empty",
                               read_config_value("lora/mesh_port.txt", ""),
                               [&vm](const std::string& v) { nav(vm, ToolId::LoraMeshHint, "portset", trim(v)); });
        }
        if (st.name == "nodes") {
            return make_run(vm, assets, "Mesh Nodes", [](Cancel& cancel, const Log& log, const Status& status) {
                const auto info = platform::probe_meshtastic_serial(&cancel);
                if (!info.present) {
                    log(info.error.empty() ? "No mesh serial API" : info.error);
                    return;
                }
                platform::MeshtasticSerial mesh;
                std::string err;
                if (!mesh.open(info, err)) {
                    log(err);
                    return;
                }
                status(info.path);
                if (info.mode == platform::MeshSerialMode::Text) {
                    log("TEXTMSG has no node table");
                    mesh.close();
                    return;
                }
                std::vector<platform::MeshNode> nodes;
                if (!mesh.list_nodes(nodes, err, &cancel)) {
                    log(err.empty() ? "node list failed" : err);
                    mesh.close();
                    return;
                }
                if (nodes.empty()) log("(no nodes yet)");
                for (const auto& n : nodes) log(mesh_node_line(n));
                mesh.close();
            });
        }
        if (st.name == "chat") {
            std::vector<MenuEntry> e;
            e.push_back(stage_entry(vm, id, "Send...", "chatask"));
            e.push_back(stage_entry(vm, id, "Listen", "chatrx"));
            return make_options(vm, assets, "Mesh Chat", std::move(e));
        }
        if (st.name == "chatask") {
            return make_prompt(vm, assets, "Mesh Chat", "message", "",
                               [&vm](const std::string& v) { nav(vm, ToolId::LoraMeshHint, "chattx", trim(v)); });
        }
        if (st.name == "chattx") {
            return make_run(vm, assets, "Mesh TX", [msg = st.arg](Cancel& cancel, const Log& log, const Status& status) {
                const auto info = platform::probe_meshtastic_serial(&cancel);
                if (!info.present) {
                    log(info.error.empty() ? "No mesh serial API" : info.error);
                    return;
                }
                platform::MeshtasticSerial mesh;
                std::string err;
                if (!mesh.open(info, err)) {
                    log(err);
                    return;
                }
                status(info.path);
                if (!mesh.send_text(msg, err)) log(err);
                else log("TX: " + msg);
                mesh.close();
            });
        }
        if (st.name == "chatrx") {
            return make_run(vm, assets, "Mesh RX", [](Cancel& cancel, const Log& log, const Status& status) {
                const auto info = platform::probe_meshtastic_serial(&cancel);
                if (!info.present) {
                    log(info.error.empty() ? "No mesh serial API" : info.error);
                    return;
                }
                platform::MeshtasticSerial mesh;
                std::string err;
                if (!mesh.open(info, err)) {
                    log(err);
                    return;
                }
                status(info.path + "  Esc stop");
                log("Listening...");
                mesh.listen_chat(
                    [&log](const platform::MeshChatLine& m) { log(mesh_chat_line(m)); }, err, cancel);
                mesh.close();
                if (!err.empty() && err != "cancelled") log(err);
            });
        }
        // Default: probe; open Nodes/Chat menu only when an API answers.
        auto ok = std::make_shared<bool>(false);
        return make_run(
            vm, assets, "Meshtastic",
            [ok](Cancel& cancel, const Log& log, const Status& status) {
                status("probing...");
                const auto info = platform::probe_meshtastic_serial(&cancel);
                if (!info.present) {
                    log(info.error.empty() ? "No mesh serial API" : info.error);
                    return;
                }
                *ok = true;
                log(info.mode == platform::MeshSerialMode::Text ? "TEXTMSG" : "PhoneAPI");
                log(info.path);
                log(std::to_string(info.baud) + " baud");
            },
            [&vm, ok](bool cancelled) {
                if (!cancelled && ok && *ok) nav(vm, ToolId::LoraMeshHint, "menu");
            });
    }
    return nullptr;
}

// --- FM ----------------------------------------------------------------------------------

Screen make_fm_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    if (id == ToolId::FmInfo) {
        return make_info(vm, assets, "FM", "Needs Si4713 on I2C.\nTA hijack stays out.");
    }
    if (id == ToolId::FmStatus) {
        return make_info_card(vm, assets, "FM Status", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                          std::string& status, view::StatusPillKind& kind) {
            platform::Si4713Session s;
            std::string err;
            if (!s.open(-1, err)) {
                fields.push_back({"chip", "Si4713"});
                fields.push_back({"status", err.empty() ? "not present" : err});
                status = "missing";
                kind = view::StatusPillKind::Missing;
                return;
            }
            std::vector<std::string> lines;
            s.status_lines(lines, err);
            int i = 0;
            for (const auto& l : lines) {
                fields.push_back({i == 0 ? "status" : ("#" + std::to_string(i)), l});
                ++i;
            }
            if (fields.empty()) fields.push_back({"chip", "Si4713"});
            s.close();
            status = "OK";
            kind = view::StatusPillKind::Ok;
        });
    }
    if (id == ToolId::FmBroadcast) {
        if (st.name == "run") {
            return make_run(vm, assets, "FM Broadcast", [freq = st.arg](Cancel&, const Log& log, const Status&) {
                platform::Si4713Session s;
                std::string err;
                if (!s.open(-1, err)) {
                    log(err);
                    return;
                }
                const int f = std::atoi(freq.c_str());
                if (!s.broadcast_start(f, err)) log(err);
                else log("TX tune " + freq);
                s.close();
            });
        }
        return make_prompt(vm, assets, "FM Broadcast", "freq 10kHz units (8810=88.10)", "8810",
                           [&vm](const std::string& v) { nav(vm, ToolId::FmBroadcast, "run", trim(v)); });
    }
    if (id == ToolId::FmStop) {
        return make_run(vm, assets, "FM Stop", [](Cancel&, const Log& log, const Status&) {
            platform::Si4713Session s;
            std::string err;
            if (!s.open(-1, err)) {
                log(err);
                return;
            }
            if (!s.broadcast_stop(err)) log(err);
            else log("Powered down");
            s.close();
        });
    }
    if (id == ToolId::FmSpectrum) {
        return make_run(vm, assets, "FM Spectrum", [](Cancel&, const Log& log, const Status&) {
            platform::Si4713Session s;
            std::string err;
            if (!s.open(-1, err)) {
                log(err);
                return;
            }
            std::vector<std::string> lines;
            s.spectrum_sample(lines, err);
            for (const auto& l : lines) log(l);
            s.close();
        });
    }
    return nullptr;
}

// --- RFID / NFC --------------------------------------------------------------------------

Screen make_tag_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    const bool rfid = (id == ToolId::RfidStatus || id == ToolId::RfidRead || id == ToolId::RfidScan ||
                       id == ToolId::RfidLoad || id == ToolId::RfidSave);
    const std::string dir = ensure_subdir(rfid ? "rfid" : "nfc");

    auto status_id = rfid ? ToolId::RfidStatus : ToolId::NfcStatus;
    auto read_id = rfid ? ToolId::RfidRead : ToolId::NfcRead;
    auto scan_id = rfid ? ToolId::RfidScan : ToolId::NfcScan;
    auto load_id = rfid ? ToolId::RfidLoad : ToolId::NfcLoad;
    auto save_id = rfid ? ToolId::RfidSave : ToolId::NfcSave;
    (void)status_id;
    (void)read_id;
    (void)scan_id;
    (void)save_id;

    if (id == ToolId::RfidStatus || id == ToolId::NfcStatus) {
        const char* title = rfid ? "RFID Status" : "NFC Status";
        return make_info_card(vm, assets, title, [](Cancel&, screen::InfoScreen::Fields& fields,
                                                    std::string& status, view::StatusPillKind& kind) {
            const auto st25 = platform::detect_st25r3916();
            if (st25.present) {
                fields.push_back({"chip", "ST25R3916"});
                if (!st25.path.empty()) fields.push_back({"path", st25.path});
                if (!st25.detail.empty()) fields.push_back({"detail", st25.detail});
                fields.push_back({"bus", "SPI + GPIO22 soft-CS (mode 1)"});
                status = "OK";
                kind = view::StatusPillKind::Ok;
                return;
            }
            const auto c = platform::detect_pn532();
            if (!c.present) {
                fields.push_back({"chip", "ST25R3916 / PN532"});
                fields.push_back({"ST25", st25.error.empty() ? "not present" : st25.error});
                fields.push_back({"PN532", c.error.empty() ? "not present" : c.error});
                fields.push_back({"hint", "Cap-CC1101 NFC is ST25R3916 on SPI; PN532 is I2C 0x24"});
                status = "missing";
                kind = view::StatusPillKind::Missing;
                return;
            }
            platform::Pn532Session s;
            std::string err;
            if (!s.open(-1, err)) {
                fields.push_back({"chip", "PN532"});
                fields.push_back({"status", err});
                status = "error";
                kind = view::StatusPillKind::Error;
                return;
            }
            fields.push_back({"chip", "PN532"});
            if (!c.path.empty()) fields.push_back({"path", c.path});
            std::string fw;
            if (s.get_firmware(fw, err)) fields.push_back({"firmware", fw});
            else fields.push_back({"firmware", err.empty() ? "?" : err});
            s.close();
            status = "OK";
            kind = view::StatusPillKind::Ok;
        });
    }
    if (id == ToolId::RfidRead || id == ToolId::NfcRead || id == ToolId::RfidScan || id == ToolId::NfcScan) {
        return make_run(vm, assets, "Read tag", [dir](Cancel& cancel, const Log& log, const Status& status) {
            std::string err;
            {
                platform::St25Session st;
                if (st.open({}, err)) {
                    status("ST25 present tag...");
                    std::string uid;
                    if (st.read_uid(uid, err, cancel_ptr(cancel))) {
                        log(uid);
                        const std::string path = dir + "/st25_" + std::to_string(std::time(nullptr)) + ".txt";
                        std::ofstream out(path);
                        if (out) {
                            out << uid << '\n';
                            log("Saved " + path);
                        }
                        st.close();
                        return;
                    }
                    log(err.empty() ? "ST25 read failed" : err);
                    st.close();
                    return;
                }
            }
            platform::Pn532Session s;
            if (!s.open(-1, err)) {
                log(err);
                return;
            }
            status("present tag...");
            std::string uid;
            if (!s.read_uid(uid, err, cancel_ptr(cancel), 10000)) {
                log(err);
                return;
            }
            log("UID " + uid);
            const std::string path = dir + "/uid_" + uid + ".txt";
            if (s.save_dump(path, uid, err)) log("Saved " + path);
            s.close();
        });
    }
    if (id == ToolId::RfidLoad || id == ToolId::NfcLoad) {
        std::vector<MenuEntry> e;
        std::error_code ec;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            if (!de.is_regular_file()) continue;
            const std::string n = de.path().filename().string();
            e.push_back({n, [&vm, path = de.path().string()]() {
                             std::ifstream in(path);
                             std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                             vm.open_tool(ToolId::None, "info:" + all);
                             if (vm.current_page() == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
                         }});
        }
        if (e.empty()) e.push_back({"(no dumps)", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, "Load", std::move(e));
    }
    if (id == ToolId::RfidSave || id == ToolId::NfcSave) {
        return make_info(vm, assets, "Save", "Read/Scan auto-saves UID dumps under " + dir);
    }
    return nullptr;
}

// --- Modules extras ----------------------------------------------------------------------

Screen make_modules_extra(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    if (id == ToolId::CapProbe) return make_cap_probe(vm, assets);
    if (id == ToolId::PahubScan) {
        return make_run(vm, assets, "PaHub Scan", [](Cancel&, const Log& log, const Status& status) {
            status("scanning");
            std::vector<platform::PahubChannelHit> hits;
            std::string err;
            if (!platform::pahub_scan(1, 0x70, hits, err)) {
                log(err);
                return;
            }
            for (const auto& h : hits) {
                if (h.addresses.empty()) {
                    log("ch" + std::to_string(h.channel) + ": (empty)");
                    continue;
                }
                std::string line = "ch" + std::to_string(h.channel) + ":";
                for (uint8_t a : h.addresses) {
                    char b[8];
                    std::snprintf(b, sizeof(b), " 0x%02X", a);
                    line += b;
                }
                log(line);
            }
        });
    }
    if (id == ToolId::IButtonRead) {
        return make_run(vm, assets, "iButton", [](Cancel&, const Log& log, const Status&) {
            std::string err;
            const auto devs = platform::list_w1_devices(err);
            if (devs.empty()) {
                log(err.empty() ? "no 1-Wire devices" : err);
                return;
            }
            for (const auto& d : devs) {
                std::string id;
                if (platform::read_w1_id_file(d.path, id, err)) {
                    log(d.id + " -> " + id);
                    const std::string path = ensure_subdir("ibutton") + "/" + d.id + ".txt";
                    platform::save_ibutton(path, id, err);
                }
            }
        });
    }
    if (id == ToolId::Pn532KillerUart) {
        return make_run(vm, assets, "PN532Killer", [](Cancel& cancel, const Log& log, const Status& status) {
            platform::UartSession uart;
            std::string err;
            const std::string dev = platform::UartSession::default_grove_uart();
            if (!uart.open(dev, 115200, err)) {
                log(err.empty() ? "UART open failed" : err);
                return;
            }
            status(dev);
            log("Listening 3s on " + dev);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline && !cancel) {
                std::string chunk;
                if (uart.read_available(chunk, err) && !chunk.empty()) log(chunk);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            uart.close();
            log("Done (status/read only)");
        });
    }
    if (id == ToolId::UartLlm) {
        if (st.name == "run") {
            return make_run(vm, assets, "UART LLM", [arg = st.arg](Cancel& cancel, const Log& log, const Status& status) {
                int baud = 115200;
                std::string msg;
                const auto sp = arg.find(' ');
                if (sp == std::string::npos) baud = std::atoi(arg.c_str());
                else {
                    baud = std::atoi(arg.substr(0, sp).c_str());
                    msg = trim(arg.substr(sp + 1));
                }
                if (baud <= 0) baud = 115200;
                platform::UartSession uart;
                std::string err;
                const std::string dev = platform::UartSession::default_grove_uart();
                if (!uart.open(dev, baud, err)) {
                    log(err);
                    return;
                }
                status(dev);
                if (!msg.empty()) {
                    const std::string data = msg + "\r\n";
                    uart.write_bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size(), err);
                    log("> " + msg);
                }
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                while (std::chrono::steady_clock::now() < deadline && !cancel) {
                    std::string chunk;
                    if (uart.read_available(chunk, err) && !chunk.empty()) log(chunk);
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
                uart.close();
            });
        }
        return make_prompt(vm, assets, "UART LLM", "baud [prompt]", "115200 hello",
                           [&vm](const std::string& v) { nav(vm, ToolId::UartLlm, "run", trim(v)); });
    }
    return nullptr;
}

} // namespace

std::unique_ptr<screen::KvxScreen> make_cap_tool_screen(viewmodel::BaseViewModel& vm, AssetManager& assets,
                                                        model::ToolId id, const std::string& param) {
    const Stage st = parse_stage(param);
    if (Screen s = make_rf_tool(vm, assets, id, st)) return s;
    if (Screen s = make_nrf_tool(vm, assets, id, st)) return s;
    if (Screen s = make_lora_tool(vm, assets, id, st)) return s;
    if (Screen s = make_fm_tool(vm, assets, id, st)) return s;
    if (Screen s = make_tag_tool(vm, assets, id, st)) return s;
    if (Screen s = make_modules_extra(vm, assets, id, st)) return s;
    return nullptr;
}

} // namespace app

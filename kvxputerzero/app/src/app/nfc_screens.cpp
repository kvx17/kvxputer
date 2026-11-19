#include "nfc_screens.h"

#include "cap_detect.h"
#include "doc_screen.h"
#include "info_screen.h"
#include "net_info.h"
#include "nfc_dump.h"
#include "nfc_module.h"
#include "nfc_ndef.h"
#include "nfc_session.h"
#include "options_list_screen.h"
#include "preset_links.h"
#include "pn532_bus.h"
#include "prompt_screen.h"
#include "run_screen.h"
#include "st25r3916.h"
#include "user_paths.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
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

Screen make_info(VM& vm, AssetManager& assets, const std::string& title, const std::string& text) {
    return std::make_unique<screen::DocScreen>(vm, assets, title, text);
}

Screen make_info_card(VM& vm, AssetManager& assets, const std::string& title,
                      screen::InfoScreen::WorkerFn worker) {
    return std::make_unique<screen::InfoScreen>(vm, assets, title, std::move(worker));
}

Screen make_run(VM& vm, AssetManager& assets, const std::string& title, Worker worker) {
    return std::make_unique<screen::RunScreen>(vm, assets, title, std::move(worker));
}

Screen make_prompt(VM& vm, AssetManager& assets, const std::string& title, const std::string& hint,
                   const std::string& initial, screen::PromptScreen::ConfirmFn fn) {
    return std::make_unique<screen::PromptScreen>(vm, assets, title, hint, initial, std::move(fn));
}

bool* cancel_ptr(Cancel& c) { return reinterpret_cast<bool*>(&c); }

Screen stub(VM& vm, AssetManager& assets, const std::string& title, const std::string& reason) {
    return make_info(vm, assets, title, reason);
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

std::string last_dump_path() { return (fs::path(tools::nfc_dir()) / "last_dump.nfc").string(); }

bool save_last_dump(const tools::NfcDump& d, std::string& error) {
    return tools::nfc_save_flipper(last_dump_path(), d, error);
}

bool load_last_dump(tools::NfcDump& d, std::string& error) {
    return tools::nfc_load_dump_file(last_dump_path(), d, error);
}

Screen post_read_menu(VM& vm, AssetManager& assets, const tools::NfcDump& d) {
    std::vector<MenuEntry> e;
    e.push_back({"View dump", [&vm, text = tools::nfc_dump_summary(d)]() {
                     vm.open_tool(ToolId::None, "info:" + text);
                     if (vm.current_page() == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
                 }});
    e.push_back({"Save", [&vm]() { nav(vm, ToolId::NfcSaveAsFile, "fromlast"); }});
    e.push_back({"Write Tag", [&vm]() { vm.open_tool(ToolId::NfcWriteNdef); }});
    e.push_back({"Emulate Tag", [&vm]() { vm.open_tool(ToolId::NfcEmulateNdef); }});
    e.push_back({"Erase tag", [&vm]() { vm.open_tool(ToolId::NfcErase); }});
    e.push_back({"Compare dump", [&vm]() { vm.open_tool(ToolId::NfcCompare); }});
    e.push_back({"Magic write", [&vm]() { vm.open_tool(ToolId::NfcMagic); }});
    e.push_back({"Detect Reader", [&vm]() { vm.open_tool(ToolId::NfcDetectReader); }});
    e.push_back({"Dict retry", [&vm]() { vm.open_tool(ToolId::NfcDictAttack); }});
    e.push_back({"Read again", [&vm]() { vm.open_tool(ToolId::NfcRead); }});
    return make_options(vm, assets, "Tag", std::move(e));
}

Screen make_status(VM& vm, AssetManager& assets) {
    return make_info_card(vm, assets, "NFC Status", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                       std::string& status, view::StatusPillKind& kind) {
        fields.push_back({"pref", tools::nfc_module_pref_label(tools::nfc_module_pref())});
        const auto st25 = platform::detect_st25r3916();
        if (st25.present) {
            fields.push_back({"ST25", st25.detail.empty() ? "present" : st25.detail});
            if (!st25.path.empty()) fields.push_back({"path", st25.path});
        } else {
            fields.push_back({"ST25", st25.error.empty() ? "not present" : st25.error});
        }
        const auto pn = platform::detect_pn532();
        if (pn.present) {
            platform::Pn532Session s;
            std::string err;
            if (s.open(-1, err)) {
                std::string fw;
                if (s.get_firmware(fw, err)) fields.push_back({"PN532", fw});
                else fields.push_back({"PN532", "present"});
                s.close();
            } else {
                fields.push_back({"PN532", err});
            }
        } else {
            fields.push_back({"PN532", pn.error.empty() ? "not present" : pn.error});
        }
        if (st25.present || pn.present) {
            status = "OK";
            kind = view::StatusPillKind::Ok;
        } else {
            status = "missing";
            kind = view::StatusPillKind::Missing;
            fields.push_back({"hint", "Cap-CC1101 NFC=ST25 SPI GPIO22; optional PN532 I2C 0x24"});
        }
    });
}

Screen make_read(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Read tag", [&vm](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status(std::string(s.chip_name()) + " present tag...");
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) {
            log(err.empty() ? "read failed" : err);
            s.close();
            return;
        }
        log(tools::nfc_dump_summary(d));
        std::string e2;
        if (save_last_dump(d, e2)) log("Cached last_dump.nfc");
        const std::string path =
            (fs::path(tools::nfc_dir()) / ("dump_" + d.uid_hex + ".nfc")).string();
        if (tools::nfc_save_flipper(path, d, e2)) log("Saved " + path);
        s.close();
        // Offer post-read via ToolPrompt rebuild
        vm.open_tool(ToolId::NfcRead, "actions");
        if (vm.current_page() == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
    });
}

Screen make_scan(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Scan tags", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        std::set<std::string> seen;
        status(std::string(s.chip_name()) + " scanning...");
        const auto start = std::chrono::steady_clock::now();
        while (!cancel && std::chrono::steady_clock::now() - start < std::chrono::minutes(5)) {
            tools::NfcDump d;
            if (s.read_dump(d, err, cancel_ptr(cancel))) {
                if (seen.insert(d.uid_hex).second) {
                    log(tools::nfc_uid_spaced(d.uid_hex) + "  " + d.device_type);
                }
            } else if (err == "cancelled") {
                break;
            }
            err.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        const auto dir = tools::nfc_subdir("scans");
        const std::string path =
            (fs::path(dir) / ("scan_result_" + std::to_string(std::time(nullptr)) + ".txt")).string();
        std::ofstream out(path);
        for (const auto& u : seen) out << u << "\n";
        log("Saved " + path + " (" + std::to_string(seen.size()) + " UIDs)");
        s.close();
    });
}

Screen make_load(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "view" && !st.arg.empty()) {
        tools::NfcDump d;
        std::string err;
        if (!tools::nfc_load_dump_file(st.arg, d, err)) return make_info(vm, assets, "Load", err);
        (void)save_last_dump(d, err);
        return make_info(vm, assets, "Load", tools::nfc_dump_summary(d));
    }
    std::vector<MenuEntry> e;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(tools::nfc_dir(), ec)) {
        if (!de.is_regular_file()) continue;
        const auto ext = de.path().extension().string();
        if (ext != ".nfc" && ext != ".rfid" && ext != ".txt") continue;
        const std::string n = de.path().filename().string();
        const std::string path = de.path().string();
        e.push_back({n, [&vm, path]() { nav(vm, ToolId::NfcLoad, "view", path); }});
    }
    if (e.empty()) e.push_back({"(no dumps)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Load file", std::move(e));
}

Screen make_erase(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Erase data", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status("hold tag...");
        if (!s.erase(err, cancel_ptr(cancel))) log(err);
        else log("Erased user pages");
        s.close();
    });
}

Screen submenu(VM& vm, AssetManager& assets, const char* title, std::initializer_list<ToolId> ids) {
    std::vector<MenuEntry> e;
    for (ToolId id : ids) {
        if (const auto* d = model::find_tool(id)) {
            e.push_back({d->label, [&vm, id]() { vm.open_tool(id); }});
        }
    }
    return make_options(vm, assets, title, std::move(e));
}

Screen make_memory_dump(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Memory dump", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status("reading...");
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) log(err);
        else {
            log(tools::nfc_dump_summary(d));
            (void)save_last_dump(d, err);
        }
        s.close();
    });
}

Screen make_chip_info(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Chip info", [](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) log(err);
        else {
            log("Type: " + d.device_type);
            log("UID: " + tools::nfc_uid_spaced(d.uid_hex));
            log("ATQA: " + d.atqa_hex);
            log("SAK: " + d.sak_hex);
            log("Pages: " + std::to_string(d.pages_read) + "/" + std::to_string(d.pages_total));
            (void)save_last_dump(d, err);
        }
        s.close();
    });
}

Screen make_write_ndef(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "run") {
        const auto tlv = tools::ndef_tlv_uri(st.arg.empty() ? "https://kvxputer.local" : st.arg);
        return make_run(vm, assets, "Write Tag", [tlv](Cancel& cancel, const Log& log, const Status& status) {
            platform::NfcSession s;
            std::string err;
            if (!s.open(err)) {
                log(err);
                return;
            }
            status("hold Ultralight/NTAG...");
            if (!s.write_ndef_tlv(tlv, err, cancel_ptr(cancel))) log(err);
            else log("NDEF written");
            s.close();
        });
    }
    std::vector<MenuEntry> e;
    e.push_back({"URI / HTTPS", [&vm]() { nav(vm, ToolId::NfcWriteNdef, "url"); }});
    e.push_back({"Text", [&vm]() { nav(vm, ToolId::NfcWriteNdef, "text"); }});
    e.push_back({"Android App", [&vm]() { nav(vm, ToolId::NfcWriteNdef, "aar"); }});
    e.push_back({"Wi-Fi", [&vm]() { nav(vm, ToolId::NfcWriteNdef, "wifi"); }});
    if (st.name == "url") {
        return make_prompt(vm, assets, "NDEF URI", "URL", "https://",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcWriteNdef, "run", trim(v)); });
    }
    if (st.name == "text") {
        return make_prompt(vm, assets, "NDEF Text", "text", "hello", [&vm](const std::string& v) {
            const auto tlv = tools::ndef_tlv_text(trim(v));
            // stash as hex in run via writing temp — reuse run with uri builder workaround
            std::string hex;
            for (uint8_t b : tlv) {
                char c[4];
                std::snprintf(c, sizeof(c), "%02X", b);
                hex += c;
            }
            nav(vm, ToolId::NfcWriteNdef, "raw", hex);
        });
    }
    if (st.name == "aar") {
        return make_prompt(vm, assets, "Android App", "package", "com.example.app",
                           [&vm](const std::string& v) {
                               const auto tlv = tools::ndef_tlv_android_app(trim(v));
                               std::string hex;
                               for (uint8_t b : tlv) {
                                   char c[4];
                                   std::snprintf(c, sizeof(c), "%02X", b);
                                   hex += c;
                               }
                               nav(vm, ToolId::NfcWriteNdef, "raw", hex);
                           });
    }
    if (st.name == "wifi") {
        return make_prompt(vm, assets, "Wi-Fi NDEF", "ssid:password", "MySSID:secret",
                           [&vm](const std::string& v) {
                               const auto c = v.find(':');
                               const std::string ssid = c == std::string::npos ? v : v.substr(0, c);
                               const std::string pw = c == std::string::npos ? "" : v.substr(c + 1);
                               const auto tlv = tools::ndef_tlv_wifi(ssid, pw);
                               std::string hex;
                               for (uint8_t b : tlv) {
                                   char cbuf[4];
                                   std::snprintf(cbuf, sizeof(cbuf), "%02X", b);
                                   hex += cbuf;
                               }
                               nav(vm, ToolId::NfcWriteNdef, "raw", hex);
                           });
    }
    if (st.name == "raw") {
        const auto tlv = parse_hex_bytes(st.arg);
        return make_run(vm, assets, "Write Tag", [tlv](Cancel& cancel, const Log& log, const Status& status) {
            platform::NfcSession s;
            std::string err;
            if (!s.open(err)) {
                log(err);
                return;
            }
            status("hold Ultralight/NTAG...");
            if (!s.write_ndef_tlv(tlv, err, cancel_ptr(cancel))) log(err);
            else log("NDEF written");
            s.close();
        });
    }
    return make_options(vm, assets, "Write Tag", std::move(e));
}

std::string zero_http_url() {
    std::string err;
    const auto iface = platform::default_route_iface(err);
    if (!iface.ipv4.empty()) return "http://" + iface.ipv4 + "/";
    const auto ifaces = platform::list_net_ifaces(err);
    for (const auto& i : ifaces) {
        if (i.up && !i.ipv4.empty() && i.name != "lo") return "http://" + i.ipv4 + "/";
    }
    return "http://kvxputerzero.local/";
}

Screen make_emulate_run(VM& vm, AssetManager& assets, const std::string& url) {
    const auto tlv = tools::ndef_tlv_uri(url.empty() ? tools::links::kRepoUrl : url);
    return make_run(vm, assets, "Emulate Tag", [tlv, url](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        const bool cap = std::string(s.chip_name()) == "ST25R3916";
        log(std::string("Emulate on ") + s.chip_name() + (cap ? " (NTAG/T2T)" : " (T4T)"));
        log(url);
        if (cap) {
            log("Cap Passive Target: no self-field (docs NFC-A emulate).");
            log("Flipper: NFC → Read (Detect Reader not needed).");
            log("Hold Flipper flat on Cap PCB NFC area (not SMA).");
            status("Listening for reader field...");
        } else {
            status("Hold phone / Flipper on antenna...");
        }
        // 60s Passive Target window; Esc cancels.
        if (!s.emulate_ndef_tlv(tlv, err, cancel_ptr(cancel), 60000)) log(err);
        else log("Reader contacted / listen ended");
        s.close();
    });
}

Screen make_emulate_ndef(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "run") {
        return make_emulate_run(vm, assets, st.arg.empty() ? zero_http_url() : st.arg);
    }
    if (st.name == "custom") {
        return make_prompt(vm, assets, "Emulate Tag", "URL to emulate", "https://",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcEmulateNdef, "run", trim(v)); });
    }
    if (st.name == "links") {
        std::vector<MenuEntry> e;
        for (size_t i = 0; i < tools::links::kUsefulCount; ++i) {
            const char* url = tools::links::kUseful[i];
            e.push_back({tools::links::short_label(url),
                         [&vm, u = std::string(url)]() { nav(vm, ToolId::NfcEmulateNdef, "run", u); }});
        }
        return make_options(vm, assets, "Useful Links", std::move(e));
    }

    // Default: preset picker.
    const std::string ip_url = zero_http_url();
    std::vector<MenuEntry> e;
    e.push_back({"Zero IP  " + ip_url, [&vm, ip_url]() { nav(vm, ToolId::NfcEmulateNdef, "run", ip_url); }});
    e.push_back({tools::links::kRepoLabel,
                 [&vm]() { nav(vm, ToolId::NfcEmulateNdef, "run", tools::links::kRepoUrl); }});
    e.push_back({tools::links::kGithubLabel,
                 [&vm]() { nav(vm, ToolId::NfcEmulateNdef, "run", tools::links::kGithubUrl); }});
    e.push_back({"Useful Links...", [&vm]() { nav(vm, ToolId::NfcEmulateNdef, "links"); }});
    e.push_back({"Custom URL...", [&vm]() { nav(vm, ToolId::NfcEmulateNdef, "custom"); }});
    return make_options(vm, assets, "Emulate Tag", std::move(e));
}

Screen make_read_records(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Read records", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status("reading...");
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) {
            log(err);
            s.close();
            return;
        }
        (void)save_last_dump(d, err);
        const auto recs = tools::ndef_parse_from_pages(d.pages_text);
        if (recs.empty()) log("No NDEF records found.");
        for (const auto& r : recs) {
            log(r.type_label + ": " + r.summary);
            if (!r.qr_payload.empty()) log("  QR: " + r.qr_payload);
        }
        s.close();
    });
}

Screen make_save_as_file(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "fromlast") {
        tools::NfcDump d;
        std::string err;
        if (!load_last_dump(d, err)) return make_info(vm, assets, "Save", err);
        return make_prompt(vm, assets, "Save as", "filename (no ext)", "tag",
                           [d](const std::string& name) {
                               std::string err2;
                               const auto base = (fs::path(tools::nfc_dir()) / trim(name)).string();
                               (void)tools::nfc_save_bruce(base + ".rfid", d, err2);
                               (void)tools::nfc_save_flipper(base + ".nfc", d, err2);
                           });
    }
    if (st.name == "gen") {
        const int pages = std::atoi(st.arg.c_str());
        return make_prompt(vm, assets, "Save as file", "UID hex + name  uid,name", "04010203,ntag",
                           [pages](const std::string& v) {
                               auto c = v.find(',');
                               std::string uid = c == std::string::npos ? v : v.substr(0, c);
                               std::string name = c == std::string::npos ? "ntag" : v.substr(c + 1);
                               auto d = tools::nfc_synthetic_ntag(pages <= 0 ? 45 : pages, uid,
                                                                  tools::ndef_tlv_uri("https://kvxputer.local"));
                               std::string err;
                               const auto base = (fs::path(tools::nfc_dir()) / trim(name)).string();
                               (void)tools::nfc_save_bruce(base + ".rfid", d, err);
                               (void)tools::nfc_save_flipper(base + ".nfc", d, err);
                           });
    }
    std::vector<MenuEntry> e;
    e.push_back({"NTAG213 (144B)", [&vm]() { nav(vm, ToolId::NfcSaveAsFile, "gen", "45"); }});
    e.push_back({"NTAG215 (496B)", [&vm]() { nav(vm, ToolId::NfcSaveAsFile, "gen", "135"); }});
    e.push_back({"NTAG216 (872B)", [&vm]() { nav(vm, ToolId::NfcSaveAsFile, "gen", "231"); }});
    e.push_back({"From last read", [&vm]() { nav(vm, ToolId::NfcSaveAsFile, "fromlast"); }});
    return make_options(vm, assets, "Save as file", std::move(e));
}

Screen make_lock(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "go") {
        std::vector<MenuEntry> e;
        e.push_back({"Cancel", [&vm]() { vm.go_back(); }});
        e.push_back({"LOCK FOREVER", [&vm]() { nav(vm, ToolId::NfcLockTag, "go"); }});
        return make_options(vm, assets, "IRREV Lock", std::move(e));
    }
    return make_run(vm, assets, "Lock tag", [](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        if (!s.lock_ntag(err, cancel_ptr(cancel))) log(err);
        else log("Lock bytes written");
        s.close();
    });
}

Screen make_password(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name.empty() || st.name == "menu") {
        std::vector<MenuEntry> e;
        e.push_back({"Set password", [&vm]() { nav(vm, ToolId::NfcPassword, "set"); }});
        e.push_back({"Clear password", [&vm]() { nav(vm, ToolId::NfcPassword, "clear"); }});
        return make_options(vm, assets, "NTAG Pass", std::move(e));
    }
    if (st.name == "set") {
        return make_prompt(vm, assets, "Set password", "8 hex PWD + 4 hex PACK", "FFFFFFFF0000",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcPassword, "do", trim(v)); });
    }
    if (st.name == "clear") {
        return make_run(vm, assets, "Clear password", [](Cancel& cancel, const Log& log, const Status&) {
            platform::NfcSession s;
            std::string err;
            if (!s.open(err)) {
                log(err);
                return;
            }
            if (!s.clear_ntag_password(err, cancel_ptr(cancel))) log(err);
            else log("Password cleared");
            s.close();
        });
    }
    const auto bytes = parse_hex_bytes(st.arg);
    return make_run(vm, assets, "Set password", [bytes](Cancel& cancel, const Log& log, const Status&) {
        if (bytes.size() < 6) {
            log("Need 6+ hex bytes (PWD4+PACK2)");
            return;
        }
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        uint8_t pwd[4] = {bytes[0], bytes[1], bytes[2], bytes[3]};
        uint8_t pack[2] = {bytes[4], bytes[5]};
        if (!s.set_ntag_password(pwd, pack, err, cancel_ptr(cancel))) log(err);
        else log("Password set");
        s.close();
    });
}

Screen make_format(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "go") {
        std::vector<MenuEntry> e;
        e.push_back({"Cancel", [&vm]() { vm.go_back(); }});
        e.push_back({"Format now", [&vm]() { nav(vm, ToolId::NfcFormat, "go"); }});
        return make_options(vm, assets, "Zero Mem", std::move(e));
    }
    return make_erase(vm, assets);
}

Screen make_apdu(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "run") {
        return make_prompt(vm, assets, "NFC command", "APDU hex", "00A4040000",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcApdu, "run", trim(v)); });
    }
    const auto tx = parse_hex_bytes(st.arg);
    return make_run(vm, assets, "NFC command", [tx](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status("hold ISO-DEP...");
        std::vector<uint8_t> rx;
        if (!s.transceive_apdu(tx, rx, err, cancel_ptr(cancel))) log(err);
        else {
            std::string hex;
            for (uint8_t b : rx) {
                char c[4];
                std::snprintf(c, sizeof(c), "%02X ", b);
                hex += c;
            }
            log(hex.empty() ? "(empty)" : hex);
        }
        s.close();
    });
}

Screen make_apdu_script(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "run") {
        return make_prompt(vm, assets, "APDU script", "file under nfc/apdu/ or hex;hex;...", "script.txt",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcApduScript, "run", trim(v)); });
    }
    return make_run(vm, assets, "APDU script", [arg = st.arg](Cancel& cancel, const Log& log, const Status&) {
        std::vector<std::string> lines;
        const auto path = fs::path(tools::nfc_subdir("apdu")) / arg;
        std::ifstream in(path);
        if (in) {
            std::string line;
            while (std::getline(in, line) && lines.size() < 8) {
                line = trim(line);
                if (!line.empty() && line[0] != '#') lines.push_back(line);
            }
        } else {
            std::string cur;
            for (char c : arg) {
                if (c == ';') {
                    if (!cur.empty()) lines.push_back(cur);
                    cur.clear();
                } else
                    cur.push_back(c);
            }
            if (!cur.empty()) lines.push_back(cur);
        }
        if (lines.empty()) {
            log("No APDU lines");
            return;
        }
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        for (const auto& line : lines) {
            if (cancel) break;
            auto tx = parse_hex_bytes(line);
            std::vector<uint8_t> rx;
            log("> " + line);
            if (!s.transceive_apdu(tx, rx, err, cancel_ptr(cancel))) {
                log("ERR " + err);
                break;
            }
            std::string hex;
            for (uint8_t b : rx) {
                char c[4];
                std::snprintf(c, sizeof(c), "%02X", b);
                hex += c;
            }
            log("< " + hex);
            if (rx.size() >= 2) {
                const uint8_t sw1 = rx[rx.size() - 2], sw2 = rx[rx.size() - 1];
                if (!(sw1 == 0x90 && sw2 == 0x00) && sw1 != 0x61) {
                    log("Stop on bad SW");
                    break;
                }
            }
        }
        s.close();
    });
}

Screen make_compare(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "run") {
        std::vector<MenuEntry> e;
        std::error_code ec;
        for (const auto& de : fs::directory_iterator(tools::nfc_dir(), ec)) {
            if (!de.is_regular_file()) continue;
            const auto ext = de.path().extension().string();
            if (ext != ".nfc" && ext != ".rfid") continue;
            const std::string path = de.path().string();
            e.push_back({de.path().filename().string(),
                         [&vm, path]() { nav(vm, ToolId::NfcCompare, "run", path); }});
        }
        if (e.empty()) e.push_back({"(no dumps)", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, "Compare dump", std::move(e));
    }
    return make_run(vm, assets, "Compare dump", [path = st.arg](Cancel& cancel, const Log& log, const Status&) {
        tools::NfcDump file;
        std::string err;
        if (!tools::nfc_load_dump_file(path, file, err)) {
            log(err);
            return;
        }
        platform::NfcSession s;
        if (!s.open(err)) {
            log(err);
            return;
        }
        tools::NfcDump live;
        if (!s.read_dump(live, err, cancel_ptr(cancel))) {
            log(err);
            s.close();
            return;
        }
        log(std::string("UID ") + (file.uid_hex == live.uid_hex ? "OK" : "DIFF"));
        log(std::string("SAK ") + (file.sak_hex == live.sak_hex ? "OK" : "DIFF"));
        log(std::string("ATQA ") + (file.atqa_hex == live.atqa_hex ? "OK" : "DIFF"));
        log(std::string("Pages ") + (file.pages_text == live.pages_text ? "OK" : "DIFF"));
        s.close();
    });
}

Screen make_field_poll(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Field poll", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        while (!cancel) {
            bool present = false;
            std::string uid;
            if (!s.field_poll(present, uid, err, cancel_ptr(cancel))) {
                if (err == "cancelled") break;
                log(err);
                break;
            }
            if (present) {
                status("TAG PRESENT");
                log(tools::nfc_uid_spaced(uid));
            } else {
                status("no tag");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
        s.close();
    });
}

Screen make_dict(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Dict attack", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        status("Classic read (known keys)...");
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) log(err);
        else {
            log(tools::nfc_dump_summary(d));
            (void)save_last_dump(d, err);
        }
        s.close();
    });
}

Screen make_magic(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name.empty() || st.name == "menu") {
        std::vector<MenuEntry> e;
        e.push_back({"Probe magic type", [&vm]() { nav(vm, ToolId::NfcMagic, "probe"); }});
        e.push_back({"Write UID", [&vm]() { nav(vm, ToolId::NfcMagic, "uid"); }});
        e.push_back({"Write full dump", [&vm]() { nav(vm, ToolId::NfcMagic, "dump"); }});
        return make_options(vm, assets, "NFC Magic", std::move(e));
    }
    if (st.name == "probe") {
        return make_run(vm, assets, "Magic probe", [](Cancel& cancel, const Log& log, const Status&) {
            platform::NfcSession s;
            std::string err;
            if (!s.open(err)) {
                log(err);
                return;
            }
            std::string result;
            if (!s.magic_probe(result, err, cancel_ptr(cancel))) log(err);
            else log(result);
            s.close();
        });
    }
    if (st.name == "uid" || st.name == "dump") {
        return make_prompt(vm, assets, "Magic write", "UID hex", "AABBCCDD",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcMagic, "write", trim(v)); });
    }
    return make_run(vm, assets, "Magic write", [uid = st.arg](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        if (!s.magic_clone_uid(uid, err, cancel_ptr(cancel))) log(err);
        else log("UID write attempted");
        s.close();
    });
}

Screen make_detect_reader(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Detect Reader", [](Cancel& cancel, const Log& log, const Status& status) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        tools::NfcDump d;
        (void)s.read_dump(d, err, cancel_ptr(cancel));
        const auto dir = tools::nfc_subdir("nonces");
        const std::string path =
            (fs::path(dir) / ("nonce_" + std::to_string(std::time(nullptr)) + ".bin")).string();
        std::ofstream out(path);
        out << "# kvxputerzero nonce harvest placeholder\n";
        out << "# Cap ST25 has no dedicated IRQ for mfkey32; ADV harvest is TBD.\n";
        out << "UID: " << d.uid_hex << "\n";
        status("placeholder...");
        log("Wrote " + path);
        log("Not a working Crypto1 harvest (matches ADV stub).");
        s.close();
    });
}

Screen make_mfkey(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "MFKey", [](Cancel&, const Log& log, const Status&) {
        const auto dir = tools::nfc_subdir("nonces");
        std::error_code ec;
        int found = 0;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            if (!de.is_regular_file()) continue;
            std::ifstream in(de.path());
            std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string hex;
            for (char c : all) {
                if (std::isxdigit(static_cast<unsigned char>(c))) hex.push_back(c);
            }
            for (size_t i = 0; i + 12 <= hex.size(); ++i) {
                // scan 12 hex runs
                bool ok = true;
                for (size_t j = 0; j < 12; ++j) {
                    if (!std::isxdigit(static_cast<unsigned char>(hex[i + j]))) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    log("Candidate " + hex.substr(i, 12));
                    ++found;
                    i += 11;
                }
            }
        }
        if (found == 0)
            log("Nonce format needs a full MFKey32 pass (future). No 12-hex runs found.");
        else
            log("Scanned files; full LFSR not implemented (matches ADV).");
    });
}

Screen make_dump_editor(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "edit") {
        std::vector<MenuEntry> e;
        std::error_code ec;
        for (const auto& de : fs::directory_iterator(tools::nfc_dir(), ec)) {
            if (!de.is_regular_file()) continue;
            const auto ext = de.path().extension().string();
            if (ext != ".nfc" && ext != ".rfid") continue;
            const std::string path = de.path().string();
            e.push_back({de.path().filename().string(),
                         [&vm, path]() { nav(vm, ToolId::NfcDumpEditor, "edit", path); }});
        }
        if (e.empty()) e.push_back({"(no dumps)", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, "Dump editor", std::move(e));
    }
    return make_prompt(vm, assets, "Dump editor", "append line", "# note", [path = st.arg](const std::string& line) {
        tools::NfcDump d;
        std::string err;
        if (!tools::nfc_load_dump_file(path, d, err)) return;
        d.pages_text += "\n# edited\n" + line + "\n";
        const auto out = (fs::path(tools::nfc_dir()) / "edited.rfid").string();
        (void)tools::nfc_save_bruce(out, d, err);
    });
}

Screen make_iso15693(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name.empty() || st.name == "menu") {
        std::vector<MenuEntry> e;
        e.push_back({"Inventory", [&vm]() { nav(vm, ToolId::NfcIso15693, "inv"); }});
        e.push_back({"Read block", [&vm]() { nav(vm, ToolId::NfcIso15693, "rblock"); }});
        e.push_back({"Write block", [&vm]() { nav(vm, ToolId::NfcIso15693, "wblock"); }});
        return make_options(vm, assets, "ISO15693", std::move(e));
    }
    if (st.name == "rblock" || st.name == "wblock") {
        return stub(vm, assets, "ISO15693",
                    "ISO15693 read/write block not on this module yet (matches ADV).");
    }
    return make_run(vm, assets, "ISO15693 inventory", [](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        std::string uid;
        if (!s.iso15693_inventory(uid, err, cancel_ptr(cancel))) log(err);
        else log(tools::nfc_uid_spaced(uid));
        s.close();
    });
}

Screen make_desfire(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "DESFire browser", [](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        tools::NfcDump d;
        if (!s.read_dump(d, err, cancel_ptr(cancel))) log(err);
        else {
            if (d.device_type.find("DESFire") != std::string::npos ||
                d.device_type.find("ISO14443-4") != std::string::npos || !d.ndef_text.empty()) {
                log(d.device_type);
                if (!d.ndef_text.empty()) log(d.ndef_text);
                log("(No app/file browser — matches ADV thin view)");
            } else {
                log("DESFire list not on module / tag not DESFire.");
                log(tools::nfc_dump_summary(d));
            }
        }
        s.close();
    });
}

Screen make_ulc(VM& vm, AssetManager& assets) {
    return stub(vm, assets, "Ultralight-C",
                "UL-C auth not on this module (matches ADV). Keys file: nfc/ulc_keys.txt");
}

Screen make_felica(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "FeliCa services", [](Cancel& cancel, const Log& log, const Status&) {
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        tools::NfcDump d;
        if (!s.read_felica(d, err, cancel_ptr(cancel))) log(err);
        else log(tools::nfc_dump_summary(d));
        if (!err.empty() && err.find("RFAL") != std::string::npos) log(err);
        s.close();
    });
}

Screen make_forced(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name.empty() || st.name == "menu") {
        std::vector<MenuEntry> e;
        e.push_back({"Auto", [&vm]() { nav(vm, ToolId::NfcForcedType, "auto"); }});
        e.push_back({"MIFARE Classic", [&vm]() { nav(vm, ToolId::NfcForcedType, "classic"); }});
        e.push_back({"UL / NTAG", [&vm]() { nav(vm, ToolId::NfcForcedType, "ul"); }});
        e.push_back({"ISO15693", [&vm]() { nav(vm, ToolId::NfcForcedType, "v"); }});
        e.push_back({"FeliCa", [&vm]() { nav(vm, ToolId::NfcForcedType, "f"); }});
        e.push_back({"ISO-DEP", [&vm]() { nav(vm, ToolId::NfcForcedType, "dep"); }});
        return make_options(vm, assets, "Force type", std::move(e));
    }
    if (st.name == "v") {
        return stub(vm, assets, "Forced ISO15693",
                    "Forced ISO15693 needs RFAL NFC-V on Cap ST25; PN532 cannot speak NFC-V.");
    }
    if (st.name == "f") return make_felica(vm, assets);
    return make_read(vm, assets);
}

Screen make_amiibo(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name.empty() || st.name == "menu") {
        std::vector<MenuEntry> e;
        e.push_back({"Load & view dump", [&vm]() { vm.open_tool(ToolId::NfcLoad); }});
        e.push_back({"Write dump", [&vm]() { nav(vm, ToolId::NfcAmiibo, "write"); }});
        e.push_back({"Emulate dump", [&vm]() { nav(vm, ToolId::NfcAmiibo, "emu"); }});
        return make_options(vm, assets, "Amiibo", std::move(e));
    }
    if (st.name == "write") {
        return make_info(vm, assets, "Amiibo write",
                         "Load a dump first, then use Write NDEF / page write path. "
                         "Keygen: supply keys on SD (not implemented — matches ADV).");
    }
    return make_emulate_ndef(vm, assets, {});
}

Screen make_emv(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "Read EMV", [](Cancel& cancel, const Log& log, const Status& status) {
        if (tools::nfc_module_pref() == tools::NfcModulePref::St25) {
            log("EMV needs PN532 on I2C (matches ADV). Set Config → NFC Module → PN532 I2C.");
            return;
        }
        platform::NfcSession s;
        std::string err;
        if (!s.open(err)) {
            log(err);
            return;
        }
        if (s.chip() != platform::NfcChip::Pn532) {
            log("EMV needs PN532\non Grove / I2C");
            s.close();
            return;
        }
        status("hold payment card...");
        // SELECT PPSE
        const auto tx = parse_hex_bytes("00A404000E325041592E5359532E444446303100");
        std::vector<uint8_t> rx;
        if (!s.transceive_apdu(tx, rx, err, cancel_ptr(cancel))) {
            log(err);
            s.close();
            return;
        }
        std::string hex;
        for (uint8_t b : rx) {
            char c[4];
            std::snprintf(c, sizeof(c), "%02X", b);
            hex += c;
        }
        log(hex);
        const auto path =
            (fs::path(tools::nfc_subdir("scans")) / ("emv_" + std::to_string(std::time(nullptr)) + ".txt"))
                .string();
        std::ofstream out(path);
        out << hex << "\n";
        log("Saved " + path);
        s.close();
    });
}

Screen make_srix(VM& vm, AssetManager& assets) {
    if (tools::nfc_module_pref() == tools::NfcModulePref::St25) {
        return stub(vm, assets, "SRIX Tool", "SRIX needs PN532 I2C (matches ADV). Switch module in Config.");
    }
    return stub(vm, assets, "SRIX Tool",
                "SRIX4K protocol port not finished on Zero yet. Use PN532 I2C when implemented; "
                "dumps would go under nfc/srix/.");
}

Screen make_config_module(VM& vm, AssetManager& assets, const Stage&) {
    std::vector<MenuEntry> e;
    e.push_back({"Auto (ST25 then PN532)", [&vm]() {
                     tools::nfc_set_module_pref(tools::NfcModulePref::Auto);
                     vm.go_back();
                 }});
    e.push_back({"ST25 SPI (GPIO22)", [&vm]() {
                     tools::nfc_set_module_pref(tools::NfcModulePref::St25);
                     vm.go_back();
                 }});
    e.push_back({"PN532 I2C 0x24", [&vm]() {
                     tools::nfc_set_module_pref(tools::NfcModulePref::Pn532);
                     vm.go_back();
                 }});
    return make_options(vm, assets, "NFC Module", std::move(e));
}

Screen make_wiring(VM& vm, AssetManager& assets) {
    return make_info(
        vm, assets, "NFC wiring",
        "Cap-CC1101 NFC (ST25R3916) — docs.m5stack.com/en/cap/Cap_CC1101:\n"
        "- Shared SPI /dev/spidev0.1 (CE1), SPI mode 1, 5 MHz\n"
        "- NFC_CS = GPIO22 on Zero (Adv silk G6)\n"
        "- NFC_IRQ = GPIO23 on Zero (Adv silk G4); polled via regs\n"
        "- Card Emulation: NFC-A Passive Target (no TX field)\n"
        "- Antenna: Cap PCB NFC coil — not the RP-SMA rubber antenna\n"
        "- Power: ext_5v_out + GPIO26 POWER_EN\n"
        "- CC1101 RF_SW0 GPIO14 / GDO0 GPIO15\n"
        "\n"
        "Optional PN532:\n"
        "- I2C bus from i2c_pn532.txt (default 1), address 0x24\n"
        "- No PN532 SPI on Zero\n"
        "\n"
        "Do not hot-plug Caps.");
}

Screen make_add_key(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name != "save") {
        return make_prompt(vm, assets, "Add MIF Key", "12 hex chars", "FFFFFFFFFFFF",
                           [&vm](const std::string& v) { nav(vm, ToolId::NfcAddKey, "save", trim(v)); });
    }
    const std::string hex = tools::nfc_normalize_hex(st.arg);
    if (hex.size() != 12) return make_info(vm, assets, "Add MIF Key", "Need exactly 12 hex chars");
    const auto path = (fs::path(tools::nfc_subdir("keys")) / "mifare.keys").string();
    std::ofstream out(path, std::ios::app);
    out << hex << "\n";
    return make_info(vm, assets, "Add MIF Key", "Appended to " + path);
}

} // namespace

std::unique_ptr<screen::KvxScreen> make_nfc_tool_screen(viewmodel::BaseViewModel& vm, AssetManager& assets,
                                                        model::ToolId id, const std::string& param) {
    const Stage st = parse_stage(param);

    switch (id) {
        case ToolId::NfcStatus: return make_status(vm, assets);
        case ToolId::NfcRead:
            if (st.name == "actions") {
                tools::NfcDump d;
                std::string err;
                if (load_last_dump(d, err)) return post_read_menu(vm, assets, d);
                return make_info(vm, assets, "Tag", err.empty() ? "No last dump" : err);
            }
            return make_read(vm, assets);
        case ToolId::NfcScan: return make_scan(vm, assets);
        case ToolId::NfcLoad: return make_load(vm, assets, st);
        case ToolId::NfcSave:
            return make_info(vm, assets, "Save",
                             "Read/Scan auto-save under ~/.config/kvxputerzero/nfc/. "
                             "Use NDEF tools → Save as file for named .nfc/.rfid.");
        case ToolId::NfcErase: return make_erase(vm, assets);

        case ToolId::NfcClassicMenu:
            return submenu(vm, assets, "Classic",
                           {ToolId::NfcDetectReader, ToolId::NfcMfkey, ToolId::NfcDictAttack,
                            ToolId::NfcMagic, ToolId::NfcDumpEditor});
        case ToolId::NfcTagsMenu:
            return submenu(vm, assets, "Tags",
                           {ToolId::NfcIso15693, ToolId::NfcDesfire, ToolId::NfcUlc, ToolId::NfcFelica,
                            ToolId::NfcForcedType});
        case ToolId::NfcNdefMenu:
            return submenu(vm, assets, "NDEF tools",
                           {ToolId::NfcWriteNdef, ToolId::NfcEmulateNdef, ToolId::NfcReadRecords,
                            ToolId::NfcSaveAsFile, ToolId::NfcMemoryDump, ToolId::NfcChipInfo,
                            ToolId::NfcLockTag, ToolId::NfcPassword, ToolId::NfcFormat, ToolId::NfcApdu});
        case ToolId::NfcLabMenu:
            return submenu(vm, assets, "Lab",
                           {ToolId::NfcApduScript, ToolId::NfcCompare, ToolId::NfcFieldPoll});
        case ToolId::NfcParsersMenu:
            return submenu(vm, assets, "Parsers",
                           {ToolId::NfcAmiibo, ToolId::NfcTransit, ToolId::NfcEpassport, ToolId::NfcPicopass,
                            ToolId::NfcEink});
        case ToolId::NfcCompanionsMenu:
            return submenu(vm, assets, "Companions",
                           {ToolId::NfcEmv, ToolId::NfcSrix, ToolId::NfcAmiibolink, ToolId::NfcPn532Ble,
                            ToolId::NfcPn532Uart, ToolId::NfcChameleon});
        case ToolId::NfcConfigMenu:
            return submenu(vm, assets, "NFC Config",
                           {ToolId::NfcModule, ToolId::NfcWiring, ToolId::NfcAddKey});

        case ToolId::NfcDetectReader: return make_detect_reader(vm, assets);
        case ToolId::NfcMfkey: return make_mfkey(vm, assets);
        case ToolId::NfcDictAttack: return make_dict(vm, assets);
        case ToolId::NfcMagic: return make_magic(vm, assets, st);
        case ToolId::NfcDumpEditor: return make_dump_editor(vm, assets, st);

        case ToolId::NfcIso15693: return make_iso15693(vm, assets, st);
        case ToolId::NfcDesfire: return make_desfire(vm, assets);
        case ToolId::NfcUlc: return make_ulc(vm, assets);
        case ToolId::NfcFelica: return make_felica(vm, assets);
        case ToolId::NfcForcedType: return make_forced(vm, assets, st);

        case ToolId::NfcWriteNdef: return make_write_ndef(vm, assets, st);
        case ToolId::NfcEmulateNdef: return make_emulate_ndef(vm, assets, st);
        case ToolId::NfcReadRecords: return make_read_records(vm, assets);
        case ToolId::NfcSaveAsFile: return make_save_as_file(vm, assets, st);
        case ToolId::NfcMemoryDump: return make_memory_dump(vm, assets);
        case ToolId::NfcChipInfo: return make_chip_info(vm, assets);
        case ToolId::NfcLockTag: return make_lock(vm, assets, st);
        case ToolId::NfcPassword: return make_password(vm, assets, st);
        case ToolId::NfcFormat: return make_format(vm, assets, st);
        case ToolId::NfcApdu: return make_apdu(vm, assets, st);

        case ToolId::NfcApduScript: return make_apdu_script(vm, assets, st);
        case ToolId::NfcCompare: return make_compare(vm, assets, st);
        case ToolId::NfcFieldPoll: return make_field_poll(vm, assets);

        case ToolId::NfcAmiibo: return make_amiibo(vm, assets, st);
        case ToolId::NfcTransit:
            return make_run(vm, assets, "Transit parse", [](Cancel& cancel, const Log& log, const Status&) {
                platform::NfcSession s;
                std::string err;
                if (!s.open(err)) {
                    log(err);
                    return;
                }
                tools::NfcDump d;
                if (s.read_dump(d, err, cancel_ptr(cancel))) {
                    log("No matching transit parser.");
                    log(tools::nfc_dump_summary(d).substr(0, 800));
                } else
                    log(err);
                s.close();
            });
        case ToolId::NfcEpassport:
            return make_run(vm, assets, "ePassport", [](Cancel& cancel, const Log& log, const Status&) {
                platform::NfcSession s;
                std::string err;
                if (!s.open(err)) {
                    log(err);
                    return;
                }
                const auto tx = parse_hex_bytes("00A4020C02011E");
                std::vector<uint8_t> rx;
                if (!s.transceive_apdu(tx, rx, err, cancel_ptr(cancel))) log(err);
                else {
                    log("BAC: not fully implemented (needs SM session).");
                    if (rx.size() >= 2) {
                        char sw[16];
                        std::snprintf(sw, sizeof(sw), "SW %02X%02X", rx[rx.size() - 2], rx[rx.size() - 1]);
                        log(sw);
                    }
                }
                s.close();
            });
        case ToolId::NfcPicopass:
            return stub(vm, assets, "Picopass / iCLASS",
                        "iCLASS/Picopass needs another frontend; Cap ST25 and PN532 do not implement it here.");
        case ToolId::NfcEink:
            return stub(vm, assets, "NFC e-ink", "Not supported on Cap ST25 / PN532 in this build.");

        case ToolId::NfcEmv: return make_emv(vm, assets);
        case ToolId::NfcSrix: return make_srix(vm, assets);
        case ToolId::NfcAmiibolink: {
            const auto prev = vm.current_page();
            vm.open_tool(ToolId::AmiiboStatus);
            if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
            return make_info(vm, assets, "Amiibolink", "Opening BLE status (connect/read only)...");
        }
        case ToolId::NfcPn532Ble: {
            const auto prev = vm.current_page();
            vm.open_tool(ToolId::Pn532Status);
            if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
            return make_info(vm, assets, "PN532 BLE", "Opening BLE status (connect/read only)...");
        }
        case ToolId::NfcPn532Uart: {
            const auto prev = vm.current_page();
            vm.open_tool(ToolId::Pn532KillerUart);
            if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
            return make_info(vm, assets, "PN532 UART", "Opening Modules UART probe...");
        }
        case ToolId::NfcChameleon: {
            const auto prev = vm.current_page();
            vm.open_tool(ToolId::ChameleonStatus);
            if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
            return make_info(vm, assets, "Chameleon", "Opening BLE status (connect/read only)...");
        }

        case ToolId::NfcModule: return make_config_module(vm, assets, st);
        case ToolId::NfcWiring: return make_wiring(vm, assets);
        case ToolId::NfcAddKey: return make_add_key(vm, assets, st);
        default: break;
    }
    return nullptr;
}

} // namespace app

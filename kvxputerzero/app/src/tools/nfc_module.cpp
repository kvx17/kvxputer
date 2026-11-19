#include "nfc_module.h"

#include "user_paths.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace tools {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

} // namespace

std::string nfc_dir() {
    const auto p = std::filesystem::path(config_dir()) / "nfc";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string nfc_subdir(const char* name) {
    const auto p = std::filesystem::path(nfc_dir()) / name;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

NfcModulePref nfc_module_pref() {
    std::ifstream in(std::filesystem::path(nfc_dir()) / "module.txt");
    if (!in) return NfcModulePref::Auto;
    std::string v;
    std::getline(in, v);
    v = trim(v);
    if (v == "st25") return NfcModulePref::St25;
    if (v == "pn532") return NfcModulePref::Pn532;
    return NfcModulePref::Auto;
}

void nfc_set_module_pref(NfcModulePref p) {
    std::ofstream out(std::filesystem::path(nfc_dir()) / "module.txt", std::ios::trunc);
    if (!out) return;
    switch (p) {
        case NfcModulePref::St25: out << "st25\n"; break;
        case NfcModulePref::Pn532: out << "pn532\n"; break;
        default: out << "auto\n"; break;
    }
}

const char* nfc_module_pref_label(NfcModulePref p) {
    switch (p) {
        case NfcModulePref::St25: return "ST25 SPI (GPIO22)";
        case NfcModulePref::Pn532: return "PN532 I2C";
        default: return "Auto (ST25 then PN532)";
    }
}

} // namespace tools

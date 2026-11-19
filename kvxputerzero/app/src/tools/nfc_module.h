#pragma once

#include <string>

namespace tools {

enum class NfcModulePref {
    Auto = 0,
    St25,
    Pn532,
};

NfcModulePref nfc_module_pref();
void nfc_set_module_pref(NfcModulePref p);
const char* nfc_module_pref_label(NfcModulePref p);
std::string nfc_dir();          // ~/.config/kvxputerzero/nfc
std::string nfc_subdir(const char* name);

} // namespace tools

#include "root/ui/quick_search_match.h"

String qsNormalize(const String &s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '-' || c == '_') c = ' ';
        out += c;
    }
    return out;
}

struct QsAlias {
    const char *alias;
    const char *target; // matched against normalized id or group
};

// Short names users type → catalog id or group.
static const QsAlias kAliases[] = {
    {"ir", "ir"},
    {"infrared", "ir"},
    {"nfc", "rfid"},
    {"rfid", "rfid"},
    {"usb", "usb"},
    {"badusb", "bad_usb"},
    {"wifi", "wifi"},
    {"ble", "ble"},
    {"rf", "rf"},
    {"gps", "gps"},
    {"totp", "totp"},
    {"calc", "calc"},
    {"calculator", "calc"},
    {"files", "files"},
    {"file", "files"},
    {"netops", "netops"},
    {"net", "netops"},
    {"pineap", "pineap_hunter"},
    {"hunter", "pineap_hunter"},
    {"clock", "clock"},
    {"config", "cfg"},
    {"settings", "cfg"},
    {"mqtt", "mqtt"},
    {"lora", "lora"},
    {"nrf", "nrf24"},
    {"fm", "fm"},
};

bool qsAliasHits(const String &normQuery, const String &id, const String &group) {
    if (normQuery.length() == 0) return false;
    const String nid = qsNormalize(id);
    const String ngrp = qsNormalize(group);
    for (size_t i = 0; i < sizeof(kAliases) / sizeof(kAliases[0]); i++) {
        if (normQuery != kAliases[i].alias) continue;
        const String tgt = qsNormalize(kAliases[i].target);
        if (nid == tgt || nid.indexOf(tgt) >= 0) return true;
        if (ngrp == tgt || ngrp.indexOf(tgt) >= 0) return true;
        if (tgt.indexOf(nid) >= 0 && nid.length() > 0) return true;
    }
    return false;
}

int qsScoreMatch(
    const String &normQuery, const String &label, const String &id, const String &group
) {
    if (normQuery.length() == 0) return 0;

    const String nLabel = qsNormalize(label);
    const String nId = qsNormalize(id);
    const String nGroup = qsNormalize(group);

    if (nLabel == normQuery || nId == normQuery) return 300;
    if (nLabel.startsWith(normQuery) || nId.startsWith(normQuery)) return 200;
    if (nLabel.indexOf(normQuery) >= 0 || nId.indexOf(normQuery) >= 0) return 100;
    if (nGroup.indexOf(normQuery) >= 0) return 50;
    if (qsAliasHits(normQuery, id, group)) return 50;
    return 0;
}

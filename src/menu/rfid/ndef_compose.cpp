/**
 * @file ndef_compose.cpp
 * Feature parity inspired by wakdev/nfctools-fz and Next-Flip NFC Maker;
 * reimplemented for kvxputer (no Flipper source).
 */
#include "ndef_compose.h"
#include "apdu.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <cstring>
#include <globals.h>

static void clearMsg(RFIDInterface *rfid) {
    memset(&rfid->ndefMessage, 0, sizeof(rfid->ndefMessage));
    rfid->ndefMessage.begin = 0x03;
    rfid->ndefMessage.header = 0xD1;
    rfid->ndefMessage.tnf = 0x01;
    rfid->ndefMessage.end = 0xFE;
    rfid->rawNdefRecord.clear();
}

void ndefBuildText(RFIDInterface *rfid, const String &text) {
    clearMsg(rfid);
    rfid->ndefMessage.payloadType = RFIDInterface::NDEF_TEXT;
    rfid->ndefMessage.payload[0] = 0x02;
    rfid->ndefMessage.payload[1] = 'e';
    rfid->ndefMessage.payload[2] = 'n';
    size_t i = 0;
    for (; i < text.length() && (i + 3) < sizeof(rfid->ndefMessage.payload); i++) {
        rfid->ndefMessage.payload[i + 3] = (uint8_t)text.charAt(i);
    }
    rfid->ndefMessage.payloadSize = (uint8_t)(i + 3);
    rfid->ndefMessage.messageSize = rfid->ndefMessage.payloadSize + 4;
}

void ndefBuildUri(RFIDInterface *rfid, const String &url) {
    clearMsg(rfid);
    static const struct {
        const char *prefix;
        uint8_t uic;
    } kUriAbbrev[] = {
        {"https://www.", 2},
        {"http://www.",  1},
        {"https://",     4},
        {"http://",      3},
        {"tel:",         5},
        {"mailto:",      6},
        {"ftp://anonymous:anonymous@", 7},
        {"ftp://ftp.",   8},
        {"ftps://",      9},
        {"sftp://",      10},
        {"smb://",       11},
        {"nfs://",       12},
        {"ftp://",       13},
        {"dav://",       14},
        {"news:",        15},
        {"telnet://",    16},
        {"imap:",        17},
        {"rtsp://",      18},
        {"urn:",         19},
        {"pop:",         20},
        {"sip:",         21},
        {"sips:",        22},
        {"tftp:",        23},
        {"btspp://",     24},
        {"btl2cap://",   25},
        {"btgoep://",    26},
        {"tcpobex://",   27},
        {"irdaobex://",  28},
        {"file://",      29},
        {"urn:epc:id:",  30},
        {"urn:epc:tag:", 31},
        {"urn:epc:pat:", 32},
        {"urn:epc:raw:", 33},
        {"urn:epc:",     34},
        {"urn:nfc:",     35},
    };

    uint8_t uic = 0;
    String data = url;
    for (auto &entry : kUriAbbrev) {
        if (data.startsWith(entry.prefix)) {
            uic = entry.uic;
            data = data.substring(strlen(entry.prefix));
            break;
        }
    }

    // sms: / geo: / bitcoin: / facetime: have no UIC; keep full string with code 0
    rfid->ndefMessage.payloadType = RFIDInterface::NDEF_URI;
    rfid->ndefMessage.payload[0] = uic;
    size_t i = 0;
    for (; i < data.length() && (i + 1) < sizeof(rfid->ndefMessage.payload); i++) {
        rfid->ndefMessage.payload[i + 1] = (uint8_t)data.charAt(i);
    }
    rfid->ndefMessage.payloadSize = (uint8_t)(i + 1);
    rfid->ndefMessage.messageSize = rfid->ndefMessage.payloadSize + 4;
}

void ndefBuildWifi(RFIDInterface *rfid, const String &ssid, const String &password, uint8_t auth,
                   uint8_t encr) {
    clearMsg(rfid);
    std::vector<uint8_t> cred;
    auto append = [](std::vector<uint8_t> &out, uint16_t type, const std::vector<uint8_t> &val) {
        out.push_back((uint8_t)(type >> 8));
        out.push_back((uint8_t)(type & 0xFF));
        out.push_back((uint8_t)(val.size() >> 8));
        out.push_back((uint8_t)(val.size() & 0xFF));
        out.insert(out.end(), val.begin(), val.end());
    };
    append(cred, 0x1026, {0x01});
    append(cred, 0x1045, std::vector<uint8_t>(ssid.begin(), ssid.end()));
    append(cred, 0x1003, {0x00, auth});
    append(cred, 0x100F, {0x00, encr});
    if (password.length()) append(cred, 0x1027, std::vector<uint8_t>(password.begin(), password.end()));
    append(cred, 0x1020, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    std::vector<uint8_t> payload;
    append(payload, 0x100E, cred);
    rfid->rawNdefRecord = Ndef::mimeRecord("application/vnd.wfa.wsc", payload, {'1'});
}

void ndefBuildVcard(RFIDInterface *rfid, const String &first, const String &last, const String &email,
                    const String &phone, const String &url) {
    clearMsg(rfid);
    String vcard = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
    vcard += "N:" + last + ";" + first + ";;;\r\n";
    vcard += "FN:" + first;
    if (last.length()) vcard += " " + last;
    vcard += "\r\n";
    if (email.length()) vcard += "EMAIL:" + email + "\r\n";
    if (phone.length()) vcard += "TEL:" + phone + "\r\n";
    if (url.length()) vcard += "URL:" + url + "\r\n";
    vcard += "END:VCARD\r\n";
    std::vector<uint8_t> payload(vcard.begin(), vcard.end());
    rfid->rawNdefRecord = Ndef::mimeRecord("text/vcard", payload);
}

void ndefBuildBluetoothOob(RFIDInterface *rfid, const uint8_t mac[6]) {
    clearMsg(rfid);
    std::vector<uint8_t> payload = {0x08, 0x00};
    payload.insert(payload.end(), mac, mac + 6);
    rfid->rawNdefRecord = Ndef::mimeRecord("application/vnd.bluetooth.ep.oob", payload);
}

void ndefBuildEmpty(RFIDInterface *rfid) {
    clearMsg(rfid);
    // Empty NDEF message: TNF=0, type="", payload=""
    rfid->rawNdefRecord = {0xD0, 0x00, 0x00};
}

void ndefBuildCustomRaw(RFIDInterface *rfid, const std::vector<uint8_t> &record) {
    clearMsg(rfid);
    rfid->rawNdefRecord = record;
}

std::vector<uint8_t> ndefBuildTlvFrom(RFIDInterface *rfid) {
    std::vector<uint8_t> record;
    if (!rfid->rawNdefRecord.empty()) {
        record = rfid->rawNdefRecord;
    } else if (rfid->ndefMessage.payloadSize > 0) {
        record.push_back(0xD1);
        record.push_back(0x01);
        record.push_back(rfid->ndefMessage.payloadSize);
        record.push_back(rfid->ndefMessage.payloadType);
        for (uint8_t i = 0; i < rfid->ndefMessage.payloadSize; i++) {
            record.push_back(rfid->ndefMessage.payload[i]);
        }
    } else {
        return {};
    }

    std::vector<uint8_t> tlv;
    tlv.push_back(0x03);
    if (record.size() < 0xFF) {
        tlv.push_back((uint8_t)record.size());
    } else {
        tlv.push_back(0xFF);
        tlv.push_back((uint8_t)(record.size() >> 8));
        tlv.push_back((uint8_t)(record.size() & 0xFF));
    }
    tlv.insert(tlv.end(), record.begin(), record.end());
    tlv.push_back(0xFE);
    return tlv;
}

static bool promptUri(RFIDInterface *rfid, const String &prefix, const char *prompt) {
    String data = keyboard(prefix, 120, prompt);
    if (data == "\x1B") return false;
    ndefBuildUri(rfid, data);
    return true;
}

static bool composeWifi(RFIDInterface *rfid) {
    uint8_t auth = 0x20; // WPA2 Personal
    uint8_t encr = 0x08; // AES
    options = {
        {"WPA2 Personal", [&]() { auth = 0x20; }},
        {"WPA Personal",  [&]() { auth = 0x02; }},
        {"Open",          [&]() { auth = 0x01; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Wi-Fi auth") < 0) return false;
    if (auth != 0x01) {
        options = {
            {"AES",      [&]() { encr = 0x08; }},
            {"TKIP",     [&]() { encr = 0x04; }},
            {"TKIP+AES", [&]() { encr = 0x0C; }},
        };
        if (loopOptions(options, MENU_TYPE_SUBMENU, "Encryption") < 0) return false;
    } else {
        encr = 0x01;
    }

    String ssid = keyboard("", 32, "SSID:");
    if (ssid == "\x1B") return false;
    String pwd;
    if (auth != 0x01) {
        pwd = keyboard("", 63, "Password:", true);
        if (pwd == "\x1B") return false;
    }
    ndefBuildWifi(rfid, ssid, pwd, auth, encr);
    return true;
}

static bool composeContact(RFIDInterface *rfid) {
    String first = keyboard("", 40, "First name:");
    if (first == "\x1B") return false;
    String last = keyboard("", 40, "Last name:");
    if (last == "\x1B") return false;
    String email = keyboard("", 60, "Email (opt):");
    if (email == "\x1B") return false;
    String phone = keyboard("", 20, "Phone (opt):");
    if (phone == "\x1B") return false;
    String url = keyboard("https://", 80, "URL (opt):");
    if (url == "\x1B") return false;
    if (url == "https://") url = "";
    ndefBuildVcard(rfid, first, last, email, phone, url);
    return true;
}

static bool composeBluetooth(RFIDInterface *rfid) {
    String mac = keyboard("", 12, "BT MAC (12 hex):");
    if (mac == "\x1B") return false;
    mac.replace(":", "");
    mac.replace(" ", "");
    mac.toUpperCase();
    if (mac.length() != 12) {
        displayError("Need 12 hex digits.", true);
        return false;
    }
    uint8_t bytes[6];
    for (int i = 0; i < 6; i++) {
        bytes[i] = (uint8_t)strtoul(mac.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
    }
    ndefBuildBluetoothOob(rfid, bytes);
    return true;
}

static bool composeCustom(RFIDInterface *rfid) {
    bool asHex = false;
    options = {
        {"Plain text", [&]() { asHex = false; }},
        {"Hex bytes",  [&]() { asHex = true; } },
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Custom data") < 0) return false;
    String data = keyboard("", 100, asHex ? "Hex:" : "Text:");
    if (data == "\x1B") return false;
    if (asHex) {
        data.replace(" ", "");
        std::vector<uint8_t> payload;
        for (size_t i = 0; i + 1 < data.length(); i += 2) {
            payload.push_back((uint8_t)strtoul(data.substring(i, i + 2).c_str(), nullptr, 16));
        }
        String text((const char *)payload.data(), payload.size());
        ndefBuildText(rfid, text);
    } else {
        ndefBuildText(rfid, data);
    }
    return true;
}

static bool composeHttpsUrl(RFIDInterface *rfid) {
    String prefix = "https://";
    options = {
        {"https://",     [&]() { prefix = "https://"; }    },
        {"https://www.", [&]() { prefix = "https://www."; }},
        {"http://",      [&]() { prefix = "http://"; }     },
        {"None",         [&]() { prefix = ""; }            },
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "URL prefix") < 0) return false;
    return promptUri(rfid, prefix, "URL:");
}

static bool composeSocial(RFIDInterface *rfid) {
    String base;
    options = {
        {"Twitter/X", [&]() { base = "https://x.com/"; }         },
        {"Instagram", [&]() { base = "https://instagram.com/"; } },
        {"LinkedIn",  [&]() { base = "https://linkedin.com/in/"; }},
        {"GitHub",    [&]() { base = "https://github.com/"; }    },
        {"YouTube",   [&]() { base = "https://youtube.com/@"; }  },
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Network") < 0) return false;
    String handle = keyboard(base, 120, "Profile URL:");
    if (handle == "\x1B") return false;
    ndefBuildUri(rfid, handle);
    return true;
}

static bool composeSearch(RFIDInterface *rfid) {
    String engine;
    options = {
        {"Google",    [&]() { engine = "https://www.google.com/search?q="; }},
        {"DuckDuckGo",[&]() { engine = "https://duckduckgo.com/?q="; }     },
        {"Bing",      [&]() { engine = "https://www.bing.com/search?q="; } },
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Engine") < 0) return false;
    String q = keyboard("", 80, "Query:");
    if (q == "\x1B") return false;
    q.replace(" ", "+");
    ndefBuildUri(rfid, engine + q);
    return true;
}

static bool composeSavedLinks(RFIDInterface *rfid) {
    if (kvxConfig.qrCodes.empty()) {
        displayError("No saved links.", true);
        return false;
    }
    int picked = -1;
    options.clear();
    for (size_t i = 0; i < kvxConfig.qrCodes.size(); i++) {
        options.emplace_back(kvxConfig.qrCodes[i].menuName, [&, i]() { picked = (int)i; });
    }
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Saved links") < 0 || picked < 0) return false;
    ndefBuildUri(rfid, kvxConfig.qrCodes[picked].content);
    return true;
}

bool ndefComposeInteractive(RFIDInterface *rfid) {
    // Two-phase: pick a type without nesting loopOptions inside callbacks.
    // Nesting reassigns the global `options` vector and destroys live lambdas → crash.
    enum : int {
        PickUrl = 0,
        PickText,
        PickMail,
        PickPhone,
        PickSms,
        PickFacetime,
        PickFtAudio,
        PickContact,
        PickWifi,
        PickBluetooth,
        PickLocation,
        PickBitcoin,
        PickUnitLink,
        PickSocial,
        PickSearch,
        PickCustom,
        PickEmpty,
        PickSaved,
    };

    int pick = -1;
    options = {
        {"HTTPS / URL", [&]() { pick = PickUrl; }      },
        {"Text",        [&]() { pick = PickText; }     },
        {"Mail",        [&]() { pick = PickMail; }     },
        {"Phone",       [&]() { pick = PickPhone; }    },
        {"SMS",         [&]() { pick = PickSms; }      },
        {"Facetime",    [&]() { pick = PickFacetime; } },
        {"FT Audio",    [&]() { pick = PickFtAudio; }  },
        {"Contact",     [&]() { pick = PickContact; }  },
        {"Wi-Fi",       [&]() { pick = PickWifi; }     },
        {"Bluetooth",   [&]() { pick = PickBluetooth; }},
        {"Location",    [&]() { pick = PickLocation; } },
        {"Bitcoin",     [&]() { pick = PickBitcoin; }  },
        {"Unit.Link",   [&]() { pick = PickUnitLink; } },
        {"Social",      [&]() { pick = PickSocial; }   },
        {"Search",      [&]() { pick = PickSearch; }   },
        {"Custom",      [&]() { pick = PickCustom; }   },
        {"Empty",       [&]() { pick = PickEmpty; }    },
        {"Saved links", [&]() { pick = PickSaved; }    },
    };

    if (loopOptions(options, MENU_TYPE_SUBMENU, "NDEF content") < 0 || pick < 0) return false;

    switch (pick) {
        case PickUrl: return composeHttpsUrl(rfid);
        case PickText: {
            String t = keyboard("", 120, "Text:");
            if (t == "\x1B") return false;
            ndefBuildText(rfid, t);
            return true;
        }
        case PickMail: return promptUri(rfid, "mailto:", "Email:");
        case PickPhone: return promptUri(rfid, "tel:", "Phone:");
        case PickSms: return promptUri(rfid, "sms:", "SMS number:");
        case PickFacetime: return promptUri(rfid, "facetime:", "Facetime:");
        case PickFtAudio: return promptUri(rfid, "facetime-audio:", "Facetime Audio:");
        case PickContact: return composeContact(rfid);
        case PickWifi: return composeWifi(rfid);
        case PickBluetooth: return composeBluetooth(rfid);
        case PickLocation: return promptUri(rfid, "geo:", "geo:lat,lon:");
        case PickBitcoin: return promptUri(rfid, "bitcoin:", "Bitcoin URI:");
        case PickUnitLink: {
            String u = keyboard("https://unit.link/", 120, "Unit.Link:");
            if (u == "\x1B") return false;
            ndefBuildUri(rfid, u);
            return true;
        }
        case PickSocial: return composeSocial(rfid);
        case PickSearch: return composeSearch(rfid);
        case PickCustom: return composeCustom(rfid);
        case PickEmpty:
            ndefBuildEmpty(rfid);
            return true;
        case PickSaved: return composeSavedLinks(rfid);
        default: return false;
    }
}

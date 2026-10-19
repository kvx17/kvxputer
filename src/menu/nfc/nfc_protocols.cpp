/**
 * @file nfc_protocols.cpp
 * ISO15693, DESFire, UL-C, FeliCa, forced-type read.
 */
#include "nfc_protocols.h"
#include "nfc_session.h"
#include "menu/rfid/tag_o_matic.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>

#if !defined(REMOVE_RFID_HW_INTERFACE)

void nfcIso15693Tool() {
    int act = -1;
    options = {
        {"Inventory",   [&]() { act = 0; }},
        {"Read block",  [&]() { act = 1; }},
        {"Write block", [&]() { act = 2; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "ISO15693") < 0 || act < 0) return;

    NfcSession s;
    if (!s.open(false)) return;

    if (act == 0) {
        String uid;
        int rc = s.rfid->iso15693_inventory(uid);
        if (rc == RFIDInterface::SUCCESS) {
            ScrollableTextArea area("ISO15693");
            area.addLine("UID: " + uid);
            if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
            area.addLine("");
            area.addLine("Esc = back");
            area.show();
        } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
            // Fallback: normal read may already fill NFC-V via ST25.
            nfcInfo("Hold ISO15693 tag, then press a key.");
            if (s.rfid->read() == RFIDInterface::SUCCESS) {
                ScrollableTextArea area("ISO15693");
                area.addLine("Type: " + s.rfid->printableUID.picc_type);
                area.addLine("UID: " + s.rfid->printableUID.uid);
                if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
                area.show();
            } else {
                nfcRefuse("No ISO15693 tag / unsupported.");
            }
        } else {
            nfcRefuse("Inventory failed.");
        }
        return;
    }

    String blk = keyboard("0", 3, "Block #:");
    if (blk == "\x1B") return;
    uint8_t block = (uint8_t)blk.toInt();
    if (act == 1) {
        uint8_t data[8] = {};
        int rc = s.rfid->iso15693_read_block(block, data, sizeof(data));
        if (rc == RFIDInterface::SUCCESS) {
            String hex;
            for (int i = 0; i < 4; i++) {
                char b[4];
                sprintf(b, "%02X ", data[i]);
                hex += b;
            }
            displaySuccess("Blk " + String(block) + ": " + hex, true);
        } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
            nfcRefuse("ISO15693 read block\nnot on this module yet.");
        } else {
            nfcRefuse("Read block failed.");
        }
    } else {
        String hex = keyboard("", 8, "4 bytes hex:");
        if (hex == "\x1B") return;
        hex.replace(" ", "");
        if (hex.length() < 8) {
            nfcRefuse("Need 8 hex digits.");
            return;
        }
        uint8_t data[4];
        for (int i = 0; i < 4; i++)
            data[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        int rc = s.rfid->iso15693_write_block(block, data, 4);
        if (rc == RFIDInterface::SUCCESS) displaySuccess("Write OK.", true);
        else if (rc == RFIDInterface::NOT_IMPLEMENTED)
            nfcRefuse("ISO15693 write not\nimplemented on this driver.");
        else nfcRefuse("Write failed.");
    }
}

void nfcDesfireBrowser() {
    NfcSession s;
    if (!s.open(true)) return;
    int rc = s.rfid->desfire_list();
    ScrollableTextArea area(FP, BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "DESFire");
    }
    area.addLine("UID: " + s.rfid->printableUID.uid);
    area.addLine("Type: " + s.rfid->printableUID.picc_type);
    if (rc == RFIDInterface::SUCCESS || s.rfid->strAllPages.length()) {
        if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
        else area.addLine("No plain apps/files.");
    } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        area.addLine("DESFire list not on module.");
        area.addLine("Chip info may still show version.");
    } else {
        area.addLine("List failed / not DESFire.");
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcUlcAuth() {
    int mode = 0;
    options = {
        {"Manual 16-byte key", [&]() { mode = 0; }},
        {"Try SD dictionary",  [&]() { mode = 1; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Ultralight-C") < 0) return;

    std::vector<String> keys;
    if (mode == 0) {
        String k = keyboard("", 32, "UL-C key 32 hex:");
        if (k == "\x1B") return;
        k.replace(" ", "");
        if (k.length() != 32) {
            nfcRefuse("Need 32 hex digits.");
            return;
        }
        keys.push_back(k);
    } else {
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs || !fs->exists(kvx::paths::RFID_ULC_KEYS)) {
            nfcRefuse("Missing ulc_keys.txt\n" + String(kvx::paths::RFID_ULC_KEYS));
            return;
        }
        File f = fs->open(kvx::paths::RFID_ULC_KEYS, FILE_READ);
        while (f && f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            line.replace(" ", "");
            if (line.length() == 32) keys.push_back(line);
        }
        if (f) f.close();
        if (keys.empty()) {
            nfcRefuse("No 32-hex keys in dict.");
            return;
        }
    }

    NfcSession s;
    if (!s.open(false)) return;
    nfcInfo("Hold UL-C tag, then press a key.");
    bool ok = false;
    for (auto &hex : keys) {
        uint8_t key[16];
        for (int i = 0; i < 16; i++)
            key[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        int rc = s.rfid->ulc_auth(key);
        if (rc == RFIDInterface::SUCCESS) {
            ok = true;
            break;
        }
        if (rc == RFIDInterface::NOT_IMPLEMENTED) {
            nfcRefuse("UL-C auth not on this module.");
            return;
        }
    }
    if (!ok) {
        nfcRefuse("UL-C auth failed.");
        return;
    }
    s.rfid->read();
    ScrollableTextArea area("UL-C dump");
    area.addLine("Auth OK");
    area.addLine("UID: " + s.rfid->printableUID.uid);
    if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
    area.show();
}

void nfcFelicaServices() {
    NfcSession s;
    if (!s.open(false)) return;
    nfcInfo("Hold FeliCa tag, then press a key.");
    int rc = s.rfid->felica_services();
    if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        // FeliCa poll via read(1)
        if (s.rfid->read(1) != RFIDInterface::SUCCESS) {
            nfcRefuse("No FeliCa / unsupported.");
            return;
        }
    } else if (rc != RFIDInterface::SUCCESS && s.rfid->read(1) != RFIDInterface::SUCCESS) {
        nfcRefuse("FeliCa services failed.");
        return;
    }
    ScrollableTextArea area("FeliCa");
    area.addLine("IDm: " + s.rfid->printableUID.uid);
    area.addLine("PMm: " + s.rfid->printableUID.sak);
    area.addLine("Sys: " + s.rfid->printableUID.atqa);
    if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
    else area.addLine("(service blocks: best-effort)");
    area.show();
}

void nfcForcedTypeRead() {
    int t = RFIDInterface::READ_AS_AUTO;
    options = {
        {"Auto",          [&]() { t = RFIDInterface::READ_AS_AUTO; }    },
        {"MIFARE Classic",[&]() { t = RFIDInterface::READ_AS_CLASSIC; } },
        {"UL / NTAG",     [&]() { t = RFIDInterface::READ_AS_UL_NTAG; } },
        {"ISO15693",      [&]() { t = RFIDInterface::READ_AS_ISO15693; }},
        {"FeliCa",        [&]() { t = RFIDInterface::READ_AS_FELICA; }  },
        {"ISO-DEP",       [&]() { t = RFIDInterface::READ_AS_ISODEP; }  },
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Force type") < 0) return;

    NfcSession s;
    if (!s.open(false)) return;
    nfcInfo("Hold tag, then press a key.");
    int rc = s.rfid->read_as((RFIDInterface::ReadAsType)t);
    if (rc != RFIDInterface::SUCCESS && rc != RFIDInterface::NOT_IMPLEMENTED) {
        nfcRefuse("Forced read failed.");
        return;
    }
    if (rc == RFIDInterface::NOT_IMPLEMENTED) rc = s.rfid->read(t == RFIDInterface::READ_AS_FELICA ? 1 : 0);
    ScrollableTextArea area("Forced read");
    area.addLine("Type: " + s.rfid->printableUID.picc_type);
    area.addLine("UID: " + s.rfid->printableUID.uid);
    area.addLine("ATQA: " + s.rfid->printableUID.atqa);
    area.addLine("SAK: " + s.rfid->printableUID.sak);
    if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
    area.show();
}

void nfcTagsMenu() {
    int pick = -1;
    options = {
        {"ISO15693 tool",   [&]() { pick = 0; }},
        {"DESFire browser", [&]() { pick = 1; }},
        {"Ultralight-C",    [&]() { pick = 2; }},
        {"FeliCa services", [&]() { pick = 3; }},
        {"Forced type read",[&]() { pick = 4; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Tags") < 0 || pick < 0) return;
    switch (pick) {
        case 0: nfcIso15693Tool(); break;
        case 1: nfcDesfireBrowser(); break;
        case 2: nfcUlcAuth(); break;
        case 3: nfcFelicaServices(); break;
        case 4: nfcForcedTypeRead(); break;
        default: break;
    }
}

#else
void nfcTagsMenu() { displayError("No NFC HW.", true); }
void nfcIso15693Tool() { displayError("No NFC HW.", true); }
void nfcDesfireBrowser() { displayError("No NFC HW.", true); }
void nfcUlcAuth() { displayError("No NFC HW.", true); }
void nfcFelicaServices() { displayError("No NFC HW.", true); }
void nfcForcedTypeRead() { displayError("No NFC HW.", true); }
#endif

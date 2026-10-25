/**
 * @file nfc_classic.cpp
 * Classic Detect Reader / MFKey / dict / Magic / dump editor.
 * Behavior inspired by Flipper NFC + mfkey; reimplemented for kvxputer.
 */
#include "nfc_classic.h"
#include "nfc_session.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/scrollableTextArea.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include <globals.h>

#if !defined(REMOVE_RFID_HW_INTERFACE)

static void ensureDir(const char *path) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return;
    if (!fs->exists(path)) fs->mkdir(path);
}

void nfcDetectReader() {
#ifndef LITE_VERSION
    NfcSession s;
    nfcTopBar("Detect");
    nfcInfo("Load a Classic dump first\n(or Read a Classic tag),\nthen press a key.\nNext: load/read, then hold near a reader.");
    if (!s.open(false)) return;

    if (s.rfid->load() != RFIDInterface::SUCCESS && s.rfid->printableUID.uid.length() == 0) {
        nfcInfo("Hold Classic tag, then press a key.");
        if (s.rfid->read() != RFIDInterface::SUCCESS) {
            nfcRefuse("Need a Classic dump or tag.");
            return;
        }
    }

    ensureDir(kvx::paths::RFID_NONCES);
    String fname = "nonce_" + String(millis()) + ".bin";
    String path = String(kvx::paths::RFID_NONCES) + "/" + fname;

    nfcTopBar("Detect");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
    padprintln("Emulating dump at reader...");
    padprintln("Esc = stop");
    padprintln("Unit NFC I2C: no IRQ —");
    padprintln("harvest may be slow.");

    int rc = s.rfid->harvest_nonces(path, 45000);
    if (rc == RFIDInterface::SUCCESS) {
        displaySuccess("Nonces saved:\n" + path, true);
    } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        nfcRefuse("Detect Reader needs ST25/PN532\nClassic emulate + harvest.\nNot on this module yet.");
    } else if (rc == RFIDInterface::TAG_NOT_PRESENT) {
        nfcRefuse("No reader nonces collected.\nTry PN532 or longer tap.");
    } else {
        nfcRefuse("Harvest failed.");
    }
#else
    nfcRefuse("Not in lite build.");
#endif
}

void nfcMfkey() {
#ifndef LITE_VERSION
    nfcTopBar("MFKey");
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        nfcRefuse("No storage.");
        return;
    }
    ensureDir(kvx::paths::RFID_NONCES);
    File dir = fs->open(kvx::paths::RFID_NONCES);
    if (!dir || !dir.isDirectory()) {
        nfcRefuse("No nonce files.\nRun Detect Reader first.");
        return;
    }

    std::vector<String> files;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) files.push_back(String(f.name()));
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    if (files.empty()) {
        nfcRefuse("No nonce files.\nRun Detect Reader first.");
        return;
    }

    int pick = -1;
    options.clear();
    for (size_t i = 0; i < files.size(); i++) {
        options.emplace_back(files[i], [&, i]() { pick = (int)i; });
    }
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Nonce file") < 0 || pick < 0) return;

    String path = String(kvx::paths::RFID_NONCES) + "/" + files[pick];
    File nf = fs->open(path, FILE_READ);
    if (!nf) {
        nfcRefuse("Cannot open nonce file.");
        return;
    }
    size_t sz = nf.size();
    nf.close();

    nfcTopBar("MFKey");
    tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
    padprintln("Cracking " + files[pick]);
    padprintln("Size: " + String(sz) + " bytes");
    padprintln("This can take minutes...");
    padprintln("Esc = cancel");

    // Lightweight placeholder cracker: scan file for 6-byte key-like patterns
    // already written by a future harvest path; also accept ASCII hex keys.
    // Full MFKey32 LFSR recovery is a follow-on — we still append any embedded keys.
    uint32_t found = 0;
    uint32_t start = millis();
    bool cancel = false;
    File rf = fs->open(path, FILE_READ);
    String ascii;
    while (rf && rf.available() && !cancel) {
        if (check(EscPress)) {
            cancel = true;
            break;
        }
        int c = rf.read();
        if (c < 0) break;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            ascii += (char)c;
            if (ascii.length() >= 12) {
                String key = ascii.substring(ascii.length() - 12);
                key.toUpperCase();
                bool ok = true;
                for (char ch : key) {
                    if (!isxdigit((unsigned char)ch)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    kvxConfig.addMifareKey(key);
                    found++;
                }
            }
        } else {
            ascii = "";
        }
        if ((millis() - start) % 500 < 20) {
            esp_task_wdt_reset();
            yield();
        }
    }
    if (rf) rf.close();

    if (cancel) {
        displayError("Cancelled.", true);
        return;
    }
    if (found == 0) {
        nfcRefuse(
            "No keys recovered.\n"
            "Nonce format needs a full\n"
            "MFKey32 pass (future) or\n"
            "ASCII hex keys in the file."
        );
        return;
    }
    displaySuccess(String(found) + " key(s) added.\nRead again with new keys.", true);
#else
    nfcRefuse("Not in lite build.");
#endif
}

void nfcDictAttack() {
    int mode = 0; // 0 full, 1 known-only
    options = {
        {"Full dictionary", [&]() { mode = 0; }},
        {"Known keys only", [&]() { mode = 1; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Dict Atk") < 0) return;

    NfcSession s;
    nfcTopBar("Dict");
    if (!s.open(false)) return;

    nfcInfo("Hold Classic tag, then press a key.");
    nfcTopBar("Dict");
    tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
    padprintln(mode ? "Known keys only..." : "Builtin + user keys...");
    padprintln("Esc = abort");

    int rc = s.rfid->dict_read_classic(mode == 1);
    if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        // Fallback: normal read (drivers already try dict internally).
        rc = s.rfid->read();
    }
    ScrollableTextArea area(uiBodyFont(), BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "Dict");
    }
    if (rc == RFIDInterface::SUCCESS) {
        area.addLine("Read OK");
        area.addLine("UID: " + s.rfid->printableUID.uid);
        area.addLine("Type: " + s.rfid->printableUID.picc_type);
        if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
    } else if (rc == RFIDInterface::TAG_AUTH_ERROR) {
        area.addLine("Auth failed on sectors.");
        area.addLine("Add keys (Config) or MFKey.");
        area.addLine("UID: " + s.rfid->printableUID.uid);
        if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
    } else {
        area.addLine("Dict read failed.");
        area.addLine(s.rfid->statusMessage(rc));
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcMagicWizard() {
    int act = -1;
    options = {
        {"Probe magic type", [&]() { act = 0; }},
        {"Write UID",        [&]() { act = 1; }},
        {"Write full dump",  [&]() { act = 2; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "NFC Magic") < 0 || act < 0) return;

    NfcSession s;
    if (!s.open(false)) return;

    if (act == 0) {
        nfcInfo("Hold magic card, then press a key.");
        auto t = s.rfid->magic_probe();
        const char *name = "None / unknown";
        if (t == RFIDInterface::MAGIC_GEN1) name = "Gen1";
        else if (t == RFIDInterface::MAGIC_GEN2) name = "Gen2";
        else if (t == RFIDInterface::MAGIC_GEN4) name = "Gen4";
        else if (t == RFIDInterface::MAGIC_UNKNOWN) name = "Responded (unknown)";
        if (t == RFIDInterface::MAGIC_NONE) {
            nfcRefuse("Not a known magic card\n(or probe unsupported).");
        } else {
            displaySuccess(String("Magic: ") + name, true);
        }
        return;
    }

    nfcInfo("Next: pick a Classic dump file.\nPress a key to continue.");
    if (s.rfid->load() != RFIDInterface::SUCCESS) {
        nfcRefuse("Load a Classic dump first.");
        return;
    }
    nfcInfo("Hold blank magic card, then press a key.");
    int rc = (act == 1) ? s.rfid->magic_write_uid() : s.rfid->magic_write_dump();
    if (rc == RFIDInterface::SUCCESS) displaySuccess("Magic write OK.", true);
    else if (rc == RFIDInterface::NOT_IMPLEMENTED)
        nfcRefuse("Magic write not on this module.\nUse ST25 Unit NFC.");
    else if (rc == RFIDInterface::TAG_NOT_PRESENT) nfcRefuse("No card.");
    else nfcRefuse("Magic write failed.");
}

void nfcDumpEditor() {
    NfcSession s;
    if (!s.open(false)) return;
    if (s.rfid->load() != RFIDInterface::SUCCESS) {
        nfcRefuse("Load a .rfid / .nfc dump.");
        return;
    }

    // Parse "Page N: XX XX XX XX" or "Block N: ..." lines into editable hex lines.
    String dump = s.rfid->strAllPages;
    if (!dump.length()) {
        nfcRefuse("Dump has no page data.");
        return;
    }

    ScrollableTextArea area(uiBodyFont(), BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "Edit");
    }
    area.addLine("UID: " + s.rfid->printableUID.uid);
    area.addLine("OK = edit one line");
    area.addLine("");
    area.fromString(dump);
    if (!area.show()) return;

    String line = keyboard("", 48, "Block/Page hex:");
    if (line == "\x1B") return;
    line.trim();
    // Append edited line into dump text and save.
    s.rfid->strAllPages += "\n# edited\n" + line + "\n";
    String name = keyboard("edited", 24, "Save as:");
    if (name == "\x1B") return;
    int rc = s.rfid->save(name);
    if (rc == RFIDInterface::SUCCESS) displaySuccess("Saved " + name + ".rfid", true);
    else displayError("Save failed.", true);
}

void nfcClassicMenu() {
    int pick = -1;
    options = {
#ifndef LITE_VERSION
        {"Detect Reader", [&]() { pick = 0; }},
        {"MFKey",         [&]() { pick = 1; }},
#endif
        {"Dict attack",   [&]() { pick = 2; }},
        {"NFC Magic",     [&]() { pick = 3; }},
        {"Dump editor",   [&]() { pick = 4; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Classic") < 0 || pick < 0) return;
    switch (pick) {
#ifndef LITE_VERSION
        case 0: nfcDetectReader(); break;
        case 1: nfcMfkey(); break;
#endif
        case 2: nfcDictAttack(); break;
        case 3: nfcMagicWizard(); break;
        case 4: nfcDumpEditor(); break;
        default: break;
    }
}

#else
void nfcClassicMenu() { displayError("No NFC HW.", true); }
void nfcDetectReader() { displayError("No NFC HW.", true); }
void nfcMfkey() { displayError("No NFC HW.", true); }
void nfcDictAttack() { displayError("No NFC HW.", true); }
void nfcMagicWizard() { displayError("No NFC HW.", true); }
void nfcDumpEditor() { displayError("No NFC HW.", true); }
#endif

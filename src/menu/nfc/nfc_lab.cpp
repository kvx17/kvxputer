/**
 * @file nfc_lab.cpp
 * APDU scripts, compare dumps, field poll.
 */
#include "nfc_lab.h"
#include "nfc_session.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>

#if !defined(REMOVE_RFID_HW_INTERFACE)

static bool parseHexLine(const String &line, std::vector<uint8_t> &out) {
    out.clear();
    String s = line;
    s.trim();
    if (!s.length() || s.startsWith("#") || s.startsWith("//")) return false;
    s.replace(" ", "");
    s.replace(":", "");
    if (s.length() < 2 || (s.length() % 2)) return false;
    for (size_t i = 0; i + 1 < s.length(); i += 2) {
        out.push_back((uint8_t)strtoul(s.substring(i, i + 2).c_str(), nullptr, 16));
    }
    return !out.empty();
}

void nfcApduScript() {
    int src = 0;
    options = {
        {"Type lines", [&]() { src = 0; }},
        {"Load file",  [&]() { src = 1; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "APDU script") < 0) return;

    std::vector<String> lines;
    if (src == 0) {
        for (int i = 0; i < 8; i++) {
            String l = keyboard("", 64, ("APDU " + String(i + 1) + " (empty=end):").c_str());
            if (l == "\x1B") return;
            l.trim();
            if (!l.length()) break;
            lines.push_back(l);
        }
    } else {
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs) {
            nfcRefuse("No storage.");
            return;
        }
        if (!fs->exists(kvx::paths::RFID_APDU)) fs->mkdir(kvx::paths::RFID_APDU);
        // Pick first .txt in folder or prompt name
        String name = keyboard("script.txt", 32, "File in apdu/:");
        if (name == "\x1B") return;
        String path = String(kvx::paths::RFID_APDU) + "/" + name;
        File f = fs->open(path, FILE_READ);
        if (!f) {
            nfcRefuse("Cannot open " + path);
            return;
        }
        while (f.available()) {
            String l = f.readStringUntil('\n');
            l.trim();
            if (l.length()) lines.push_back(l);
        }
        f.close();
    }
    if (lines.empty()) {
        nfcRefuse("No APDUs.");
        return;
    }

    bool stopOnErr = true;
    options = {
        {"Stop on bad SW", [&]() { stopOnErr = true; } },
        {"Continue always", [&]() { stopOnErr = false; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "On error");

    NfcSession s;
    if (!s.open(true)) return;

    ScrollableTextArea area(FP, BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "APDU");
    }

    for (size_t i = 0; i < lines.size(); i++) {
        std::vector<uint8_t> tx;
        if (!parseHexLine(lines[i], tx)) {
            area.addLine("# skip: " + lines[i]);
            continue;
        }
        uint8_t rx[256];
        uint16_t rxLen = 0;
        int rc = s.rfid->transceive_apdu(tx.data(), (uint16_t)tx.size(), rx, sizeof(rx), &rxLen);
        String line = String(i + 1) + "> ";
        if (rc != RFIDInterface::SUCCESS) {
            line += "ERR";
            area.addLine(line);
            if (stopOnErr) break;
            continue;
        }
        uint16_t sw = 0;
        if (rxLen >= 2) sw = ((uint16_t)rx[rxLen - 2] << 8) | rx[rxLen - 1];
        char swb[8];
        sprintf(swb, "%04X", sw);
        line += "SW=";
        line += swb;
        area.addLine(line);
        String hex;
        for (uint16_t j = 0; j < rxLen && j < 24; j++) {
            char b[4];
            sprintf(b, "%02X", rx[j]);
            hex += b;
        }
        if (hex.length()) area.addLine("  " + hex);
        if (stopOnErr && sw != 0x9000 && (sw >> 8) != 0x61) break;
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcCompareDumps() {
    NfcSession s;
    if (!s.open(false)) return;
    nfcInfo("Next: pick a dump file to compare.\nPress a key to continue.");
    if (s.rfid->load() != RFIDInterface::SUCCESS) {
        nfcRefuse("Load failed.");
        return;
    }
    String fileUid = s.rfid->printableUID.uid;
    String fileSak = s.rfid->printableUID.sak;
    String fileAtqa = s.rfid->printableUID.atqa;
    String filePages = s.rfid->strAllPages;

    nfcInfo("Hold live tag, then press a key.");
    if (s.rfid->read() != RFIDInterface::SUCCESS) {
        nfcRefuse("No live tag.");
        return;
    }

    ScrollableTextArea area(FP, BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "Diff");
    }
    auto row = [&](const char *lab, const String &a, const String &b) {
        bool ok = (a == b);
        area.addLine(String(lab) + (ok ? " OK" : " DIFF"));
        if (!ok) {
            area.addLine("  file: " + a);
            area.addLine("  live: " + b);
        }
    };
    row("UID ", fileUid, s.rfid->printableUID.uid);
    row("SAK ", fileSak, s.rfid->printableUID.sak);
    row("ATQA", fileAtqa, s.rfid->printableUID.atqa);
    area.addLine(String("Pages ") + (filePages == s.rfid->strAllPages ? "OK" : "DIFF"));
    if (filePages != s.rfid->strAllPages) {
        area.addLine("--- file ---");
        area.fromString(filePages.substring(0, 400));
        area.addLine("--- live ---");
        area.fromString(s.rfid->strAllPages.substring(0, 400));
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcFieldPoll() {
    NfcSession s;
    if (!s.open(false)) return;
    nfcTopBar("Field");
    tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
    padprintln("Continuous poll");
    padprintln("Esc = stop");

    uint32_t lastHit = 0;
    bool wasPresent = false;
    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) break;
        int rc = s.rfid->field_poll();
        bool present = (rc == RFIDInterface::SUCCESS);
        if (present) lastHit = millis();
        if (present != wasPresent || (millis() % 500 < 30)) {
            nfcTopBar("Field");
            tft.setCursor(BORDER_PAD_X, KVX_TOPBAR_H + 4);
            padprintln(present ? "TAG PRESENT" : "no tag");
            if (s.rfid->printableUID.uid.length()) padprintln("UID: " + s.rfid->printableUID.uid);
            if (lastHit) padprintln("Last hit: " + String(millis() - lastHit) + " ms ago");
            padprintln("");
            padprintln("Esc = stop");
            wasPresent = present;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void nfcLabMenu() {
    int pick = -1;
    options = {
        {"APDU script",  [&]() { pick = 0; }},
        {"Compare dump", [&]() { pick = 1; }},
        {"Field poll",   [&]() { pick = 2; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Lab") < 0 || pick < 0) return;
    switch (pick) {
        case 0: nfcApduScript(); break;
        case 1: nfcCompareDumps(); break;
        case 2: nfcFieldPoll(); break;
        default: break;
    }
}

#else
void nfcLabMenu() { displayError("No NFC HW.", true); }
void nfcApduScript() { displayError("No NFC HW.", true); }
void nfcCompareDumps() { displayError("No NFC HW.", true); }
void nfcFieldPoll() { displayError("No NFC HW.", true); }
#endif

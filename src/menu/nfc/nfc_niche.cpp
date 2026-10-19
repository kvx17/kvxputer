/**
 * @file nfc_niche.cpp
 * Amiibo dumps, transit parsers, Picopass/e-ink stubs, ePassport BAC.
 */
#include "nfc_niche.h"
#include "nfc_session.h"
#include "nfc_lab.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>

#if !defined(REMOVE_RFID_HW_INTERFACE)

void nfcAmiiboToolkit() {
#ifndef LITE_VERSION
    int act = -1;
    options = {
        {"Load & view dump", [&]() { act = 0; }},
        {"Write dump",       [&]() { act = 1; }},
        {"Emulate dump",     [&]() { act = 2; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Amiibo") < 0 || act < 0) return;

    NfcSession s;
    if (!s.open(false)) return;
    nfcInfo("Next: pick a dump under amiibo/.\nPress a key to continue.");
    // Use driver load (file picker in load())
    if (s.rfid->load() != RFIDInterface::SUCCESS) {
        nfcRefuse("Load failed.\nPut dumps in\n" + String(kvx::paths::RFID_AMIIBO));
        return;
    }
    if (act == 0) {
        ScrollableTextArea area("Amiibo dump");
        area.addLine("UID: " + s.rfid->printableUID.uid);
        area.addLine("Pages: " + String(s.rfid->dataPages) + "/" + String(s.rfid->totalPages));
        if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages);
        area.addLine("");
        area.addLine("Keygen: supply keys on SD");
        area.show();
        return;
    }
    if (act == 1) {
        nfcInfo("Hold blank NTAG215, then press a key.");
        int rc = s.rfid->write();
        if (rc == RFIDInterface::SUCCESS) displaySuccess("Write OK.", true);
        else nfcRefuse("Write failed / wrong tag.");
        return;
    }
    nfcInfo("Emulation will start after this.\nEsc stops. Press a key.");
    s.rfid->emulate();
#else
    nfcRefuse("Not in lite build.");
#endif
}

void nfcTransitParsers() {
#ifndef LITE_VERSION
    NfcSession s;
    if (!s.open(true)) return;

    ScrollableTextArea area(FP, BORDER_PAD_X, KVX_TOPBAR_H + 2, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - KVX_TOPBAR_H - 4, false, false);
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar("NFC", "Transit");
    }

    String type = s.rfid->printableUID.picc_type;
    String uid = s.rfid->printableUID.uid;
    area.addLine("UID: " + uid);
    area.addLine("Type: " + type);

    // Minimal heuristic parsers — unknown → raw dump.
    bool parsed = false;
    if (type.indexOf("Classic") >= 0 || type.indexOf("MIFARE") >= 0) {
        // Look for common "balance-like" trailing bytes in first sectors text dump.
        String pages = s.rfid->strAllPages;
        if (pages.indexOf("Block 1:") >= 0 || pages.indexOf("Page 4:") >= 0) {
            area.addLine("Parser: generic Classic/UL");
            area.addLine("(no known transit layout)");
            parsed = false;
        }
    }
    if (!parsed) {
        area.addLine("No matching transit parser.");
        area.addLine("--- raw ---");
        if (s.rfid->strAllPages.length()) area.fromString(s.rfid->strAllPages.substring(0, 800));
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
#else
    nfcRefuse("Not in lite build.");
#endif
}

void nfcPicopassStub() {
    ScrollableTextArea area("Picopass");
    area.addLine("iCLASS / Picopass needs");
    area.addLine("a dedicated HF frontend");
    area.addLine("or HID SAM path.");
    area.addLine("");
    area.addLine("Unit NFC (ST25) and PN532");
    area.addLine("do not implement full");
    area.addLine("Picopass here.");
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcEpassport() {
#ifndef LITE_VERSION
    nfcInfo("Own documents only.\nNext: enter MRZ line 2 (44 chars).\nPress a key to continue.");
    String mrz = keyboard("", 44, "MRZ line 2:");
    if (mrz == "\x1B") return;
    mrz.trim();
    if (mrz.length() < 20) {
        nfcRefuse("MRZ too short for BAC.");
        return;
    }

    NfcSession s;
    if (!s.open(true)) return;

    // BAC key derivation + secure messaging is a large stack; run EF.COM SELECT
    // as a probe so the UI is honest about session state.
    uint8_t selEfCom[] = {0x00, 0xA4, 0x02, 0x0C, 0x02, 0x01, 0x1E};
    uint8_t rx[64];
    uint16_t rxLen = 0;
    int rc = s.rfid->transceive_apdu(selEfCom, sizeof(selEfCom), rx, sizeof(rx), &rxLen);
    ScrollableTextArea area("ePassport");
    area.addLine("MRZ len: " + String(mrz.length()));
    area.addLine("BAC: not fully implemented");
    area.addLine("(needs SM session).");
    if (rc == RFIDInterface::SUCCESS && rxLen >= 2) {
        char sw[8];
        sprintf(sw, "%02X%02X", rx[rxLen - 2], rx[rxLen - 1]);
        area.addLine(String("EF.COM SELECT SW=") + sw);
    } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        area.addLine("APDU not on this module.");
    } else {
        area.addLine("ISO-DEP session failed.");
        area.addLine("Hold passport on Unit NFC.");
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
#else
    nfcRefuse("Not in lite build.");
#endif
}

void nfcEinkStub() {
    ScrollableTextArea area("NFC e-ink");
    area.addLine("Not supported on");
    area.addLine("Unit NFC / PN532 in");
    area.addLine("this build.");
    area.addLine("");
    area.addLine("No shelf-tag protocol");
    area.addLine("is vendor-linked here.");
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
}

void nfcParsersMenu() {
    int pick = -1;
    options = {
#ifndef LITE_VERSION
        {"Amiibo toolkit", [&]() { pick = 0; }},
        {"Transit parse",  [&]() { pick = 1; }},
        {"ePassport",      [&]() { pick = 3; }},
#endif
        {"Picopass / iCLASS", [&]() { pick = 2; }},
        {"NFC e-ink",         [&]() { pick = 4; }},
    };
    if (loopOptions(options, MENU_TYPE_SUBMENU, "Parsers") < 0 || pick < 0) return;
    switch (pick) {
#ifndef LITE_VERSION
        case 0: nfcAmiiboToolkit(); break;
        case 1: nfcTransitParsers(); break;
        case 3: nfcEpassport(); break;
#endif
        case 2: nfcPicopassStub(); break;
        case 4: nfcEinkStub(); break;
        default: break;
    }
}

#else
void nfcParsersMenu() { displayError("No NFC HW.", true); }
void nfcAmiiboToolkit() { displayError("No NFC HW.", true); }
void nfcTransitParsers() { displayError("No NFC HW.", true); }
void nfcPicopassStub() { displayError("No NFC HW.", true); }
void nfcEpassport() { displayError("No NFC HW.", true); }
void nfcEinkStub() { displayError("No NFC HW.", true); }
#endif

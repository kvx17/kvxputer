#include "nfc_tools.h"
#include "nfc_dump_gen.h"
#include "menu/others/qrcode_menu.h"
#include "menu/rfid/ndef_parse.h"
#include "menu/rfid/rfid_shared.h"
#include "root/config/configPins.h"
#include "root/hal/pahub.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include <globals.h>
#include <vector>

static bool withLiveTag(RFIDInterface **outRfid, PahubChannelGuard **outGuard, bool needRead) {
    *outGuard = openHfPahubGuard(HfDomain::Nfc);
    *outRfid = createHfModule(HfDomain::Nfc);
    if (!*outRfid || !(*outRfid)->begin()) {
        displayError("NFC module init failed.", true);
        delete *outRfid;
        *outRfid = nullptr;
        delete *outGuard;
        *outGuard = nullptr;
        return false;
    }
    if (needRead) {
        displayInfo("Hold tag near reader,\nthen press any key.", true);
        int result = RFIDInterface::TAG_NOT_PRESENT;
        uint32_t start = millis();
        while (millis() - start < 15000 && !returnToMenu) {
            result = (*outRfid)->read();
            if (result == RFIDInterface::SUCCESS || result == RFIDInterface::NOT_IMPLEMENTED) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (result != RFIDInterface::SUCCESS && result != RFIDInterface::NOT_IMPLEMENTED) {
            displayError("No tag found.", true);
            delete *outRfid;
            *outRfid = nullptr;
            delete *outGuard;
            *outGuard = nullptr;
            return false;
        }
    }
    return true;
}

static void releaseLive(RFIDInterface *rfid, PahubChannelGuard *guard) {
    delete rfid;
    delete guard;
}

void nfcReadRecords() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    RFIDInterface *rfid = nullptr;
    PahubChannelGuard *guard = nullptr;
    if (!withLiveTag(&rfid, &guard, true)) return;

    auto records = ndefParseFromPages(rfid->strAllPages);
    if (records.empty()) {
        displayError("No NDEF records found.", true);
        releaseLive(rfid, guard);
        return;
    }

    while (!returnToMenu) {
        int pick = -1;
        options.clear();
        for (size_t i = 0; i < records.size(); i++) {
            String label = String(i + 1) + ". " + records[i].typeLabel + ": " + records[i].summary;
            if (label.length() > 40) label = label.substring(0, 37) + "...";
            options.emplace_back(label, [&, i]() { pick = (int)i; });
        }
        options.emplace_back("Back", [&]() { pick = -2; });
        if (loopOptions(options, MENU_TYPE_SUBMENU, "NDEF records") < 0 || pick < 0) break;
        if (pick == -2) break;

        int act = -1;
        options = {{"Details", [&]() { act = 0; }}};
        if (records[pick].qrPayload.length()) options.emplace_back("Show QR", [&]() { act = 1; });
        options.emplace_back("Back", [&]() { act = 2; });
        if (loopOptions(options, MENU_TYPE_SUBMENU, "Record") < 0 || act < 0 || act == 2) continue;
        if (act == 0) {
            ScrollableTextArea area(records[pick].typeLabel);
            area.addLine(records[pick].detail);
            area.addLine("");
            area.addLine("Esc = back");
            area.show();
        } else if (act == 1) {
            qrcode_display(records[pick].qrPayload);
        }
    }
    releaseLive(rfid, guard);
#endif
}

void nfcMemoryDump() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    RFIDInterface *rfid = nullptr;
    PahubChannelGuard *guard = nullptr;
    if (!withLiveTag(&rfid, &guard, true)) return;
    ScrollableTextArea area("Memory dump");
    if (rfid->strAllPages.length() == 0) {
        area.addLine("No page data.");
    } else {
        int start = 0;
        String pages = rfid->strAllPages;
        while (start < (int)pages.length()) {
            int nl = pages.indexOf('\n', start);
            if (nl < 0) nl = pages.length();
            String line = pages.substring(start, nl);
            area.addLine(line);
            // ASCII side panel for hex page lines
            int colon = line.indexOf(':');
            if (colon > 0) {
                String hex = line.substring(colon + 1);
                hex.replace(" ", "");
                String ascii;
                for (size_t i = 0; i + 1 < hex.length(); i += 2) {
                    char c = (char)strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16);
                    ascii += (c >= 32 && c < 127) ? c : '.';
                }
                if (ascii.length()) area.addLine("  |" + ascii + "|");
            }
            start = nl + 1;
        }
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
    releaseLive(rfid, guard);
#endif
}

void nfcChipInfo() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    RFIDInterface *rfid = nullptr;
    PahubChannelGuard *guard = nullptr;
    if (!withLiveTag(&rfid, &guard, true)) return;
    ScrollableTextArea area("Chip info");
    const uint16_t valueColor = kvxConfig.secColor;
    const uint8_t labelFont = uiDenseFont();
    const uint8_t valueFont = uiBodyFont();
    auto addField = [&](const String &label, const String &value) {
        area.addLine(label, kvxConfig.priColor, labelFont);
        area.addLine(value, valueColor, valueFont);
    };
    addField("Type:", rfid->printableUID.picc_type);
    addField("UID:", rfid->printableUID.uid);
    addField("ATQA:", rfid->printableUID.atqa);
    addField("SAK:", rfid->printableUID.sak);
    addField("Pages:", String(rfid->dataPages) + "/" + String(rfid->totalPages));
#if !defined(LITE_VERSION)
    // ST25 extras when active
    // dynamic_cast not RTTI-safe on Arduino — use picc_type / ntag fields via printable only
#endif
    if (rfid->pageReadStatus != RFIDInterface::SUCCESS) {
        area.addLine("[!] " + rfid->statusMessage(rfid->pageReadStatus));
    }
    int freeHint = 0;
    if (rfid->totalPages > 4 && rfid->dataPages >= 0) {
        freeHint = (rfid->totalPages - 4) * 4; // rough user bytes
        addField("User bytes:", "~" + String(freeHint));
    }
    area.addLine("");
    area.addLine("Esc = back");
    area.show();
    releaseLive(rfid, guard);
#endif
}

void nfcLockTag() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    options = {
        {"Cancel", []() {}},
        {"LOCK FOREVER",
         []() {
             RFIDInterface *rfid = nullptr;
             PahubChannelGuard *guard = nullptr;
             if (!withLiveTag(&rfid, &guard, true)) return;
             displayInfo("Locking NTAG...", true);
             int rc = rfid->lock_ntag();
             if (rc == RFIDInterface::SUCCESS) displaySuccess("Tag locked permanently.", true);
             else if (rc == RFIDInterface::NOT_IMPLEMENTED)
                 displayError("Lock not supported on this module/tag.", true);
             else if (rc == RFIDInterface::TAG_NOT_MATCH)
                 displayError("Not an NTAG/Ultralight tag.", true);
             else displayError("Lock failed.", true);
             releaseLive(rfid, guard);
         }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "IRREVERSIBLE lock");
#endif
}

void nfcPasswordMenu() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    options = {
        {"Set password",
         []() {
             String p = keyboard("", 8, "PWD 8 hex:");
             if (p == "\x1B") return;
             p.replace(" ", "");
             if (p.length() != 8) {
                 displayError("Need 8 hex digits.", true);
                 return;
             }
             String pk = keyboard("0000", 4, "PACK 4 hex:");
             if (pk == "\x1B") return;
             pk.replace(" ", "");
             if (pk.length() != 4) {
                 displayError("Need 4 hex digits.", true);
                 return;
             }
             uint8_t pwd[4], pack[2];
             for (int i = 0; i < 4; i++)
                 pwd[i] = (uint8_t)strtoul(p.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
             pack[0] = (uint8_t)strtoul(pk.substring(0, 2).c_str(), nullptr, 16);
             pack[1] = (uint8_t)strtoul(pk.substring(2, 4).c_str(), nullptr, 16);
             RFIDInterface *rfid = nullptr;
             PahubChannelGuard *guard = nullptr;
             if (!withLiveTag(&rfid, &guard, true)) return;
             int rc = rfid->set_ntag_password(pwd, pack);
             if (rc == RFIDInterface::SUCCESS) displaySuccess("Password set.", true);
             else if (rc == RFIDInterface::NOT_IMPLEMENTED)
                 displayError("Not supported here.", true);
             else displayError("Set password failed.", true);
             releaseLive(rfid, guard);
         }},
        {"Clear password",
         []() {
             RFIDInterface *rfid = nullptr;
             PahubChannelGuard *guard = nullptr;
             if (!withLiveTag(&rfid, &guard, true)) return;
             int rc = rfid->clear_ntag_password();
             if (rc == RFIDInterface::SUCCESS) displaySuccess("Password cleared.", true);
             else if (rc == RFIDInterface::NOT_IMPLEMENTED)
                 displayError("Not supported here.", true);
             else displayError("Clear failed.", true);
             releaseLive(rfid, guard);
         }},
        {"Back", []() {}},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "NTAG password");
#endif
}

void nfcFormatMemory() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    options = {
        {"Cancel", []() {}},
        {"Format now",
         []() {
             RFIDInterface *rfid = nullptr;
             PahubChannelGuard *guard = nullptr;
             if (!withLiveTag(&rfid, &guard, false)) return;
             displayInfo("Hold tag to format,\nthen press any key.", true);
             int rc = RFIDInterface::TAG_NOT_PRESENT;
             uint32_t start = millis();
             while (millis() - start < 15000 && !returnToMenu) {
                 rc = rfid->format_memory();
                 if (rc != RFIDInterface::TAG_NOT_PRESENT) break;
                 vTaskDelay(pdMS_TO_TICKS(50));
             }
             if (rc == RFIDInterface::SUCCESS) displaySuccess("Memory formatted.", true);
             else if (rc == RFIDInterface::NOT_IMPLEMENTED)
                 displayError("Format not supported.", true);
             else displayError("Format failed.", true);
             releaseLive(rfid, guard);
         }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Zero user memory");
#endif
}

void nfcSendApdu() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
#else
    if (!KvxputerConfigPins::isNfcCapableModule(kvxConfigPins.nfcModule)) {
        displayError("APDU needs PN532 or ST25.", true);
        return;
    }
    String hex = keyboard("", 64, "APDU hex:");
    if (hex == "\x1B") return;
    hex.replace(" ", "");
    if (hex.length() < 2 || (hex.length() % 2)) {
        displayError("Invalid hex.", true);
        return;
    }
    std::vector<uint8_t> tx;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        tx.push_back((uint8_t)strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16));
    }
    RFIDInterface *rfid = nullptr;
    PahubChannelGuard *guard = nullptr;
    if (!withLiveTag(&rfid, &guard, false)) return;
    displayInfo("Hold ISO-DEP tag,\nthen press any key.", true);
    uint8_t rx[256];
    uint16_t rxLen = 0;
    int rc = RFIDInterface::TAG_NOT_PRESENT;
    uint32_t start = millis();
    while (millis() - start < 15000 && !returnToMenu) {
        rc = rfid->transceive_apdu(tx.data(), (uint16_t)tx.size(), rx, sizeof(rx), &rxLen);
        if (rc != RFIDInterface::TAG_NOT_PRESENT) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (rc == RFIDInterface::SUCCESS) {
        String out;
        for (uint16_t i = 0; i < rxLen; i++) {
            if (rx[i] < 0x10) out += "0";
            out += String(rx[i], HEX);
            if (i + 1 < rxLen) out += " ";
        }
        out.toUpperCase();
        ScrollableTextArea area("APDU response");
        area.addLine(out);
        area.addLine("");
        area.addLine("Esc = back");
        area.show();
    } else if (rc == RFIDInterface::NOT_IMPLEMENTED) {
        displayError("APDU not supported.", true);
    } else {
        displayError("APDU failed / no tag.", true);
    }
    releaseLive(rfid, guard);
#endif
}

void nfcSaveAsFile() { nfcSaveAsFileWizard(); }

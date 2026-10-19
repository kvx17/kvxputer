/**
 * @file nfc_dump_gen.cpp
 * Inspired by Next-Flip/Momentum-Apps nfc_maker; reimplemented for kvxputer.
 */
#include "nfc_dump_gen.h"
#include "menu/rfid/ndef_compose.h"
#include "menu/rfid/rfid_shared.h"
#include "root/hal/pahub.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <cstring>
#include <esp_random.h>
#include <globals.h>

struct CardCap {
    const char *name;
    int totalPages; // Type2 page count; 0 = T4T/special
    size_t ndefMax;
    enum Kind { T2, T4, CLASSIC, SLIX } kind;
};

// Capacities aligned with NFC Maker (user NDEF area).
static const CardCap kCards[] = {
    {"NTAG 203 (144B)",  42,  144, CardCap::T2},
    {"NTAG 213 (144B)",  45,  144, CardCap::T2},
    {"NTAG 215 (496B)",  135, 496, CardCap::T2},
    {"NTAG 216 (872B)",  231, 872, CardCap::T2},
    {"NTAG I2C 1K",      231, 872, CardCap::T2},
    {"Type 4 generic",   0,   2048, CardCap::T4},
    {"Classic 1K (file)", 0,  716, CardCap::CLASSIC},
    {"SLIX (file only)", 0,   108, CardCap::SLIX},
};

bool nfcDumpApplyNtag(RFIDInterface *rfid, int totalPages, const uint8_t *uidBytes, uint8_t uidLen) {
    auto tlv = ndefBuildTlvFrom(rfid);
    if (tlv.empty()) return false;
    if (uidLen < 4 || uidLen > 10) return false;

    rfid->uid.size = uidLen;
    memcpy(rfid->uid.uidByte, uidBytes, uidLen);
    rfid->uid.sak = 0x00;
    rfid->uid.atqaByte[0] = 0x44;
    rfid->uid.atqaByte[1] = 0x00;
    rfid->printableUID.picc_type = "NTAG";
    if (totalPages == 45) rfid->printableUID.picc_type = "NTAG213";
    else if (totalPages == 135) rfid->printableUID.picc_type = "NTAG215";
    else if (totalPages == 231) rfid->printableUID.picc_type = "NTAG216";
    rfid->printableUID.sak = "00";
    rfid->printableUID.atqa = "44 00";
    rfid->printableUID.uid = "";
    for (uint8_t i = 0; i < uidLen; i++) {
        if (uidBytes[i] < 0x10) rfid->printableUID.uid += "0";
        rfid->printableUID.uid += String(uidBytes[i], HEX);
        if (i + 1 < uidLen) rfid->printableUID.uid += " ";
    }
    rfid->printableUID.uid.toUpperCase();

    // Build page dump: pages 0-2 UID/lock, page 3 CC, then NDEF
    uint8_t pages[256][4] = {{0}};
    if (totalPages > 256) totalPages = 256;
    // Page 0-1 UID
    pages[0][0] = uidBytes[0];
    pages[0][1] = uidBytes[1];
    pages[0][2] = uidBytes[2];
    pages[0][3] = (uint8_t)(uidBytes[0] ^ uidBytes[1] ^ uidBytes[2]);
    if (uidLen >= 7) {
        pages[1][0] = uidBytes[3];
        pages[1][1] = uidBytes[4];
        pages[1][2] = uidBytes[5];
        pages[1][3] = uidBytes[6];
    }
    pages[2][0] = (uint8_t)(uidBytes[3] ^ uidBytes[4] ^ uidBytes[5] ^ uidBytes[6]);
    // CC
    pages[3][0] = 0xE1;
    pages[3][1] = 0x10;
    pages[3][2] = (uint8_t)((totalPages - 4) * 4 / 8); // size/8
    pages[3][3] = 0x00;
    for (size_t i = 0; i < tlv.size() && (4 + (int)(i / 4)) < totalPages; i++) {
        pages[4 + i / 4][i % 4] = tlv[i];
    }

    rfid->strAllPages = "";
    rfid->totalPages = totalPages;
    rfid->dataPages = 0;
    for (int p = 0; p < totalPages; p++) {
        char line[48];
        sprintf(line, "Page %d: %02X %02X %02X %02X\n", p, pages[p][0], pages[p][1], pages[p][2],
                pages[p][3]);
        rfid->strAllPages += line;
        rfid->dataPages++;
    }
    rfid->pageReadSuccess = true;
    rfid->pageReadStatus = RFIDInterface::SUCCESS;
    return true;
}

static void randomUid(uint8_t *uid, uint8_t len, bool nxp) {
    esp_fill_random(uid, len);
    if (nxp && len >= 1) uid[0] = 0x04;
}

void nfcSaveAsFileWizard() {
#if defined(REMOVE_RFID_HW_INTERFACE)
    displayError("No RFID HW.", true);
    return;
#endif
    // Use a throwaway driver instance as NDEF carrier + save()
    PahubChannelGuard *guard = openHfPahubGuard(HfDomain::Nfc);
    RFIDInterface *rfid = createHfModule(HfDomain::Nfc);
    if (!rfid || !rfid->begin()) {
        displayError("NFC module init failed.", true);
        delete rfid;
        delete guard;
        return;
    }
    if (!ndefComposeInteractive(rfid)) {
        delete rfid;
        delete guard;
        return;
    }
    auto tlv = ndefBuildTlvFrom(rfid);
    size_t need = tlv.size();

    options.clear();
    for (size_t i = 0; i < sizeof(kCards) / sizeof(kCards[0]); i++) {
        bool tooBig = need > kCards[i].ndefMax;
        String label = kCards[i].name;
        if (tooBig) label += " (too large)";
        size_t idx = i;
        options.push_back({label, [idx, tooBig, rfid, need]() {
                               (void)need;
                               if (tooBig) {
                                   displayError("Data too large.", true);
                                   return;
                               }
                               const CardCap &c = kCards[idx];
                               if (c.kind == CardCap::SLIX || c.kind == CardCap::CLASSIC) {
                                   // Archive as Type-2-shaped pages for Load compatibility.
                               }
                               uint8_t uid[7];
                               randomUid(uid, 7, c.kind == CardCap::T2 || c.kind == CardCap::T4);
                               bool custom = false;
                               options = {
                                   {"Random UID", [&]() { custom = false; }},
                                   {"Enter UID",  [&]() { custom = true; } },
                               };
                               loopOptions(options, MENU_TYPE_SUBMENU, "UID");
                               if (custom) {
                                   String u = keyboard("", 14, "UID hex:");
                                   if (u == "\x1B") return;
                                   u.replace(" ", "");
                                   if (u.length() < 8) {
                                       displayError("UID too short.", true);
                                       return;
                                   }
                                   size_t n = u.length() / 2;
                                   if (n > 7) n = 7;
                                   for (size_t i = 0; i < n; i++)
                                       uid[i] = (uint8_t)strtoul(u.substring(i * 2, i * 2 + 2).c_str(),
                                                                 nullptr, 16);
                               }

                               int pages = c.totalPages > 0 ? c.totalPages : 45;
                               if (c.kind == CardCap::T4) {
                                   // Keep raw NDEF; synthetic Type4 metadata
                                   rfid->printableUID.picc_type = "ISO14443-4";
                                   rfid->printableUID.sak = "20";
                                   rfid->printableUID.atqa = "04 00";
                                   rfid->uid.sak = 0x20;
                                   rfid->uid.size = 7;
                                   memcpy(rfid->uid.uidByte, uid, 7);
                                   rfid->printableUID.uid = "";
                                   for (int i = 0; i < 7; i++) {
                                       if (uid[i] < 0x10) rfid->printableUID.uid += "0";
                                       rfid->printableUID.uid += String(uid[i], HEX);
                                       if (i < 6) rfid->printableUID.uid += " ";
                                   }
                                   rfid->printableUID.uid.toUpperCase();
                                   rfid->strAllPages = "NDEF T4T\nSize: " + String(need) + "\n";
                                   rfid->totalPages = 1;
                                   rfid->dataPages = 1;
                               } else if (!nfcDumpApplyNtag(rfid, pages, uid, 7)) {
                                   displayError("Dump build failed.", true);
                                   return;
                               }

                               String name = keyboard("made", 24, "File name:");
                               if (name == "\x1B") return;
                               int rc = rfid->save(name);
                               if (rc == RFIDInterface::SUCCESS) {
                                   rfid->saveFlipper(name); // may be NOT_IMPLEMENTED
                                   displaySuccess("Saved " + name, true);
                               } else {
                                   displayError("Save failed.", true);
                               }
                           }});
    }
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, "Card type");
    delete rfid;
    delete guard;
}

/**
 * @file ndef_compose.h
 * @brief NDEF record composers for Write NDEF / Emulate NDEF.
 *
 * Feature parity inspired by wakdev/nfctools-fz and Next-Flip NFC Maker;
 * reimplemented for kvxputer (no Flipper source).
 */
#ifndef __NDEF_COMPOSE_H__
#define __NDEF_COMPOSE_H__

#include "RFIDInterface.h"
#include <Arduino.h>
#include <vector>

struct NdefComposeResult {
    bool ok = false;
    bool useRaw = false; // true → fill rawNdefRecord; false → ndefMessage
};

// Apply a composed result onto an RFIDInterface instance.
void ndefApplyTo(RFIDInterface *rfid, const NdefComposeResult &meta, const std::vector<uint8_t> &raw,
                 RFIDInterface::NdefMessage *msg);

// Build into rfid's ndefMessage / rawNdefRecord. Returns true if user completed.
bool ndefComposeInteractive(RFIDInterface *rfid);

// Low-level builders (also used by dump gen).
void ndefBuildText(RFIDInterface *rfid, const String &text);
void ndefBuildUri(RFIDInterface *rfid, const String &url);
void ndefBuildWifi(RFIDInterface *rfid, const String &ssid, const String &password, uint8_t auth = 0x20,
                   uint8_t encr = 0x08);
void ndefBuildVcard(RFIDInterface *rfid, const String &first, const String &last, const String &email,
                    const String &phone, const String &url);
void ndefBuildBluetoothOob(RFIDInterface *rfid, const uint8_t mac[6]);
void ndefBuildEmpty(RFIDInterface *rfid);
void ndefBuildCustomRaw(RFIDInterface *rfid, const std::vector<uint8_t> &record);

// Full TLV (0x03 … 0xFE) for Type 2 dumps; empty if nothing composed.
std::vector<uint8_t> ndefBuildTlvFrom(RFIDInterface *rfid);

#endif

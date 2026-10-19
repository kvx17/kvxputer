/**
 * @file ndef_parse.h
 * @brief Parse NDEF records from Type 2 TLV / raw message bytes.
 * Inspired by wakdev/nfctools-fz; reimplemented for kvxputer.
 */
#ifndef __NDEF_PARSE_H__
#define __NDEF_PARSE_H__

#include <Arduino.h>
#include <vector>

struct NdefParsedRecord {
    String typeLabel;
    String summary;
    String detail;
    String qrPayload; // empty if not QR-able
};

// Parse Type 2 page dump lines ("Page N: XX XX XX XX") or a raw TLV/message buffer.
std::vector<NdefParsedRecord> ndefParseFromPages(const String &strAllPages);
std::vector<NdefParsedRecord> ndefParseFromBytes(const uint8_t *data, size_t len);

#endif

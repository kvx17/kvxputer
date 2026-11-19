#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools {

struct NfcDump {
    std::string device_type; // e.g. NTAG215, Mifare Classic, ISO14443-3A
    std::string uid_hex;     // contiguous hex, no spaces
    std::string atqa_hex;    // "XX XX" little-endian Flipper style when known
    std::string sak_hex;     // "XX"
    int pages_total{0};
    int pages_read{0};
    bool page_read_ok{true};
    std::string pages_text; // "Page N: XX XX XX XX\n..."
    std::string ndef_text;  // optional T4T / raw NDEF summary
    std::string raw_extra;  // signature / counters / notes
};

// Parse Bruce .rfid or Flipper .nfc (and kvxputerzero UID dumps).
bool nfc_load_dump_file(const std::string& path, NfcDump& out, std::string& error);

bool nfc_save_bruce(const std::string& path, const NfcDump& dump, std::string& error);
bool nfc_save_flipper(const std::string& path, const NfcDump& dump, std::string& error);

std::string nfc_dump_summary(const NfcDump& d);
std::string nfc_uid_spaced(const std::string& uid_hex);
std::string nfc_normalize_hex(const std::string& s);

// Build a synthetic Type-2 NDEF dump (for Save as file).
NfcDump nfc_synthetic_ntag(int total_pages, const std::string& uid_hex,
                           const std::vector<uint8_t>& ndef_tlv);

} // namespace tools

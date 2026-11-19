#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools {

struct NdefRecord {
    std::string type_label;
    std::string summary;
    std::string detail;
    std::string qr_payload;
};

// Parse Type-2 "Page N: .." dump text into NDEF records.
std::vector<NdefRecord> ndef_parse_from_pages(const std::string& pages_text);

// Build Type-2 TLV (0x03 … 0xFE) for common records.
std::vector<uint8_t> ndef_tlv_uri(const std::string& url);
std::vector<uint8_t> ndef_tlv_text(const std::string& text);
std::vector<uint8_t> ndef_tlv_android_app(const std::string& package);
std::vector<uint8_t> ndef_tlv_wifi(const std::string& ssid, const std::string& password);

// Strip Type-2 NDEF TLV (0x03 … [0xFE]) to the bare NDEF message bytes.
// If input is already a bare message (no 0x03), returns it unchanged.
std::vector<uint8_t> ndef_message_from_tlv(const std::vector<uint8_t>& tlv_or_message);

// T4T NDEF file: 2-byte big-endian length + message, zero-padded to max_len (default 128).
std::vector<uint8_t> ndef_t4t_file_from_tlv(const std::vector<uint8_t>& tlv_or_message,
                                            size_t max_len = 128);

} // namespace tools

#pragma once

#include "nfc_dump.h"
#include "nfc_module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

enum class NfcChip {
    None,
    St25,
    Pn532,
};

// Opens preferred module (auto → ST25 then PN532). Desktop returns false with Cap not present.
struct NfcSession {
    bool open(std::string& error);
    void close();
    bool ok() const;
    NfcChip chip() const { return chip_; }
    const char* chip_name() const;

    bool read_dump(tools::NfcDump& out, std::string& error, bool* cancel = nullptr);
    bool read_uid(std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool erase(std::string& error, bool* cancel = nullptr);
    bool write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr);
    bool lock_ntag(std::string& error, bool* cancel = nullptr);
    bool set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                           bool* cancel = nullptr);
    bool clear_ntag_password(std::string& error, bool* cancel = nullptr);
    bool transceive_apdu(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error,
                         bool* cancel = nullptr);
    bool emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr,
                          int timeout_ms = 30000);
    bool read_felica(tools::NfcDump& out, std::string& error, bool* cancel = nullptr);
    bool iso15693_inventory(std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool magic_probe(std::string& result, std::string& error, bool* cancel = nullptr);
    bool magic_clone_uid(const std::string& uid_hex, std::string& error, bool* cancel = nullptr);

private:
    NfcChip chip_{NfcChip::None};
    void* st_{nullptr};
    void* pn_{nullptr};
};

} // namespace platform

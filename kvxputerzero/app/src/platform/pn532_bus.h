#pragma once

#include "nfc_dump.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct Pn532Session {
    bool open(int i2c_bus, std::string& error);
    void close();
    bool ok() const;

    bool get_firmware(std::string& out, std::string& error);
    // Wait for ISO14443A UID; cancelable.
    bool read_uid(std::string& uid_hex, std::string& error, bool* cancel = nullptr,
                  int timeout_ms = 8000);
    bool save_dump(const std::string& path, const std::string& uid_hex, std::string& error);

    // Full dump: UID + ATQA + SAK + pages when possible.
    bool read_dump(tools::NfcDump& out, std::string& error, bool* cancel = nullptr,
                   int timeout_ms = 10000);
    bool field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool erase_ultralight(std::string& error, bool* cancel = nullptr);
    bool write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr);
    bool lock_ntag(std::string& error, bool* cancel = nullptr);
    bool set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                           bool* cancel = nullptr);
    bool clear_ntag_password(std::string& error, bool* cancel = nullptr);
    bool transceive_apdu(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error,
                         bool* cancel = nullptr);
    // Target-mode NDEF (TgInitAsTarget). Returns false with reason if not supported / failed.
    bool emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr,
                          int timeout_ms = 30000);
    bool read_felica(tools::NfcDump& out, std::string& error, bool* cancel = nullptr,
                     int timeout_ms = 8000);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

#pragma once

#include "nfc_dump.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// Cap-CC1101 onboard NFC (ST25R3916): shared SPI mode 1, soft-CS BCM GPIO22 (Zero Cap-Bus
// NFC_CS; Adv docs list G6). Cap-Bus NFC_IRQ = GPIO23 on Zero. Card Emulation = NFC-A Passive
// Target (no self-field). Detection: platform::detect_st25r3916() in cap_detect.h.

class St25Session {
public:
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool identity(std::string& line, std::string& error);
    // Best-effort ISO14443-A REQA / ATQA. Esc/cancel via cancel flag.
    bool read_uid(std::string& uid_hex, std::string& error, bool* cancel = nullptr);

    bool read_dump(tools::NfcDump& out, std::string& error, bool* cancel = nullptr);
    bool field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool erase_ultralight(std::string& error, bool* cancel = nullptr);
    bool write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr);
    bool lock_ntag(std::string& error, bool* cancel = nullptr);
    bool set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                           bool* cancel = nullptr);
    bool clear_ntag_password(std::string& error, bool* cancel = nullptr);
    bool transceive_apdu(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error,
                         bool* cancel = nullptr);
    bool emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel = nullptr,
                          int timeout_ms = 30000);
    bool iso15693_inventory(std::string& uid_hex, std::string& error, bool* cancel = nullptr);
    bool magic_probe(std::string& result, std::string& error, bool* cancel = nullptr);
    bool magic_clone_uid(const std::string& uid_hex, std::string& error, bool* cancel = nullptr);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

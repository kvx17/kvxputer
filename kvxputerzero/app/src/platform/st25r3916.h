#pragma once

#include <cstdint>
#include <string>

namespace platform {

// Cap-CC1101 onboard NFC (ST25R3916) over shared SPI + GPIO22 soft-CS (SPI mode 1).
// Detection: platform::detect_st25r3916() in cap_detect.h.

class St25Session {
public:
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool identity(std::string& line, std::string& error);
    // Best-effort ISO14443-A REQA / ATQA. Esc/cancel via cancel flag.
    bool read_uid(std::string& uid_hex, std::string& error, bool* cancel = nullptr);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct Nrf24Session {
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool spectrum_rpd(int samples, std::vector<std::string>& lines, std::string& error, bool* cancel = nullptr);
    bool listen_payloads(int seconds, std::vector<std::string>& lines, std::string& error, bool* cancel = nullptr);
    // Payload hex bytes, max 32. Needs CE high for TX — reports error if CE GPIO unset.
    bool send_payload(const uint8_t* data, size_t len, std::string& error);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

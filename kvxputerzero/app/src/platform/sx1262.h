#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct Sx1262Session {
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool read_rssi(int& dbm, std::string& error);
    bool antenna_check(std::vector<std::string>& lines, std::string& error);
    // Best-effort short packet TX/RX. BUSY/NSS GPIO may be required on Cap; errors if SPI-only fails.
    bool send_text(const std::string& text, std::string& error);
    bool receive_text(int timeout_ms, std::string& out, std::string& error, bool* cancel = nullptr);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

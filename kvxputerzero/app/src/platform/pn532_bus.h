#pragma once

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
    bool read_uid(std::string& uid_hex, std::string& error, bool* cancel = nullptr, int timeout_ms = 8000);
    bool save_dump(const std::string& path, const std::string& uid_hex, std::string& error);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform

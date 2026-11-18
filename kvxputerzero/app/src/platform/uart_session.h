#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

class UartSession {
public:
    UartSession() = default;
    ~UartSession();

    UartSession(const UartSession&) = delete;
    UartSession& operator=(const UartSession&) = delete;

    // Opens path at baud. Refuses login consoles (/dev/ttyAMA0 console, pts, ttyS0 often).
    bool open(const std::string& path, int baud, std::string& error);
    void close();
    bool is_open() const;

    bool write_bytes(const uint8_t* data, size_t len, std::string& error);
    // Non-blocking-ish read with short timeout; appends to out.
    bool read_available(std::string& out, std::string& error);

    static std::string default_grove_uart();

private:
    int fd_{-1};
};

} // namespace platform

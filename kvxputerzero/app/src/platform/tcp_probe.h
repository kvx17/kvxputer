#pragma once

#include <cstdint>
#include <string>

namespace platform {

struct TcpProbeResult {
    bool open{false};
    std::string error;
    std::string banner; // up to 512 bytes when grab_banner
};

// Non-blocking connect with timeout_ms. Optionally reads a short banner.
TcpProbeResult tcp_probe(const std::string& host, uint16_t port, int timeout_ms = 1000,
                         bool grab_banner = false, size_t banner_max = 512);

} // namespace platform

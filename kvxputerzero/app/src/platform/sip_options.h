#pragma once

#include <string>

namespace platform {

struct SipOptionsResult {
    bool ok{false};
    std::string error;
    std::string status_line; // first SIP response line, or "no reply"
};

// One UDP SIP OPTIONS to host:5060. timeout_ms caps wait for a reply.
SipOptionsResult sip_options(const std::string& host, int timeout_ms = 2000, bool* cancel = nullptr);

} // namespace platform

#pragma once

#include <string>

namespace platform {

struct HttpResult {
    int status{-1};
    std::string headers;
    std::string body; // capped
    std::string error;
};

HttpResult http_request(const std::string& method, const std::string& url, const std::string& body = {},
                        int timeout_sec = 8, size_t body_cap = 4096);

} // namespace platform

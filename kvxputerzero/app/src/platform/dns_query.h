#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

enum class DnsType : uint16_t {
    A = 1,
    AAAA = 28,
    CNAME = 5,
    TXT = 16,
    MX = 15,
};

struct DnsAnswer {
    std::string text;
};

// Query resolver (empty = system /etc/resolv.conf first nameserver, else typed IP).
bool dns_query(const std::string& name, DnsType type, const std::string& resolver_ip,
               std::vector<DnsAnswer>& answers, std::string& error);

} // namespace platform

#pragma once

#include <string>
#include <vector>

namespace platform {

struct LdapReadResult {
    bool ok{false};
    std::string error;
    std::vector<std::string> lines;
};

// Anonymous or simple bind + subtree search (size limit 20). Drops password attributes.
LdapReadResult ldap_read(const std::string& host, const std::string& base_dn, const std::string& bind_dn,
                         const std::string& password, int timeout_ms = 3000, bool* cancel = nullptr);

} // namespace platform

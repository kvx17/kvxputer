#pragma once

#include <string>

namespace platform {

// Resolve path under root. Returns empty and sets error if the result escapes root.
std::string resolve_under_root(const std::string& root, const std::string& relative, std::string& error);

bool path_is_under_root(const std::string& root, const std::string& absolute);

} // namespace platform

#include "fs_root.h"

#include <filesystem>

namespace platform {

std::string resolve_under_root(const std::string& root, const std::string& relative, std::string& error) {
    error.clear();
    if (root.empty()) {
        error = "empty root";
        return {};
    }
    std::error_code ec;
    const auto root_abs = std::filesystem::weakly_canonical(std::filesystem::path(root), ec);
    if (ec) {
        error = "bad root";
        return {};
    }
    auto joined = root_abs / relative;
    const auto abs = std::filesystem::weakly_canonical(joined, ec);
    if (ec) {
        // File may not exist yet; build manually.
        auto probe = root_abs;
        for (const auto& part : std::filesystem::path(relative)) {
            if (part == "..") {
                error = "path escapes root";
                return {};
            }
            if (part == ".") continue;
            probe /= part;
        }
        if (!path_is_under_root(root_abs.string(), probe.string())) {
            error = "path escapes root";
            return {};
        }
        return probe.string();
    }
    if (!path_is_under_root(root_abs.string(), abs.string())) {
        error = "path escapes root";
        return {};
    }
    return abs.string();
}

bool path_is_under_root(const std::string& root, const std::string& absolute) {
    if (root.empty() || absolute.empty()) return false;
    std::error_code ec;
    const auto r = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto a = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) return false;
    auto rit = r.begin();
    auto ait = a.begin();
    for (; rit != r.end() && ait != a.end(); ++rit, ++ait) {
        if (*rit != *ait) return false;
    }
    return rit == r.end();
}

} // namespace platform

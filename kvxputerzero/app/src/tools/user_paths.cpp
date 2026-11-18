#include "user_paths.h"

#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>

namespace tools {
namespace {

std::string ensure_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#if !defined(_WIN32)
    ::chmod(dir.c_str(), 0700);
#endif
    return dir.string();
}

} // namespace

std::string config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg);
        if (!root.empty() && root.is_absolute()) {
            return ensure_dir(root / "kvxputerzero");
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return ensure_dir(root / ".config" / "kvxputerzero");
        }
    }
    return ensure_dir(std::filesystem::path(".") / "kvxputerzero-config");
}

std::string config_file(const char* name) {
    return (std::filesystem::path(config_dir()) / name).string();
}

} // namespace tools

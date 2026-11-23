#pragma once

#include "runner.h"
#include "user_paths.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace runner {

inline int emit_error(const LineFn& line, const std::string& msg) {
    if (line) line(msg);
    return 1;
}

inline int emit_ok(const LineFn& line, const std::string& msg) {
    if (line) line(msg);
    return 0;
}

inline int emit_lines(const LineFn& line, const std::vector<std::string>& lines) {
    for (const auto& s : lines) {
        if (line) line(s);
    }
    return 0;
}

inline std::string ensure_config_subdir(const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(tools::config_dir()) / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

inline bool cancelled(std::atomic<bool>& cancel) { return cancel.load(); }

} // namespace runner

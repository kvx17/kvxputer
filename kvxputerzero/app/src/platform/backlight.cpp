#include "backlight.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef APP_BACKLIGHT_ROOT
#define APP_BACKLIGHT_ROOT "/sys/class/backlight"
#endif

namespace platform {
namespace {

bool find_backlight_dir(std::filesystem::path& out) {
    const std::filesystem::path root(APP_BACKLIGHT_ROOT);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            return false;
        }
        if (!entry.is_directory(ec)) {
            continue;
        }
        const auto brightness = entry.path() / "brightness";
        const auto max_brightness = entry.path() / "max_brightness";
        if (std::filesystem::exists(brightness, ec) && !ec &&
            std::filesystem::exists(max_brightness, ec) && !ec) {
            out = entry.path();
            return true;
        }
    }
    return false;
}

bool read_int_file(const std::filesystem::path& path, int32_t& value) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    int32_t parsed = 0;
    if (!(file >> parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool write_int_file(const std::filesystem::path& path, int32_t value) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file << value;
    return static_cast<bool>(file);
}

} // namespace

bool read_brightness_percent(int32_t& percent) {
#if USE_DESKTOP
    (void)percent;
    return false;
#else
    std::filesystem::path dir;
    if (!find_backlight_dir(dir)) {
        return false;
    }
    int32_t cur = 0;
    int32_t max_v = 0;
    if (!read_int_file(dir / "brightness", cur) || !read_int_file(dir / "max_brightness", max_v)) {
        return false;
    }
    if (max_v <= 0) {
        return false;
    }
    percent = std::clamp(static_cast<int32_t>((cur * 100 + max_v / 2) / max_v), 0, 100);
    return true;
#endif
}

bool write_brightness_percent(int32_t percent) {
#if USE_DESKTOP
    (void)percent;
    return false;
#else
    percent = std::clamp(percent, 0, 100);
    std::filesystem::path dir;
    if (!find_backlight_dir(dir)) {
        return false;
    }
    int32_t max_v = 0;
    if (!read_int_file(dir / "max_brightness", max_v) || max_v <= 0) {
        return false;
    }
    const int32_t raw = (percent * max_v + 50) / 100;
    return write_int_file(dir / "brightness", std::clamp(raw, 0, max_v));
#endif
}

} // namespace platform

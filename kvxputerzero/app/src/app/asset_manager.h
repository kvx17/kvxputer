/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {

enum class StandardFontWeight {
    Regular,
    Bold,
};

class AssetManager {
public:
    explicit AssetManager(std::string app_name = default_app_name(),
                          std::filesystem::path cmake_assets_root = default_cmake_assets_root());
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    static std::string default_app_name();
    static std::filesystem::path default_cmake_assets_root();

    const std::vector<std::filesystem::path>& roots() const;
    std::filesystem::path resolve(const std::filesystem::path& relative_path) const;
    std::filesystem::path resolve_font(const std::filesystem::path& file_name) const;

    lv_font_t* load_font(const std::filesystem::path& file_name, uint32_t size);
    lv_font_t* load_standard_font(uint32_t size,
                                  StandardFontWeight weight = StandardFontWeight::Bold);

private:
    struct LoadedFont;

    void add_root_if_valid(std::filesystem::path path);

    std::vector<std::filesystem::path> roots_;
    std::vector<std::unique_ptr<LoadedFont>> loaded_fonts_;
};

} // namespace app

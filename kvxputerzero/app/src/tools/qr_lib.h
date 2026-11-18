#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::qr {

struct Matrix {
    int size = 0;
    std::vector<uint8_t> modules; // size*size, 1 = dark
};

struct SavedEntry {
    std::string name;
    std::string content;
};

bool encode_text(const std::string& text, Matrix& out, std::string& error);
std::string build_pix(const std::string& key, const std::string& amount, std::string& error);

bool load_saved(std::vector<SavedEntry>& entries, std::string& error);
bool save_saved(const std::vector<SavedEntry>& entries, std::string& error);
bool add_saved(const std::string& name, const std::string& content, std::string& error);
bool remove_saved(const std::string& name, std::string& error);
std::string storage_path();

} // namespace tools::qr

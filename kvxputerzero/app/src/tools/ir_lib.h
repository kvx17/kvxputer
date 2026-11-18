#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::ir {

struct Entry {
    std::string name;
    uint16_t address{0};
    uint16_t command{0};
};

// ~/.config/kvxputerzero/ir
std::string ir_dir();
bool ensure_dir(std::string& error);

// remotes.txt (lines: name address command; hex ok with 0x prefix)
bool load_remotes(std::vector<Entry>& out, std::string& error);
bool save_remotes(const std::vector<Entry>& entries, std::string& error);
bool append_remote(const Entry& entry, std::string& error);

// One file per remote under ir_dir(); basename without path. Lines same format.
std::vector<std::string> list_remote_files(std::string& error);
bool load_remote_file(const std::string& file_name, std::vector<Entry>& out, std::string& error);
bool save_remote_file(const std::string& file_name, const std::vector<Entry>& entries, std::string& error);

// Flat list: remotes.txt plus every *.txt remote file (name collisions kept as-is).
bool list_all_entries(std::vector<Entry>& out, std::string& error);

} // namespace tools::ir

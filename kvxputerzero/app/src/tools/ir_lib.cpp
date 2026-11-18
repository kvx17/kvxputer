#include "ir_lib.h"

#include "user_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace tools::ir {
namespace {

namespace fs = std::filesystem;

void chmod_private(const std::string& path) {
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#else
    (void)path;
#endif
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool parse_u16(const std::string& token, uint16_t& out) {
    if (token.empty()) return false;
    try {
        size_t idx = 0;
        unsigned long v = 0;
        if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            v = std::stoul(token, &idx, 16);
        } else {
            // Prefer hex when letters are present; otherwise accept decimal.
            bool has_hex_alpha = false;
            for (char c : token) {
                if (std::isxdigit(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) {
                    has_hex_alpha = true;
                    break;
                }
            }
            v = std::stoul(token, &idx, has_hex_alpha ? 16 : 0);
        }
        if (idx != token.size() || v > 0xFFFFul) return false;
        out = static_cast<uint16_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_entry_line(const std::string& line, Entry& out) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') return false;
    std::istringstream iss(t);
    std::string name, a, c;
    if (!(iss >> name >> a >> c)) return false;
    uint16_t addr = 0, cmd = 0;
    if (!parse_u16(a, addr) || !parse_u16(c, cmd)) return false;
    out.name = name;
    out.address = addr;
    out.command = cmd;
    return true;
}

std::string format_entry(const Entry& e) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s 0x%X 0x%X", e.name.c_str(), static_cast<unsigned>(e.address),
                  static_cast<unsigned>(e.command));
    return buf;
}

bool read_entries_file(const fs::path& path, std::vector<Entry>& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        // Missing file is empty, not an error.
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Entry e;
        if (parse_entry_line(line, e)) out.push_back(std::move(e));
    }
    (void)error;
    return true;
}

bool write_entries_file(const fs::path& path, const std::vector<Entry>& entries, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    for (const auto& e : entries) {
        out << format_entry(e) << '\n';
    }
    out.close();
    chmod_private(path.string());
    return true;
}

bool safe_file_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return true;
}

} // namespace

std::string ir_dir() {
    return (fs::path(tools::config_dir()) / "ir").string();
}

bool ensure_dir(std::string& error) {
    error.clear();
    std::error_code ec;
    fs::create_directories(ir_dir(), ec);
    if (ec) {
        error = "cannot create ir dir";
        return false;
    }
#if !defined(_WIN32)
    ::chmod(ir_dir().c_str(), 0700);
#endif
    return true;
}

bool load_remotes(std::vector<Entry>& out, std::string& error) {
    out.clear();
    error.clear();
    if (!ensure_dir(error)) return false;
    return read_entries_file(fs::path(ir_dir()) / "remotes.txt", out, error);
}

bool save_remotes(const std::vector<Entry>& entries, std::string& error) {
    error.clear();
    if (!ensure_dir(error)) return false;
    return write_entries_file(fs::path(ir_dir()) / "remotes.txt", entries, error);
}

bool append_remote(const Entry& entry, std::string& error) {
    error.clear();
    std::vector<Entry> entries;
    if (!load_remotes(entries, error)) return false;
    entries.push_back(entry);
    return save_remotes(entries, error);
}

std::vector<std::string> list_remote_files(std::string& error) {
    error.clear();
    std::vector<std::string> names;
    if (!ensure_dir(error)) return names;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(ir_dir(), ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        const std::string name = de.path().filename().string();
        if (name == "remotes.txt") continue;
        if (de.path().extension() != ".txt" && de.path().extension() != ".ir") continue;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool load_remote_file(const std::string& file_name, std::vector<Entry>& out, std::string& error) {
    out.clear();
    error.clear();
    if (!safe_file_name(file_name)) {
        error = "bad file name";
        return false;
    }
    if (!ensure_dir(error)) return false;
    return read_entries_file(fs::path(ir_dir()) / file_name, out, error);
}

bool save_remote_file(const std::string& file_name, const std::vector<Entry>& entries, std::string& error) {
    error.clear();
    if (!safe_file_name(file_name)) {
        error = "bad file name";
        return false;
    }
    if (!ensure_dir(error)) return false;
    return write_entries_file(fs::path(ir_dir()) / file_name, entries, error);
}

bool list_all_entries(std::vector<Entry>& out, std::string& error) {
    out.clear();
    error.clear();
    if (!load_remotes(out, error)) return false;
    for (const auto& name : list_remote_files(error)) {
        std::vector<Entry> part;
        if (!load_remote_file(name, part, error)) return false;
        out.insert(out.end(), part.begin(), part.end());
    }
    error.clear();
    return true;
}

} // namespace tools::ir

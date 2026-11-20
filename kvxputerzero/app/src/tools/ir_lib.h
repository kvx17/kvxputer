#pragma once

#include <atomic>
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

// User library: a folder of existing Flipper-style .ir files.
// Path is stored in ~/.config/kvxputerzero/ir_library.txt. Empty when unset.
std::string library_dir();
bool set_library_dir(const std::string& path, std::string& error);

struct LibraryEntry {
    std::string name;
    bool directory{false};
};

struct Signal {
    std::string name;
    std::string protocol; // NEC, NECext, Samsung32, SIRC*, or "raw"
    std::string type;     // "parsed" or "raw"
    uint32_t address{0};
    uint32_t command{0};
    uint32_t carrier_hz{38000};
    std::vector<uint32_t> raw_us;
};

// relative is under library_dir(). Empty relative is the library root.
bool list_library(const std::string& relative, std::vector<LibraryEntry>& out, std::string& error);
bool load_library_file(const std::string& relative, std::vector<Signal>& out, std::string& error);

struct LibraryHit {
    std::string relative;
    std::string name;
};

// Case-insensitive match on the .ir file name and its path inside the library.
// cancel stops the walk; scanned counts entries visited so the UI can show progress.
bool search_library(const std::string& query, std::vector<LibraryHit>& out, std::string& error,
                    const std::atomic<bool>* cancel = nullptr, std::atomic<int>* scanned = nullptr);

// ~/.config/kvxputerzero/ir_favorites.json — copy this file to share favorites.
std::string favorites_file();

struct Favorite {
    std::string relative;
    std::string name;
};

bool load_favorites(std::vector<Favorite>& out, std::string& error);
bool is_favorite(const std::string& relative);
// Adds the remote when missing, removes it when present. now_favorite is the result.
bool toggle_favorite(const std::string& relative, const std::string& name, bool& now_favorite, std::string& error);

// Parse a Flipper .ir file, or legacy "name address command" lines.
bool load_signals_file(const std::string& path, std::vector<Signal>& out, std::string& error);

// Build a mark/space buffer (microseconds) for send_raw. Carrier is written to carrier_hz.
bool encode_signal(const Signal& signal, std::vector<uint32_t>& pulses_us, uint32_t& carrier_hz,
                   std::string& error);

} // namespace tools::ir

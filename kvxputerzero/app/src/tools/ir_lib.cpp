#include "ir_lib.h"

#include "user_paths.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

std::string lower_copy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string expand_user_path(std::string path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return path;
    if (path.size() == 1) return home;
    if (path[1] == '/') return std::string(home) + path.substr(1);
    return path;
}

bool parse_le_bytes(const std::string& text, uint32_t& out) {
    std::istringstream iss(text);
    std::string tok;
    uint32_t value = 0;
    int i = 0;
    bool any = false;
    while (iss >> tok && i < 4) {
        try {
            size_t idx = 0;
            const unsigned long b = std::stoul(tok, &idx, 16);
            if (idx != tok.size() || b > 0xFFul) return false;
            value |= static_cast<uint32_t>(b) << (8 * i);
            ++i;
            any = true;
        } catch (...) {
            return false;
        }
    }
    if (!any) return false;
    out = value;
    return true;
}

bool parse_u32_list(const std::string& text, std::vector<uint32_t>& out) {
    std::istringstream iss(text);
    std::string tok;
    bool any = false;
    while (iss >> tok) {
        try {
            size_t idx = 0;
            const unsigned long v = std::stoul(tok, &idx, 10);
            if (idx != tok.size() || v > 500000ul) return false;
            out.push_back(static_cast<uint32_t>(v));
            any = true;
        } catch (...) {
            return false;
        }
    }
    return any;
}

std::string field_value(const std::string& line) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) return {};
    return trim(line.substr(colon + 1));
}

bool looks_flipper(const std::string& line) {
    const std::string t = lower_copy(trim(line));
    return t.rfind("filetype:", 0) == 0 || t.rfind("name:", 0) == 0 || t.rfind("protocol:", 0) == 0 ||
           t.rfind("type:", 0) == 0 || t.rfind("data:", 0) == 0 || t.rfind("address:", 0) == 0 ||
           t.rfind("command:", 0) == 0;
}

void push_bits(std::vector<uint32_t>& pulses, uint32_t value, int nbits, uint32_t mark, uint32_t zero_space,
               uint32_t one_space) {
    for (int i = 0; i < nbits; ++i) {
        pulses.push_back(mark);
        pulses.push_back((value & 1u) ? one_space : zero_space);
        value >>= 1;
    }
}

bool resolve_library_path(const std::string& relative, std::string& out, std::string& error) {
    const std::string root = library_dir();
    if (root.empty()) {
        error = "library not set";
        return false;
    }
    std::error_code ec;
    const fs::path root_abs = fs::weakly_canonical(fs::path(root), ec);
    if (ec || root_abs.empty()) {
        error = "library folder is missing";
        return false;
    }
    const fs::path rel(relative);
    if (rel.is_absolute()) {
        error = "bad path";
        return false;
    }
    for (const auto& part : rel) {
        if (part == "..") {
            error = "path escapes library";
            return false;
        }
    }
    const fs::path joined = root_abs / rel;
    const fs::path canon = fs::weakly_canonical(joined, ec);
    const fs::path check = ec ? joined.lexically_normal() : canon;
    const std::string root_s = root_abs.lexically_normal().string();
    const std::string check_s = check.lexically_normal().string();
    if (check_s != root_s && check_s.rfind(root_s + "/", 0) != 0) {
        error = "path escapes library";
        return false;
    }
    out = check_s;
    error.clear();
    return true;
}

void push_sony_bits(std::vector<uint32_t>& pulses, uint32_t value, int nbits) {
    for (int i = 0; i < nbits; ++i) {
        pulses.push_back((value & 1u) ? 1200u : 600u);
        pulses.push_back(600u);
        value >>= 1;
    }
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

std::string library_dir() {
    std::ifstream in(tools::config_file("ir_library.txt"));
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return trim(line);
}

bool set_library_dir(const std::string& path, std::string& error) {
    error.clear();
    const std::string trimmed = trim(path);
    const fs::path cfg = tools::config_file("ir_library.txt");
    if (trimmed.empty()) {
        std::error_code ec;
        fs::remove(cfg, ec);
        return true;
    }
    std::string expanded = expand_user_path(trimmed);
    if (!expanded.empty() && expanded[0] != '/') {
        const char* home = std::getenv("HOME");
        if (home && *home) expanded = (fs::path(home) / expanded).string();
    }
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(expanded), ec);
    if (ec || canon.empty()) {
        error = "folder not found";
        return false;
    }
    if (!fs::is_directory(canon, ec)) {
        error = "not a folder";
        return false;
    }
    std::ofstream out(cfg, std::ios::trunc);
    if (!out) {
        error = "cannot save library path";
        return false;
    }
    out << canon.string() << '\n';
    out.close();
    chmod_private(cfg.string());
    return true;
}

bool list_library(const std::string& relative, std::vector<LibraryEntry>& out, std::string& error) {
    out.clear();
    error.clear();
    std::string dir;
    if (!resolve_library_path(relative, dir, error)) return false;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = "library folder is missing";
        return false;
    }
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = de.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code ec2;
        LibraryEntry item;
        item.name = name;
        item.directory = de.is_directory(ec2);
        if (!item.directory) {
            const std::string ext = lower_copy(de.path().extension().string());
            if (ext != ".ir") continue;
        }
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return lower_copy(a.name) < lower_copy(b.name);
    });
    return true;
}

bool load_signals_file(const std::string& path, std::vector<Signal>& out, std::string& error) {
    out.clear();
    error.clear();
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::vector<std::string> lines;
    std::string line;
    bool flipper = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!flipper && looks_flipper(line)) flipper = true;
        lines.push_back(line);
    }
    if (!flipper) {
        for (const auto& raw : lines) {
            Entry e;
            if (!parse_entry_line(raw, e)) continue;
            Signal s;
            s.name = e.name;
            s.protocol = "NEC";
            s.type = "parsed";
            s.address = e.address;
            s.command = e.command;
            s.carrier_hz = 38000;
            out.push_back(std::move(s));
        }
        return true;
    }

    Signal cur;
    bool have = false;
    auto flush = [&]() {
        if (!have || trim(cur.name).empty()) return;
        if (cur.protocol.empty() && !cur.raw_us.empty()) cur.protocol = "raw";
        if (cur.type.empty()) cur.type = cur.raw_us.empty() ? "parsed" : "raw";
        if (cur.carrier_hz == 0) cur.carrier_hz = 38000;
        out.push_back(std::move(cur));
        cur = Signal{};
        have = false;
    };
    for (const auto& raw : lines) {
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#') continue;
        const std::string key = lower_copy(t.substr(0, t.find(':')));
        const std::string val = field_value(t);
        if (key == "name") {
            flush();
            cur.name = val;
            have = true;
            continue;
        }
        if (!have) continue;
        if (key == "type") cur.type = lower_copy(val);
        else if (key == "protocol") cur.protocol = val;
        else if (key == "address") {
            uint32_t v = 0;
            if (!parse_le_bytes(val, v)) {
                error = "bad address in " + cur.name;
                return false;
            }
            cur.address = v;
        } else if (key == "command") {
            uint32_t v = 0;
            if (!parse_le_bytes(val, v)) {
                error = "bad command in " + cur.name;
                return false;
            }
            cur.command = v;
        } else if (key == "frequency") {
            try {
                cur.carrier_hz = static_cast<uint32_t>(std::stoul(val));
            } catch (...) {
                cur.carrier_hz = 38000;
            }
        } else if (key == "data") {
            if (!parse_u32_list(val, cur.raw_us)) {
                error = "bad raw data in " + cur.name;
                return false;
            }
        }
    }
    flush();
    return true;
}

bool load_library_file(const std::string& relative, std::vector<Signal>& out, std::string& error) {
    out.clear();
    error.clear();
    std::string path;
    if (!resolve_library_path(relative, path, error)) return false;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "not a file";
        return false;
    }
    return load_signals_file(path, out, error);
}

bool encode_signal(const Signal& signal, std::vector<uint32_t>& pulses_us, uint32_t& carrier_hz, std::string& error) {
    pulses_us.clear();
    error.clear();
    const std::string proto = lower_copy(signal.protocol);
    const std::string type = lower_copy(signal.type);
    const bool raw = type == "raw" || proto == "raw" || !signal.raw_us.empty();
    if (raw && !signal.raw_us.empty()) {
        if (signal.raw_us.size() > 4096) {
            error = "signal too long";
            return false;
        }
        pulses_us = signal.raw_us;
        carrier_hz = signal.carrier_hz ? signal.carrier_hz : 38000;
        if (carrier_hz < 10000) carrier_hz = 10000;
        if (carrier_hz > 60000) carrier_hz = 60000;
        return true;
    }

    auto nec_frame = [&](uint32_t word, uint32_t header_mark, uint32_t header_space, uint32_t bit_mark,
                         uint32_t zero_space, uint32_t one_space, int nbits) {
        pulses_us.push_back(header_mark);
        pulses_us.push_back(header_space);
        push_bits(pulses_us, word, nbits, bit_mark, zero_space, one_space);
        pulses_us.push_back(bit_mark);
    };

    if (proto == "nec") {
        const uint32_t addr = signal.address & 0xFFu;
        const uint32_t cmd = signal.command & 0xFFu;
        const uint32_t word = addr | ((~addr & 0xFFu) << 8) | (cmd << 16) | ((~cmd & 0xFFu) << 24);
        nec_frame(word, 9000, 4500, 560, 560, 1690, 32);
        carrier_hz = 38000;
        return true;
    }
    if (proto == "necext") {
        const uint32_t word = (signal.address & 0xFFFFu) | ((signal.command & 0xFFFFu) << 16);
        nec_frame(word, 9000, 4500, 560, 560, 1690, 32);
        carrier_hz = 38000;
        return true;
    }
    if (proto == "samsung32") {
        const uint32_t addr = signal.address & 0xFFu;
        const uint32_t cmd = signal.command & 0xFFu;
        const uint32_t word = addr | (addr << 8) | (cmd << 16) | ((~cmd & 0xFFu) << 24);
        nec_frame(word, 4500, 4500, 550, 550, 1650, 32);
        carrier_hz = 38000;
        return true;
    }
    if (proto == "sirc" || proto == "sirc15" || proto == "sirc20") {
        const int addr_bits = proto == "sirc20" ? 13 : proto == "sirc15" ? 8 : 5;
        pulses_us.push_back(2400);
        pulses_us.push_back(600);
        push_sony_bits(pulses_us, signal.command, 7);
        push_sony_bits(pulses_us, signal.address, addr_bits);
        carrier_hz = 40000;
        return true;
    }
    error = signal.protocol.empty() ? "unsupported signal" : ("unsupported protocol " + signal.protocol);
    return false;
}

namespace {

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c == '\n') {
            out += "\\n";
        } else if (c >= 0x20) {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

bool read_json_string(const std::string& text, size_t quote, std::string& out, size_t& end) {
    out.clear();
    if (quote >= text.size() || text[quote] != '"') return false;
    for (size_t i = quote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
            continue;
        }
        if (c == '"') {
            end = i + 1;
            return true;
        }
        out.push_back(c);
    }
    return false;
}

bool favorite_path_ok(const std::string& relative) {
    if (relative.empty() || relative[0] == '/') return false;
    const fs::path rel(relative);
    if (rel.is_absolute()) return false;
    for (const auto& part : rel) {
        if (part == "..") return false;
    }
    return true;
}

} // namespace

bool search_library(const std::string& query, std::vector<LibraryHit>& out, std::string& error,
                    const std::atomic<bool>* cancel, std::atomic<int>* scanned) {
    out.clear();
    error.clear();
    const std::string needle = lower_copy(trim(query));
    if (needle.empty()) {
        error = "type a name";
        return false;
    }
    const std::string root = library_dir();
    if (root.empty()) {
        error = "library not set";
        return false;
    }
    std::error_code ec;
    const fs::path root_path = fs::weakly_canonical(fs::path(root), ec);
    if (ec || !fs::is_directory(root_path, ec)) {
        error = "library folder is missing";
        return false;
    }
    fs::recursive_directory_iterator it(root_path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            out.clear();
            error = "cancelled";
            return false;
        }
        if (scanned) scanned->fetch_add(1, std::memory_order_relaxed);
        if (ec) {
            ec.clear();
            continue;
        }
        if (it.depth() > 8) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (lower_copy(it->path().extension().string()) != ".ir") continue;
        std::error_code ec3;
        std::string rel = fs::relative(it->path(), root_path, ec3).generic_string();
        if (ec3 || !favorite_path_ok(rel)) continue;
        if (lower_copy(name).find(needle) == std::string::npos && lower_copy(rel).find(needle) == std::string::npos) {
            continue;
        }
        out.push_back(LibraryHit{rel, name});
        if (out.size() >= 80) break;
    }
    std::sort(out.begin(), out.end(), [](const LibraryHit& a, const LibraryHit& b) {
        return lower_copy(a.relative) < lower_copy(b.relative);
    });
    return true;
}

std::string favorites_file() {
    return tools::config_file("ir_favorites.json");
}

bool load_favorites(std::vector<Favorite>& out, std::string& error) {
    out.clear();
    error.clear();
    std::ifstream in(favorites_file());
    if (!in) return true;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    size_t pos = 0;
    while (pos < text.size()) {
        const auto key = text.find("\"path\"", pos);
        if (key == std::string::npos) break;
        const auto colon = text.find(':', key + 6);
        if (colon == std::string::npos) break;
        const auto quote = text.find('"', colon + 1);
        if (quote == std::string::npos) break;
        Favorite fav;
        size_t end = quote;
        if (!read_json_string(text, quote, fav.relative, end)) break;
        pos = end;
        if (!favorite_path_ok(fav.relative)) continue;
        const auto next = text.find("\"path\"", end);
        const auto name_key = text.find("\"name\"", end);
        if (name_key != std::string::npos && (next == std::string::npos || name_key < next)) {
            const auto name_colon = text.find(':', name_key + 6);
            const auto name_quote = name_colon == std::string::npos ? std::string::npos : text.find('"', name_colon + 1);
            size_t name_end = 0;
            if (name_quote != std::string::npos && read_json_string(text, name_quote, fav.name, name_end)) {
                pos = name_end;
            }
        }
        if (fav.name.empty()) fav.name = fs::path(fav.relative).filename().string();
        bool dup = false;
        for (const auto& have : out) {
            if (have.relative == fav.relative) dup = true;
        }
        if (!dup) out.push_back(std::move(fav));
    }
    return true;
}

bool is_favorite(const std::string& relative) {
    std::vector<Favorite> favs;
    std::string err;
    if (!load_favorites(favs, err)) return false;
    for (const auto& fav : favs) {
        if (fav.relative == relative) return true;
    }
    return false;
}

bool toggle_favorite(const std::string& relative, const std::string& name, bool& now_favorite, std::string& error) {
    now_favorite = false;
    error.clear();
    if (!favorite_path_ok(relative)) {
        error = "bad remote path";
        return false;
    }
    std::vector<Favorite> favs;
    if (!load_favorites(favs, error)) return false;
    bool removed = false;
    std::vector<Favorite> next;
    for (const auto& fav : favs) {
        if (fav.relative == relative) {
            removed = true;
            continue;
        }
        next.push_back(fav);
    }
    if (!removed) {
        Favorite fav;
        fav.relative = relative;
        fav.name = name.empty() ? fs::path(relative).filename().string() : name;
        next.push_back(std::move(fav));
        now_favorite = true;
    }
    const fs::path cfg = favorites_file();
    std::ofstream out(cfg, std::ios::trunc);
    if (!out) {
        error = "cannot write " + cfg.string();
        return false;
    }
    out << "{\n  \"favorites\": [\n";
    for (size_t i = 0; i < next.size(); ++i) {
        out << "    {\"path\": \"" << json_escape(next[i].relative) << "\", \"name\": \"" << json_escape(next[i].name)
            << "\"}";
        if (i + 1 < next.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
    out.close();
    if (!out) {
        error = "cannot write favorites";
        return false;
    }
    chmod_private(cfg.string());
    return true;
}

} // namespace tools::ir

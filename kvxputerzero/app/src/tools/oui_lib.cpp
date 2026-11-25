#include "oui_lib.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#ifndef APP_SOURCE_ASSETS_ROOT
#define APP_SOURCE_ASSETS_ROOT "assets"
#endif

#ifndef OUI_TABLE_BUILD_PATH
#define OUI_TABLE_BUILD_PATH ""
#endif

namespace tools::oui {
namespace {

constexpr uint32_t kMagic = 0x3149554Fu; // 'OUI1' little-endian

#pragma pack(push, 1)
struct FileHeader {
    char magic[4];
    uint16_t version;
    uint16_t reserved;
    uint32_t string_pool_size;
    uint32_t section_count;
};

struct SectionHeader {
    uint16_t bit_length;
    uint16_t reserved;
    uint32_t entry_count;
};

struct Entry {
    uint64_t prefix;
    uint32_t vendor_off;
    uint8_t flags;
    uint8_t block_type;
    uint16_t pad;
};
#pragma pack(pop)

struct Section {
    int bit_length{0};
    std::vector<Entry> entries;
};

struct Table {
    bool loaded{false};
    std::vector<Section> sections;
    std::vector<char> pool;
};

Table g_table;
std::string g_override;
std::mutex g_mu;

const char* vendor_at(const Table& t, uint32_t off) {
    if (off >= t.pool.size()) return "";
    return t.pool.data() + off;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto n = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    out.resize(n);
    if (n && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n))) {
        return false;
    }
    return true;
}

bool parse_blob(const std::vector<uint8_t>& blob, Table& t) {
    if (blob.size() < sizeof(FileHeader)) return false;
    FileHeader hdr{};
    std::memcpy(&hdr, blob.data(), sizeof(hdr));
    if (std::memcmp(hdr.magic, "OUI1", 4) != 0 || hdr.version != 1) return false;
    if (hdr.section_count == 0 || hdr.section_count > 8) return false;

    size_t off = sizeof(FileHeader);
    std::vector<SectionHeader> sh(hdr.section_count);
    for (uint32_t i = 0; i < hdr.section_count; ++i) {
        if (off + sizeof(SectionHeader) > blob.size()) return false;
        std::memcpy(&sh[i], blob.data() + off, sizeof(SectionHeader));
        off += sizeof(SectionHeader);
    }

    t.sections.clear();
    t.sections.reserve(hdr.section_count);
    for (uint32_t i = 0; i < hdr.section_count; ++i) {
        Section sec;
        sec.bit_length = static_cast<int>(sh[i].bit_length);
        sec.entries.resize(sh[i].entry_count);
        const size_t bytes = static_cast<size_t>(sh[i].entry_count) * sizeof(Entry);
        if (off + bytes > blob.size()) return false;
        if (bytes) {
            std::memcpy(sec.entries.data(), blob.data() + off, bytes);
        }
        off += bytes;
        t.sections.push_back(std::move(sec));
    }

    if (off + hdr.string_pool_size > blob.size()) return false;
    t.pool.assign(reinterpret_cast<const char*>(blob.data() + off),
                  reinterpret_cast<const char*>(blob.data() + off + hdr.string_pool_size));
    if (t.pool.empty() || t.pool.front() != '\0') return false;
    t.loaded = true;
    return true;
}

std::vector<std::string> candidate_paths() {
    std::vector<std::string> paths;
    if (!g_override.empty()) paths.push_back(g_override);
    if (OUI_TABLE_BUILD_PATH[0]) paths.emplace_back(OUI_TABLE_BUILD_PATH);
    paths.emplace_back(std::string(APP_SOURCE_ASSETS_ROOT) + "/files/oui_table.bin");
    paths.emplace_back("assets/files/oui_table.bin");
    paths.emplace_back("/usr/share/kvxputerzero/files/oui_table.bin");
    return paths;
}

bool load_locked() {
    if (g_table.loaded) return true;
    std::vector<uint8_t> blob;
    for (const auto& path : candidate_paths()) {
        if (!read_file(path, blob)) continue;
        Table next;
        if (parse_blob(blob, next)) {
            g_table = std::move(next);
            return true;
        }
    }
    return false;
}

std::string only_hex(const std::string& in) {
    std::string hex;
    hex.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isxdigit(c)) hex.push_back(static_cast<char>(std::toupper(c)));
    }
    return hex;
}

const Entry* find_in_section(const Section& sec, uint64_t prefix) {
    const auto it = std::lower_bound(
        sec.entries.begin(), sec.entries.end(), prefix,
        [](const Entry& e, uint64_t v) { return e.prefix < v; });
    if (it == sec.entries.end() || it->prefix != prefix) return nullptr;
    return &*it;
}

Match match_entry(const Entry& e, int bit_length, const Table& t) {
    Match m;
    m.found = true;
    m.prefix = e.prefix;
    m.bit_length = bit_length;
    m.vendor = vendor_at(t, e.vendor_off);
    m.is_private = (e.flags & 1u) != 0 || m.vendor.empty();
    m.block_type = block_type_name(e.block_type);
    return m;
}

} // namespace

const char* block_type_name(uint8_t code) {
    switch (code) {
    case 1: return "MA-L";
    case 2: return "MA-M";
    case 3: return "MA-S";
    case 4: return "IAB";
    case 5: return "CID";
    default: return "";
    }
}

void set_table_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_override = path;
    g_table = Table{};
}

bool ensure_loaded() {
    std::lock_guard<std::mutex> lock(g_mu);
    return load_locked();
}

Match lookup(const std::string& mac_or_oui) {
    Match miss;
    const std::string hex = only_hex(mac_or_oui);
    if (hex.empty()) return miss;

    uint64_t mac48 = 0;
    const size_t take = std::min<size_t>(hex.size(), 12);
    for (size_t i = 0; i < take; ++i) {
        const char c = hex[i];
        const int n = (c >= 'A') ? (c - 'A' + 10) : (c - '0');
        mac48 = (mac48 << 4) | static_cast<uint64_t>(n);
    }
    // Left-align into 48 bits when the input is shorter than 12 hex digits.
    if (take < 12) mac48 <<= (12 - take) * 4;

    std::lock_guard<std::mutex> lock(g_mu);
    if (!load_locked()) return miss;

    // Probe longest first when we have a full MAC; OUI-only stays at 24/12.
    const int order_full[] = {36, 28, 24, 12};
    const int order_oui[] = {24, 12};
    const int* order = (hex.size() >= 12) ? order_full : order_oui;
    const int n_order = (hex.size() >= 12) ? 4 : 2;

    for (int i = 0; i < n_order; ++i) {
        const int bits = order[i];
        for (const auto& sec : g_table.sections) {
            if (sec.bit_length != bits) continue;
            const uint64_t shift = static_cast<uint64_t>(48 - bits);
            const uint64_t pref = mac48 >> shift;
            if (const Entry* e = find_in_section(sec, pref)) {
                return match_entry(*e, bits, g_table);
            }
        }
    }
    return miss;
}

} // namespace tools::oui

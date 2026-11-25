#pragma once

#include <cstdint>
#include <string>

namespace tools::oui {

struct Match {
    bool found{false};
    uint64_t prefix{0};
    int bit_length{0};
    std::string vendor;
    bool is_private{false};
    const char* block_type{""}; // MA-L / MA-M / MA-S / IAB / CID / ""
};

// Load the compiled table once. Searches build path, AssetManager-style roots,
// then /usr/share/kvxputerzero/files/oui_table.bin. Returns false if missing.
bool ensure_loaded();

// Override search path (tests / build tree). Empty clears the override.
void set_table_path(const std::string& path);

// Longest-prefix match against hex digits (colons optional).
// A 6-digit OUI only searches 24-bit and shorter. A full 12-digit MAC
// probes 36 → 28 → 24 → 12.
Match lookup(const std::string& mac_or_oui);

const char* block_type_name(uint8_t code);

} // namespace tools::oui

#pragma once

#include "cc1101.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

enum class RfRegion {
    Eu = 0,
    Us = 1,
};

enum class RfModHint {
    Ook = 0,
    Fsk = 1,
};

struct RfSettings {
    uint32_t freq_hz{433920000};
    uint32_t step_hz{25000};
    int ref_level_dbm{-20};
    int floor_dbm{-100};
    int threshold_dbm{-60};
    RfRegion region{RfRegion::Eu};
    RfModHint modulation{RfModHint::Ook};
    Cc1101PaLevel pa{Cc1101PaLevel::Low};
    int tx_duration_ms{500};
    int record_max_ms{5000};
    std::vector<uint32_t> recents;
    std::vector<uint32_t> favourites;
};

RfSettings load_rf_settings();
void save_rf_settings(const RfSettings& s);

void rf_push_recent(RfSettings& s, uint32_t hz);
bool rf_tx_allowed(RfRegion region, uint32_t hz, std::string& reason);
Cc1101PaLevel rf_clamp_pa(RfRegion region, Cc1101PaLevel want);

std::string rf_dir();
std::string rf_format_mhz(uint32_t hz);
std::string rf_auto_name(uint32_t hz, const char* mod_tag);

} // namespace platform

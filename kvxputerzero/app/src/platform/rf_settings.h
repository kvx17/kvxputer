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
    Cc1101Preset preset{Cc1101Preset::OokAsync};
    Cc1101PaLevel pa{Cc1101PaLevel::Low};
    int tx_duration_ms{500};
    int record_max_ms{5000};
    float rx_bw_khz{270.f};
    int rssi_cal_db{0};
    bool allow_sub_http_import{false};
    std::vector<uint32_t> recents;
    std::vector<uint32_t> favourites;
    std::vector<std::string> favourite_paths; // Replay slots 1–3
};

RfSettings load_rf_settings();
void save_rf_settings(const RfSettings& s);

void rf_push_recent(RfSettings& s, uint32_t hz);
bool rf_tx_allowed(RfRegion region, uint32_t hz, std::string& reason);
Cc1101PaLevel rf_clamp_pa(RfRegion region, Cc1101PaLevel want);

// EU 868.0–868.6: ≤1% duty (600 ms on-air / 60 s). Returns false if blocked.
bool rf_duty_reserve(uint32_t hz, int on_air_ms, std::string& reason);
int rf_duty_remaining_ms(uint32_t hz);

std::string rf_dir();
std::string rf_format_mhz(uint32_t hz);
std::string rf_auto_name(uint32_t hz, const char* mod_tag);
const char* rf_flipper_preset_name(Cc1101Preset preset);
Cc1101Preset rf_preset_from_name(const std::string& name);

} // namespace platform

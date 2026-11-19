#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace platform {

enum class Cc1101Preset {
    SpectrumWide, // wide RX BW, OOK-ish for fast hops
    OokAsync,     // OOK async, GDO0 = serial data
    Fsk2,         // 2-FSK packet-ish
};

enum class Cc1101PaLevel {
    Min = 0, // ~-30 dBm class
    Low = 1, // ~0 dBm class
    Mid = 2, // ~+5 dBm class
    Max = 3, // ~+10 dBm class (still capped by region)
};

struct Cc1101Band {
    uint32_t start_hz;
    uint32_t stop_hz;
};

// Chip synthesizer bands (26 MHz crystal).
inline constexpr Cc1101Band kCc1101Bands[] = {
    {300000000u, 348000000u},
    {387000000u, 464000000u},
    {779000000u, 928000000u},
};
inline constexpr size_t kCc1101BandCount = sizeof(kCc1101Bands) / sizeof(kCc1101Bands[0]);

inline constexpr uint32_t kCc1101TuneQuantumHz = 397; // ≈ 26e6 / 2^16

bool cc1101_freq_supported(uint32_t hz);
// Returns which band contains hz, or -1.
int cc1101_band_index(uint32_t hz);
// Gaps inside [start,stop] that the chip cannot tune (for hatching).
std::vector<std::pair<uint32_t, uint32_t>> cc1101_unsupported_gaps(uint32_t start_hz, uint32_t stop_hz);
// Clamp start/stop into supported bands; widen step so bin_count stays near target_bins.
bool cc1101_plan_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t& step_hz, uint32_t target_bins,
                       std::vector<uint32_t>& freqs_out, std::string& error);

struct Cc1101Session {
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool soft_reset(std::string& error);
    bool apply_preset(Cc1101Preset preset, std::string& error);
    bool set_pa_level(Cc1101PaLevel level, std::string& error);
    bool set_rx_bw_khz(float khz, std::string& error);

    // Sets RF path + FREQ + SCAL. Leaves chip in IDLE unless enter_rx.
    bool set_frequency_hz(uint32_t hz, std::string& error, bool enter_rx = false);
    bool enter_rx(std::string& error);
    bool enter_idle(std::string& error);

    // Stay-in-RX RSSI (no per-sample SIDLE). Call enter_rx / set_frequency first.
    bool read_rssi_dbm(int& dbm, std::string& error);

    // Hop across band; append "freq_hz,rssi_dbm" lines. cancel may stop early.
    bool spectrum_scan(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, std::vector<std::string>& lines,
                       std::string& error, std::atomic<bool>* cancel = nullptr);
    // Fill rssi_out[i] for each freqs[i]. Keeps RX between hops when possible.
    bool spectrum_sweep(const std::vector<uint32_t>& freqs, std::vector<int>& rssi_out, std::string& error,
                        std::atomic<bool>* cancel = nullptr);

    // Stay in RX briefly and collect RSSI samples (for listen / record).
    bool listen_rssi(int samples, int delay_ms, std::vector<int>& out, std::string& error,
                     std::atomic<bool>* cancel = nullptr);

    // Unmodulated carrier. Hard-stops into IDLE after duration_ms or when cancel trips.
    // duration_ms is clamped to [1, 2000].
    bool tx_carrier(uint32_t hz, Cc1101PaLevel level, int duration_ms, std::string& error,
                    std::atomic<bool>* cancel = nullptr);
    // Abort TX immediately (SIDLE + flush).
    bool stop_tx(std::string& error);

    // Async OOK mark/space replay. Positive = mark (µs), negative = space (µs).
    bool tx_raw_timings(uint32_t hz, Cc1101PaLevel level, const std::vector<int>& timings_us, int repeats,
                        int delay_ms, std::string& error, std::atomic<bool>* cancel = nullptr);

    // Async TX of raw mark/space is Cap-specific; send a short test FIFO packet instead.
    bool send_test_packet(const uint8_t* payload, size_t len, std::string& error);

    // Poll GDO0 level (requires Cap GPIO15). Used for modulation heuristics / capture.
    bool read_gdo0(bool& high, std::string& error);

    uint32_t frequency_hz() const { return freq_hz_; }
    Cc1101Preset preset() const { return preset_; }

private:
    struct Impl;
    Impl* impl_{nullptr};
    uint32_t freq_hz_{433920000};
    Cc1101Preset preset_{Cc1101Preset::SpectrumWide};
    Cc1101PaLevel pa_{Cc1101PaLevel::Low};
    bool rx_active_{false};

    bool strobe(uint8_t cmd, std::string& error);
    bool write_reg(uint8_t reg, uint8_t val, std::string& error);
    bool read_status(uint8_t reg, uint8_t& val, std::string& error);
    bool write_burst(uint8_t addr, const uint8_t* data, size_t len, std::string& error);
    bool wait_idle(std::string& error);
};

// Flipper SubGhz RAW helpers (real mark/space timings).
bool write_sub_raw(const std::string& path, uint32_t freq_hz, const std::vector<int>& timings_us,
                   std::string& error, const std::string& preset_name = "FuriHalSubGhzPresetOok650Async",
                   const std::string& comment_block = {});
bool write_sub_raw_from_rssi(const std::string& path, uint32_t freq_hz, const std::vector<int>& rssi_samples,
                             std::string& error); // legacy archival path
bool list_sub_files(const std::string& dir, std::vector<std::string>& names, std::string& error);

struct SubFileInfo {
    uint32_t freq_hz{0};
    std::string preset;
    std::string protocol; // "RAW" or named
    std::string key_hex;
    int bit_count{0};
    int te_us{0};
    std::vector<int> timings_us;
    std::string path;
    std::string comment;
};

bool parse_sub_file(const std::string& path, SubFileInfo& out, std::string& error);

} // namespace platform

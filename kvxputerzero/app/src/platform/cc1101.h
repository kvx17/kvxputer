#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct Cc1101Session {
    bool open(const std::string& spidev, std::string& error);
    void close();
    bool ok() const;

    bool soft_reset(std::string& error);
    bool set_frequency_hz(uint32_t hz, std::string& error);
    bool read_rssi_dbm(int& dbm, std::string& error);
    // Hop across band; append "freq_hz,rssi_dbm" lines. cancel may stop early.
    bool spectrum_scan(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, std::vector<std::string>& lines,
                       std::string& error, bool* cancel = nullptr);
    // Stay in RX briefly and collect RSSI samples (for listen / record).
    bool listen_rssi(int samples, int delay_ms, std::vector<int>& out, std::string& error, bool* cancel = nullptr);
    // Async TX of raw mark/space is Cap-specific; send a short test FIFO packet instead.
    bool send_test_packet(const uint8_t* payload, size_t len, std::string& error);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

bool write_sub_raw(const std::string& path, uint32_t freq_hz, const std::vector<int>& rssi_samples,
                   std::string& error);
bool list_sub_files(const std::string& dir, std::vector<std::string>& names, std::string& error);

} // namespace platform

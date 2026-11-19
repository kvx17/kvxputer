#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

struct GpsFix {
    bool valid{false};
    double lat{0};
    double lon{0};
    double alt_m{0};
    double hdop{-1};
    int sats{0};
    int sats_in_view{0};
    int quality{0};
    char rmc_status{'-'};
    std::string time_utc;
    std::string date_utc;
    std::string error;
};

enum class GpsSource : int {
    Auto = 0,
    Gpsd = 1,
    Nmea = 2,
};

// Physical GNSS attachment on Cardputer Zero.
enum class GpsHardware : int {
    CapLoRa = 0,   // Cap LoRa-1262 onboard ATGM336H — EXT5V (+ POWER_EN)
    UnitGpsV11 = 1, // Unit GPS v1.1 on Grove HY2.0-4P — grove UART + grove_5v
};

struct GpsSessionState {
    bool open{false};
    bool valid{false};
    double lat{0};
    double lon{0};
    double alt_m{0};
    double hdop{-1};
    int sats{0};
    int sats_in_view{0};
    int quality{0};
    char rmc_status{'-'};
    std::string time_utc;
    std::string date_utc;
    std::string source_label;
    std::string hardware_label;
    std::string path;
    int baud{115200};
    std::string open_result;
    uint64_t bytes{0};
    uint64_t sentences{0};
    uint64_t checksum_ok{0};
    uint64_t checksum_bad{0};
    std::string last_talker;
    std::string last_error;
    uint32_t age_ms{0xFFFFFFFFu};
};

// Streaming GPS. Poll state() from the UI/worker thread. Desktop start() never opens /dev.
class GpsSession {
public:
    GpsSession() = default;
    ~GpsSession();

    GpsSession(const GpsSession&) = delete;
    GpsSession& operator=(const GpsSession&) = delete;

    bool start(GpsSource source, const std::string& nmea_tty, int baud, std::string& error,
               GpsHardware hardware = GpsHardware::UnitGpsV11);
    void stop();
    bool running() const { return running_.load(); }

    GpsSessionState state() const;

private:
    void thread_main(GpsSource source, std::string nmea_tty, int baud, GpsHardware hardware);
    void apply_nmea_line(const std::string& line);
    void apply_gpsd_line(const std::string& line);
    bool open_gpsd(std::string& error);
    bool open_nmea(const std::string& path, int baud, std::string& error);
    void close_fd();
    void pump_once();

    mutable std::mutex mu_;
    GpsSessionState st_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::thread thr_;
    int fd_{-1};
    bool via_gpsd_{false};
    std::string line_buf_;
    std::chrono::steady_clock::time_point last_fix_tp_{};
};

GpsSource gps_source_from_string(const std::string& s);
const char* gps_source_name(GpsSource s);

GpsHardware gps_hardware_from_string(const std::string& s);
const char* gps_hardware_name(GpsHardware h);
const char* gps_hardware_label(GpsHardware h); // UI label

GpsFix wait_for_fix(int timeout_ms = 3000, bool* cancel = nullptr, const std::string& nmea_tty = {},
                    int baud = 115200, GpsHardware hardware = GpsHardware::UnitGpsV11);

} // namespace platform

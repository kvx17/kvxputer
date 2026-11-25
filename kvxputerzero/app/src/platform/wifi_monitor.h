#pragma once

#include "wifi_scan.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace platform {

struct WifiMonitorIface {
    std::string ifname;
    std::string phy;
    std::string driver;
    std::string usb_product; // empty if not USB
    std::string usb_vid_pid; // "vvvv:pppp" or empty
    bool selectable{false};
    std::string unavailable_reason;
};

struct WifiChannelSample {
    uint8_t ch{0};
    uint8_t load{0};
    uint8_t peak{0};
    int8_t rssi{0}; // 0 if none (matches ESP tool)
};

struct WifiApEntry {
    uint8_t bssid[6]{};
    char ssid[33]{};
    char revealed[33]{};
    uint8_t ssid_len{0};
    bool hidden{false};
    int8_t rssi{-128};
    uint8_t channel{0};
    uint16_t beacon_interval{0};
    uint16_t capab{0};
    bool has_wpa{false};
    bool has_rsn{false};
    bool has_sae{false};
    bool used{false};
};

struct WifiAnalyzerState {
    static constexpr int kChannels = 11;
    static constexpr int kApMax = 48;

    std::array<uint8_t, 12> load{};
    std::array<uint8_t, 12> peak{};
    std::array<int8_t, 12> rssi{};
    std::array<WifiApEntry, kApMax> aps{};
    int ap_count{0};
    uint16_t dwell_ms{350};
    uint8_t nav_ch{1};
    uint8_t lock_ch{0}; // 0 = hop
    uint8_t sweep_ch{1};
    int sweep_idx{0};
};

// Enumerate wireless interfaces. Desktop / missing iw returns empty with error set.
std::vector<WifiMonitorIface> list_monitor_ifaces(std::string& error);

// Empty or "onboard" => use nmcli STA scan (default). Else USB monitor iface name.
std::string load_monitor_iface();
void save_monitor_iface(const std::string& ifname);
bool monitor_iface_is_onboard(const std::string& ifname);
bool monitor_iface_valid(const std::string& ifname, std::string& error);

// Path to kvxputerzero-wifimon helper, or empty.
std::string find_wifimon_helper();

std::string wifi_ap_auth_label(const WifiApEntry& ap);
std::string wifi_ap_mac_str(const uint8_t bssid[6]);

void wifi_analyzer_reset(WifiAnalyzerState& st);
void wifi_analyzer_set_dwell(WifiAnalyzerState& st, int dwell_ms);
void wifi_analyzer_toggle_lock(WifiAnalyzerState& st);
void wifi_analyzer_finish_dwell(WifiAnalyzerState& st, uint32_t frame_bytes, uint32_t pkt_count,
                                int8_t peak_rssi, uint32_t elapsed_ms);

// STA / nmcli path: rebuild AP table + channel bars from a scan (no airtime).
// load ≈ min(100, AP_count_on_ch * 12), matching the Tab5 fallback.
void wifi_analyzer_apply_scan(WifiAnalyzerState& st, const std::vector<WifiAp>& aps);

// Parse one 802.11 frame (radiotap already stripped). Counts toward airtime via out params.
// Updates AP table for beacon/probe-response.
void wifi_analyzer_ingest_frame(WifiAnalyzerState& st, const uint8_t* frame, size_t len, int8_t rssi_dbm,
                                uint8_t radiotap_ch, uint32_t& out_bytes, uint32_t& out_pkts,
                                int8_t& out_peak_rssi);

// Parse radiotap + 802.11. Returns false if too short / not radiotap.
bool wifi_parse_radiotap(const uint8_t* pkt, size_t len, size_t& header_len, int8_t& rssi_dbm,
                         uint8_t& channel);

std::vector<std::string> wifi_ap_row_labels(const WifiAnalyzerState& st, uint8_t filter_ch);
int wifi_ap_index_at_row(const WifiAnalyzerState& st, uint8_t filter_ch, int row);
std::vector<std::pair<std::string, std::string>> wifi_ap_detail_fields(const WifiApEntry& ap);

bool wifi_analyzer_export_csv(const WifiAnalyzerState& st, std::string& path_out, std::string& error);

// Session that talks to kvxputerzero-wifimon over pipes.
class WifiMonitorSession {
public:
    WifiMonitorSession() = default;
    ~WifiMonitorSession();

    WifiMonitorSession(const WifiMonitorSession&) = delete;
    WifiMonitorSession& operator=(const WifiMonitorSession&) = delete;

    bool start(const std::string& ifname, std::string& error);
    void stop();
    bool running() const { return running_; }

    // Set channel 1-11. Returns false on write failure.
    bool set_channel(uint8_t ch);

    // Read one length-prefixed frame. Returns false on EOF/error/timeout (timeout_ms).
    bool read_frame(std::vector<uint8_t>& out, int timeout_ms, std::string& error);

private:
    void restore_child();
    bool read_exact(uint8_t* dst, size_t len, int timeout_ms, std::string& error);

    pid_t child_{-1};
    int stdin_fd_{-1};
    int stdout_fd_{-1};
    bool running_{false};
    std::vector<uint8_t> pending_; // leftover after OK banner
};

// Run kvxputerzero-wifimon <iface> --ids until cancel. Streams text lines (DEAUTH/DISASSOC/STATS).
// Returns false if helper/iface unavailable (error set). Detection only — no transmit.
bool run_deauth_ids(const std::string& ifname, std::atomic<bool>& cancel,
                    const std::function<void(const std::string&)>& on_line, std::string& error);

} // namespace platform

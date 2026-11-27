#pragma once

#include <string>

namespace platform {

// Wired-link status for the Network Analyzer STAT tab. All sources are
// non-privileged: /sys/class/net, /proc/net/route, resolv.conf, and `ip`.
struct EthStat {
    bool present{false};        // a wired (ethernet) interface exists
    std::string iface;          // chosen interface name (eth0, enx..., ...)
    bool carrier{false};        // link up
    int speed_mbps{-1};         // valid only while carrier is up
    std::string duplex;         // "full" | "half" | ""
    std::string hw;             // MAC
    long carrier_changes{0};    // raw /sys counter
    long flaps{0};              // (carrier_changes - base) / 2
    std::string ipv4;
    int prefix{-1};
    std::string gateway;
    std::string dns;
    std::string error;
};

// Pick the wired interface: override (if non-empty and present) else the first
// non-wireless ethernet interface, preferring one that is up. Empty if none.
std::string pick_eth_iface(const std::string& override_name = {});

// Read current status. `flap_base` is the carrier_changes value captured when the
// screen opened, so flaps count only changes during this session.
EthStat read_eth_stat(const std::string& iface, long flap_base);

} // namespace platform

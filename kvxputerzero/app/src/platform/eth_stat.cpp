#include "eth_stat.h"

#include "net_info.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace platform {
namespace {

std::string read_sysfs(const std::string& iface, const char* leaf) {
    std::ifstream in("/sys/class/net/" + iface + "/" + leaf);
    std::string v;
    if (in) std::getline(in, v);
    // Trim trailing whitespace/newline.
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
    return v;
}

// Default gateway for `iface` from /proc/net/route (little-endian hex).
std::string default_gateway(const std::string& iface) {
    std::ifstream in("/proc/net/route");
    if (!in) return {};
    std::string line;
    std::getline(in, line);  // header
    std::string best;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string name, dest, gw;
        ss >> name >> dest >> gw;
        if (dest != "00000000") continue;  // default route only
        if (!iface.empty() && name != iface) continue;
        unsigned long g = std::strtoul(gw.c_str(), nullptr, 16);
        if (g == 0) continue;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu", g & 0xFF, (g >> 8) & 0xFF, (g >> 16) & 0xFF,
                      (g >> 24) & 0xFF);
        best = buf;
        break;
    }
    return best;
}

// First non-loopback nameserver from resolv.conf (skips 127.* stub resolvers).
std::string first_dns() {
    const char* files[] = {"/run/systemd/resolve/resolv.conf", "/etc/resolv.conf"};
    for (const char* f : files) {
        std::ifstream in(f);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("nameserver", 0) != 0) continue;
            std::istringstream ss(line);
            std::string kw, ip;
            ss >> kw >> ip;
            if (!ip.empty() && ip.rfind("127.", 0) != 0) return ip;
        }
    }
    return {};
}

} // namespace

std::string pick_eth_iface(const std::string& override_name) {
    std::string err;
    const auto ifaces = list_net_ifaces(err);
    if (!override_name.empty()) {
        for (const auto& i : ifaces)
            if (i.name == override_name) return override_name;
    }
    std::string first_eth;
    for (const auto& i : ifaces) {
        if (i.kind != "ethernet") continue;
        if (first_eth.empty()) first_eth = i.name;
        if (i.up) return i.name;  // prefer an interface that is up
    }
    return first_eth;
}

EthStat read_eth_stat(const std::string& iface, long flap_base) {
    EthStat s;
    s.iface = iface;
    if (iface.empty()) {
        s.error = "no wired interface";
        return s;
    }
    s.present = true;
    s.carrier = read_sysfs(iface, "carrier") == "1";
    s.hw = read_sysfs(iface, "address");
    const std::string cc = read_sysfs(iface, "carrier_changes");
    if (!cc.empty()) {
        s.carrier_changes = std::strtol(cc.c_str(), nullptr, 10);
        s.flaps = (s.carrier_changes - flap_base) / 2;
        if (s.flaps < 0) s.flaps = 0;
    }
    if (s.carrier) {
        const std::string sp = read_sysfs(iface, "speed");
        if (!sp.empty()) s.speed_mbps = static_cast<int>(std::strtol(sp.c_str(), nullptr, 10));
        s.duplex = read_sysfs(iface, "duplex");
    }

    std::string err;
    const NetIface ni = find_iface(iface, err);
    s.ipv4 = ni.ipv4;
    s.prefix = ni.prefix;
    s.gateway = default_gateway(iface);
    if (s.gateway.empty()) s.gateway = ni.gateway;
    s.dns = ni.dns.empty() ? first_dns() : ni.dns;
    if (s.dns.empty()) s.dns = first_dns();
    return s;
}

} // namespace platform

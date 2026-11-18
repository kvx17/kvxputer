#include "net_info.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace platform {
namespace {

std::string run_capture(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return {};
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string kind_for(const std::string& name) {
    if (name.rfind("wl", 0) == 0 || name.rfind("wlan", 0) == 0) return "wifi";
    if (name.rfind("en", 0) == 0 || name.rfind("eth", 0) == 0 || name == "end0") return "ethernet";
    return "other";
}

} // namespace

std::vector<NetIface> list_net_ifaces(std::string& error) {
    error.clear();
    std::vector<NetIface> out;
    const std::string text = run_capture("ip -o -4 addr show 2>/dev/null");
    if (text.empty()) {
        // Fallback: parse /proc/net/dev names only.
        std::ifstream in("/proc/net/dev");
        if (!in) {
            error = "cannot read network interfaces";
            return out;
        }
        std::string line;
        std::getline(in, line);
        std::getline(in, line);
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = trim(line.substr(0, colon));
            if (name == "lo") continue;
            NetIface iface;
            iface.name = name;
            iface.kind = kind_for(name);
            iface.state = "unknown";
            out.push_back(std::move(iface));
        }
        return out;
    }

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // Format: 2: wlan0    inet 192.168.1.2/24 ...
        std::istringstream ls(line);
        std::string idx, name, inet_tok, cidr;
        ls >> idx >> name >> inet_tok >> cidr;
        if (name.empty() || name == "lo") continue;
        if (!name.empty() && name.back() == ':') name.pop_back();
        NetIface iface;
        iface.name = name;
        iface.kind = kind_for(name);
        iface.up = true;
        iface.state = "up";
        const auto slash = cidr.find('/');
        if (slash != std::string::npos) {
            iface.ipv4 = cidr.substr(0, slash);
            try {
                iface.prefix = std::stoi(cidr.substr(slash + 1));
            } catch (...) {
                iface.prefix = -1;
            }
        } else {
            iface.ipv4 = cidr;
        }
        out.push_back(std::move(iface));
    }

    // Enrich gateway from ip route
    const std::string route = run_capture("ip route show default 2>/dev/null");
    std::string def_dev;
    std::string def_gw;
    {
        std::istringstream rs(route);
        std::string tok;
        while (rs >> tok) {
            if (tok == "via") rs >> def_gw;
            else if (tok == "dev") rs >> def_dev;
        }
    }
    for (auto& iface : out) {
        if (!def_dev.empty() && iface.name == def_dev) iface.gateway = def_gw;
        const std::string link = run_capture(("ip -o link show " + iface.name + " 2>/dev/null").c_str());
        const auto link_pos = link.find("link/ether ");
        if (link_pos != std::string::npos) {
            iface.hw_address = trim(link.substr(link_pos + 11, 17));
        }
    }
    return out;
}

NetIface default_route_iface(std::string& error) {
    auto ifaces = list_net_ifaces(error);
    if (!error.empty() && ifaces.empty()) return {};
    for (const auto& iface : ifaces) {
        if (!iface.gateway.empty()) return iface;
    }
    if (!ifaces.empty()) return ifaces.front();
    error = "no interfaces";
    return {};
}

NetIface find_iface(const std::string& name, std::string& error) {
    auto ifaces = list_net_ifaces(error);
    for (const auto& iface : ifaces) {
        if (iface.name == name) return iface;
    }
    error = "interface not found";
    return {};
}

} // namespace platform

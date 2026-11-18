#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct NetIface {
    std::string name;
    std::string kind; // wifi | ethernet | other
    std::string state;
    std::string hw_address;
    std::string ipv4;
    int prefix{-1};
    std::string gateway;
    std::string dns;
    bool up{false};
};

std::vector<NetIface> list_net_ifaces(std::string& error);
NetIface default_route_iface(std::string& error);
NetIface find_iface(const std::string& name, std::string& error);

} // namespace platform

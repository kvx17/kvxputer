#pragma once

#include <string>
#include <vector>

namespace platform {

struct W1Device {
    std::string id; // e.g. 28-xxxxxxxxxxxx or 01-...
    std::string path;
};

std::vector<W1Device> list_w1_devices(std::string& error);
bool read_w1_id_file(const std::string& device_path, std::string& id_out, std::string& error);
bool save_ibutton(const std::string& path, const std::string& id, std::string& error);

} // namespace platform

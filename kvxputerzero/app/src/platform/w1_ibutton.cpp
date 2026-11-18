#include "w1_ibutton.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

} // namespace

std::vector<W1Device> list_w1_devices(std::string& error) {
    error.clear();
    std::vector<W1Device> out;
    if (kDesktop) {
        error = "1-Wire unavailable";
        return out;
    }
    DIR* dir = opendir("/sys/bus/w1/devices");
    if (!dir) {
        error = "no w1 master (load w1-gpio / w1-therm)";
        return out;
    }
    while (dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        if (std::strncmp(ent->d_name, "w1_bus_master", 13) == 0) continue;
        W1Device d;
        d.id = ent->d_name;
        d.path = std::string("/sys/bus/w1/devices/") + ent->d_name;
        out.push_back(std::move(d));
    }
    closedir(dir);
    return out;
}

bool read_w1_id_file(const std::string& device_path, std::string& id_out, std::string& error) {
    id_out.clear();
    std::ifstream in(device_path + "/id");
    if (!in) {
        // Fall back to directory name
        const auto slash = device_path.find_last_of('/');
        id_out = slash == std::string::npos ? device_path : device_path.substr(slash + 1);
        return true;
    }
    std::string hex;
    unsigned char b = 0;
    while (in.read(reinterpret_cast<char*>(&b), 1)) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", b);
        hex += buf;
    }
    if (hex.empty()) {
        error = "empty id";
        return false;
    }
    id_out = hex;
    return true;
}

bool save_ibutton(const std::string& path, const std::string& id, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << id << "\n";
    return true;
}

} // namespace platform

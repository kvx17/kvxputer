#include "runner.h"

#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <unistd.h>

namespace runner {
namespace {

bool comm_is_gui(const std::string& comm) {
    // Exact match for the GUI binary name; ignore CLI and helpers.
    if (comm == "kvxputerzero") return true;
    // Truncated to 15 chars in /proc/*/comm on some kernels — still exact for this name.
    return false;
}

} // namespace

bool graphical_app_busy() {
    DIR* d = opendir("/proc");
    if (!d) return false;
    const pid_t self = getpid();
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char* end = nullptr;
        const long pid = strtol(e->d_name, &end, 10);
        if (!end || *end || pid == self) continue;
        std::string path = std::string("/proc/") + e->d_name + "/comm";
        std::ifstream in(path);
        std::string comm;
        if (!std::getline(in, comm)) continue;
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
        if (comm_is_gui(comm)) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

} // namespace runner

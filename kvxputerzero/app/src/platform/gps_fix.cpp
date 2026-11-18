#include "gps_fix.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace platform {
namespace {

#if USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

bool cancelled(bool* cancel) {
    return cancel && *cancel;
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

bool parse_nmea_rmc_or_gga(const std::string& line, GpsFix& fix) {
    if (line.size() < 6 || line[0] != '$') return false;
    const auto star = line.find('*');
    const std::string body = star == std::string::npos ? line : line.substr(0, star);
    const auto f = split_csv(body);
    if (f.size() < 6) return false;
    const std::string type = f[0].size() >= 6 ? f[0].substr(3) : "";

    auto dm_to_deg = [](const std::string& dm, const std::string& hemi) -> double {
        if (dm.size() < 4) return 0;
        const auto dot = dm.find('.');
        const size_t deg_digits = (dot == std::string::npos) ? (dm.size() > 4 ? 3 : 2) : (dot > 4 ? 3 : 2);
        const double deg = std::stod(dm.substr(0, deg_digits));
        const double minutes = std::stod(dm.substr(deg_digits));
        double v = deg + minutes / 60.0;
        if (!hemi.empty() && (hemi[0] == 'S' || hemi[0] == 'W')) v = -v;
        return v;
    };

    try {
        if (type == "RMC" && f.size() >= 7) {
            if (f[2] != "A") return false;
            fix.time_utc = f[1];
            fix.lat = dm_to_deg(f[3], f[4]);
            fix.lon = dm_to_deg(f[5], f[6]);
            fix.valid = true;
            return true;
        }
        if (type == "GGA" && f.size() >= 7) {
            if (f[6].empty() || f[6] == "0") return false;
            fix.time_utc = f[1];
            fix.lat = dm_to_deg(f[2], f[3]);
            fix.lon = dm_to_deg(f[4], f[5]);
            fix.valid = true;
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

GpsFix try_gpsd(int timeout_ms, bool* cancel) {
    GpsFix fix;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fix.error = "gpsd socket failed";
        return fix;
    }
    const int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2947);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        ::close(fd);
        fix.error = "gpsd not available";
        return fix;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    // Wait for connect.
    {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        timeval tv{0, 200000};
        if (select(fd + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
            ::close(fd);
            fix.error = "gpsd not available";
            return fix;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) {
            ::close(fd);
            fix.error = "gpsd not available";
            return fix;
        }
    }

    const char* watch = "?WATCH={\"enable\":true,\"json\":true}\n";
    (void)::write(fd, watch, std::strlen(watch));

    std::string buf;
    char tmp[512];
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(cancel)) {
            fix.error = "cancelled";
            ::close(fd);
            return fix;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv{0, 100000};
        const int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel > 0) {
            const ssize_t n = ::read(fd, tmp, sizeof(tmp));
            if (n > 0) buf.append(tmp, static_cast<size_t>(n));
            else if (n == 0) break;
        }
        size_t pos = 0;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            // Minimal JSON parse for TPV class with mode>=2.
            if (line.find("\"class\":\"TPV\"") == std::string::npos &&
                line.find("\"class\": \"TPV\"") == std::string::npos) {
                continue;
            }
            if (line.find("\"mode\":3") == std::string::npos && line.find("\"mode\":2") == std::string::npos &&
                line.find("\"mode\": 3") == std::string::npos && line.find("\"mode\": 2") == std::string::npos) {
                continue;
            }
            auto grab = [&](const char* key, double& out) -> bool {
                const std::string k = std::string("\"") + key + "\":";
                auto p = line.find(k);
                if (p == std::string::npos) {
                    const std::string k2 = std::string("\"") + key + "\": ";
                    p = line.find(k2);
                    if (p == std::string::npos) return false;
                    p += k2.size();
                } else {
                    p += k.size();
                }
                try {
                    out = std::stod(line.substr(p));
                    return true;
                } catch (...) {
                    return false;
                }
            };
            double lat = 0;
            double lon = 0;
            if (grab("lat", lat) && grab("lon", lon)) {
                fix.valid = true;
                fix.lat = lat;
                fix.lon = lon;
                auto tp = line.find("\"time\":\"");
                if (tp != std::string::npos) {
                    tp += 8;
                    const auto te = line.find('"', tp);
                    if (te != std::string::npos) fix.time_utc = line.substr(tp, te - tp);
                }
                ::close(fd);
                return fix;
            }
        }
    }
    ::close(fd);
    fix.error = "No GPS fix";
    return fix;
}

GpsFix try_nmea_tty(const std::string& path, int timeout_ms, bool* cancel) {
    GpsFix fix;
    if (path.empty()) {
        fix.error = "No GPS fix";
        return fix;
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fix.error = "NMEA open failed";
        return fix;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string buf;
    char tmp[256];
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(cancel)) {
            fix.error = "cancelled";
            ::close(fd);
            return fix;
        }
        const ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            buf.append(tmp, static_cast<size_t>(n));
            size_t pos = 0;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (parse_nmea_rmc_or_gga(line, fix)) {
                    ::close(fd);
                    return fix;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    ::close(fd);
    fix.error = "No GPS fix";
    return fix;
}

} // namespace

GpsFix wait_for_fix(int timeout_ms, bool* cancel, const std::string& nmea_tty) {
    GpsFix fix;
    if (kDesktop) {
        fix.error = "No GPS fix";
        return fix;
    }
    if (timeout_ms < 100) timeout_ms = 100;
    fix = try_gpsd(timeout_ms, cancel);
    if (fix.valid) return fix;
    if (!nmea_tty.empty()) {
        const auto left = timeout_ms; // best-effort second attempt with same budget
        return try_nmea_tty(nmea_tty, left, cancel);
    }
    if (fix.error.empty()) fix.error = "No GPS fix";
    return fix;
}

} // namespace platform

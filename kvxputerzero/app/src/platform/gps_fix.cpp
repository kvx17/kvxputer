#include "gps_fix.h"

#include "cap_power.h"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <sstream>
#include <vector>

namespace platform {
namespace {

#if USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

bool cancelled(bool* cancel) { return cancel && *cancel; }

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

double dm_to_deg(const std::string& dm, const std::string& hemi) {
    if (dm.size() < 4) return 0;
    const auto dot = dm.find('.');
    const size_t deg_digits = (dot == std::string::npos) ? (dm.size() > 4 ? 3 : 2) : (dot > 4 ? 3 : 2);
    const double deg = std::stod(dm.substr(0, deg_digits));
    const double minutes = std::stod(dm.substr(deg_digits));
    double v = deg + minutes / 60.0;
    if (!hemi.empty() && (hemi[0] == 'S' || hemi[0] == 'W')) v = -v;
    return v;
}

std::string hhmmss_to_utc(const std::string& t) {
    if (t.size() < 6) return t;
    return t.substr(0, 2) + ":" + t.substr(2, 2) + ":" + t.substr(4, 2) + " UTC";
}

std::string ddmmyy_to_date(const std::string& d) {
    if (d.size() < 6) return {};
    int dd = 0, mm = 0, yy = 0;
    try {
        dd = std::stoi(d.substr(0, 2));
        mm = std::stoi(d.substr(2, 2));
        yy = std::stoi(d.substr(4, 2));
    } catch (...) {
        return {};
    }
    const int year = yy >= 70 ? 1900 + yy : 2000 + yy;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, mm, dd);
    return buf;
}

bool nmea_checksum_ok(const std::string& line, bool& has_cs) {
    has_cs = false;
    if (line.size() < 4 || line[0] != '$') return false;
    const auto star = line.find('*');
    if (star == std::string::npos || star + 2 >= line.size()) return true; // no checksum
    has_cs = true;
    unsigned expected = 0;
    try {
        expected = static_cast<unsigned>(std::stoul(line.substr(star + 1, 2), nullptr, 16));
    } catch (...) {
        return false;
    }
    unsigned xorv = 0;
    for (size_t i = 1; i < star; ++i) xorv ^= static_cast<unsigned char>(line[i]);
    return xorv == expected;
}

speed_t baud_to_flag(int baud) {
    switch (baud) {
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return B115200;
    }
}

bool set_tty_baud(int fd, int baud) {
    termios tio{};
    if (tcgetattr(fd, &tio) != 0) return false;
    cfmakeraw(&tio);
    const speed_t sp = baud_to_flag(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tio) == 0;
}

bool json_has_class(const std::string& line, const char* cls) {
    const std::string a = std::string("\"class\":\"") + cls + "\"";
    const std::string b = std::string("\"class\": \"") + cls + "\"";
    return line.find(a) != std::string::npos || line.find(b) != std::string::npos;
}

bool json_grab_double(const std::string& line, const char* key, double& out) {
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
}

bool json_grab_int(const std::string& line, const char* key, int& out) {
    double d = 0;
    if (!json_grab_double(line, key, d)) return false;
    out = static_cast<int>(d);
    return true;
}

} // namespace

GpsSource gps_source_from_string(const std::string& s) {
    if (s == "gpsd") return GpsSource::Gpsd;
    if (s == "nmea" || s == "tty" || s == "serial") return GpsSource::Nmea;
    return GpsSource::Auto;
}

const char* gps_source_name(GpsSource s) {
    switch (s) {
    case GpsSource::Gpsd: return "gpsd";
    case GpsSource::Nmea: return "nmea";
    case GpsSource::Auto:
    default: return "auto";
    }
}

GpsHardware gps_hardware_from_string(const std::string& s) {
    if (s == "cap" || s == "cap_lora" || s == "lora" || s == "cap-lora-1262") return GpsHardware::CapLoRa;
    if (s == "unit" || s == "unit_v11" || s == "gps_v11" || s == "grove" || s == "unit-gps-v1.1")
        return GpsHardware::UnitGpsV11;
    return GpsHardware::UnitGpsV11;
}

const char* gps_hardware_name(GpsHardware h) {
    switch (h) {
    case GpsHardware::CapLoRa: return "cap_lora";
    case GpsHardware::UnitGpsV11:
    default: return "unit_v11";
    }
}

const char* gps_hardware_label(GpsHardware h) {
    switch (h) {
    case GpsHardware::CapLoRa: return "LoRa Cap GNSS";
    case GpsHardware::UnitGpsV11:
    default: return "Unit GPS v1.1";
    }
}

GpsSession::~GpsSession() { stop(); }

bool GpsSession::start(GpsSource source, const std::string& nmea_tty, int baud, std::string& error,
                       GpsHardware hardware) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mu_);
        st_ = GpsSessionState{};
        st_.baud = baud > 0 ? baud : 115200;
        st_.path = nmea_tty.empty() ? "/dev/serial0" : nmea_tty;
        st_.source_label = gps_source_name(source);
        st_.hardware_label = gps_hardware_label(hardware);
    }
    if (kDesktop) {
        std::lock_guard<std::mutex> lock(mu_);
        st_.last_error = "No GPS on desktop";
        st_.open_result = "desktop";
        error = st_.last_error;
        return false;
    }
    // Both attachments speak NMEA on /dev/serial0 @ 115200; power rails differ.
    {
        std::string perr;
        bool powered = false;
        if (hardware == GpsHardware::CapLoRa) {
            // Cap LoRa-1262 GNSS: EXT5V + POWER_EN high. No SX1262/SPI (keeps UART on 14/15).
            powered = enable_cap_power(CapKindHint::Gps, perr);
        } else {
            // Unit GPS v1.1 on Grove: UART mode (grove_fun=0) + grove_5v_out.
            powered = enable_grove_uart_power(perr);
        }
        if (!powered) {
            std::lock_guard<std::mutex> lock(mu_);
            st_.last_error = perr.empty() ? "GPS power enable failed" : perr;
            st_.open_result = st_.last_error;
            error = st_.last_error;
            return false;
        }
        // Module needs a beat after Grove 5V before NMEA streams.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // Physical Cap/Unit UART: prefer raw NMEA. gpsd often holds serial0 with no sats.
    if (source == GpsSource::Auto) source = GpsSource::Nmea;
    stop_ = false;
    running_ = true;
    thr_ = std::thread([this, source, nmea_tty, baud, hardware]() {
        thread_main(source, nmea_tty.empty() ? "/dev/serial0" : nmea_tty, baud > 0 ? baud : 115200,
                    hardware);
    });
    return true;
}

void GpsSession::stop() {
    stop_ = true;
    if (thr_.joinable()) thr_.join();
    close_fd();
    release_grove_uart_power(true);
    release_cap_power(true);
    running_ = false;
}

GpsSessionState GpsSession::state() const {
    std::lock_guard<std::mutex> lock(mu_);
    GpsSessionState s = st_;
    if (s.valid && last_fix_tp_.time_since_epoch().count() != 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - last_fix_tp_)
                            .count();
        s.age_ms = ms < 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(ms);
    }
    return s;
}

void GpsSession::close_fd() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    via_gpsd_ = false;
    line_buf_.clear();
}

bool GpsSession::open_gpsd(std::string& error) {
    close_fd();
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = "gpsd socket failed";
        return false;
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
        error = "gpsd not available";
        return false;
    }
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    timeval tv{0, 300000};
    if (select(fd + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
        ::close(fd);
        error = "gpsd not available";
        return false;
    }
    int soerr = 0;
    socklen_t sl = sizeof(soerr);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
    if (soerr != 0) {
        ::close(fd);
        error = "gpsd not available";
        return false;
    }
    const char* watch = "?WATCH={\"enable\":true,\"json\":true}\n";
    (void)::write(fd, watch, std::strlen(watch));
    fd_ = fd;
    via_gpsd_ = true;
    error.clear();
    return true;
}

bool GpsSession::open_nmea(const std::string& path, int baud, std::string& error) {
    close_fd();
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        error = "NMEA open failed";
        return false;
    }
    if (!set_tty_baud(fd, baud)) {
        // Still try to read; some gpsd-owned ports reject termios.
    }
    fd_ = fd;
    via_gpsd_ = false;
    error.clear();
    return true;
}

void GpsSession::apply_nmea_line(const std::string& line) {
    if (line.size() < 6 || line[0] != '$') return;
    bool has_cs = false;
    const bool ok = nmea_checksum_ok(line, has_cs);
    {
        std::lock_guard<std::mutex> lock(mu_);
        st_.sentences++;
        if (has_cs) {
            if (ok) st_.checksum_ok++;
            else st_.checksum_bad++;
        }
    }
    if (has_cs && !ok) return;

    const auto star = line.find('*');
    const std::string body = star == std::string::npos ? line : line.substr(0, star);
    const auto f = split_csv(body);
    // Address is $TTMMM (2-char talker + 3-char type), e.g. $GNGGA / $BDGSV.
    if (f.empty() || f[0].size() < 6 || f[0][0] != '$') return;
    const std::string addr = f[0].substr(1);
    std::string type = addr.size() >= 5 ? addr.substr(addr.size() - 3) : "";
    for (char& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::string talker = addr.size() > 3 ? addr.substr(0, addr.size() - 3) : addr;

    auto parse_int = [](const std::string& s, int fallback = 0) -> int {
        if (s.empty()) return fallback;
        try {
            return std::stoi(s);
        } catch (...) {
            return fallback;
        }
    };
    auto parse_double = [](const std::string& s, double fallback = 0) -> double {
        if (s.empty()) return fallback;
        try {
            return std::stod(s);
        } catch (...) {
            return fallback;
        }
    };

    try {
        if (type == "GGA" && f.size() >= 8) {
            const int q = parse_int(f[6]);
            // Field 7 = sats in use. Empty means unknown — do not wipe a GSV count.
            const bool have_sats = f.size() > 7 && !f[7].empty();
            const int sats_used = have_sats ? parse_int(f[7]) : -1;
            const double hdop = f.size() > 8 ? parse_double(f[8], -1.0) : -1.0;
            const double alt = f.size() > 9 ? parse_double(f[9]) : 0.0;
            std::lock_guard<std::mutex> lock(mu_);
            st_.last_talker = talker + type;
            st_.quality = q;
            if (sats_used >= 0) st_.sats = sats_used;
            if (hdop > 0) st_.hdop = hdop;
            st_.alt_m = alt;
            if (!f[1].empty()) st_.time_utc = hhmmss_to_utc(f[1]);
            if (q > 0 && f.size() > 5 && !f[2].empty() && !f[4].empty()) {
                st_.lat = dm_to_deg(f[2], f[3]);
                st_.lon = dm_to_deg(f[4], f[5]);
                st_.valid = true;
                last_fix_tp_ = std::chrono::steady_clock::now();
                st_.last_error.clear();
            } else if (q == 0) {
                st_.valid = false;
            }
            return;
        }
        if (type == "GSV" && f.size() >= 4) {
            // $GxGSV,totalMsgs,msgNum,satsInView,...
            const int in_view = parse_int(f[3]);
            std::lock_guard<std::mutex> lock(mu_);
            st_.last_talker = talker + type;
            if (in_view > 0) {
                if (in_view > st_.sats_in_view) st_.sats_in_view = in_view;
                if (st_.sats <= 0) st_.sats = in_view;
            }
            return;
        }
        if (type == "GSA" && f.size() >= 4) {
            int used = 0;
            for (size_t i = 3; i <= 15 && i < f.size(); ++i) {
                if (!f[i].empty()) ++used;
            }
            std::lock_guard<std::mutex> lock(mu_);
            st_.last_talker = talker + type;
            if (used > 0) st_.sats = used;
            if (f.size() > 17 && !f[17].empty()) {
                const double hdop = parse_double(f[17], -1.0);
                if (hdop > 0) st_.hdop = hdop;
            }
            return;
        }
        if (type == "RMC" && f.size() >= 7) {
            const char stch = f[2].empty() ? '-' : f[2][0];
            std::lock_guard<std::mutex> lock(mu_);
            st_.last_talker = talker + type;
            st_.rmc_status = stch;
            if (!f[1].empty()) st_.time_utc = hhmmss_to_utc(f[1]);
            if (f.size() > 9 && !f[9].empty()) st_.date_utc = ddmmyy_to_date(f[9]);
            if ((stch == 'A' || stch == 'a') && !f[3].empty() && !f[5].empty()) {
                st_.lat = dm_to_deg(f[3], f[4]);
                st_.lon = dm_to_deg(f[5], f[6]);
                st_.valid = true;
                if (st_.quality < 1) st_.quality = 1;
                last_fix_tp_ = std::chrono::steady_clock::now();
                st_.last_error.clear();
            } else if (stch == 'V' || stch == 'v') {
                st_.valid = false;
            }
        }
    } catch (...) {
    }
}

void GpsSession::apply_gpsd_line(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        st_.sentences++;
    }
    if (json_has_class(line, "SKY")) {
        int nsat = 0;
        double hdop = -1;
        if (!json_grab_int(line, "uSat", nsat)) (void)json_grab_int(line, "nSat", nsat);
        // Fallback: count objects in the satellites array (gpsd often omits uSat/nSat).
        if (nsat <= 0) {
            const auto p = line.find("\"satellites\"");
            if (p != std::string::npos) {
                const auto lb = line.find('[', p);
                const auto rb = line.find(']', lb == std::string::npos ? p : lb);
                if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                    int count = 0;
                    for (size_t i = lb; i < rb; ++i) {
                        if (line[i] == '{') ++count;
                    }
                    nsat = count;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (nsat > 0) st_.sats = nsat;
            st_.last_talker = "SKY";
            if (json_grab_double(line, "hdop", hdop) && hdop > 0) st_.hdop = hdop;
        }
        return;
    }
    if (!json_has_class(line, "TPV")) return;
    int mode = 0;
    // Still record talker / time when mode < 2 so Status shows activity.
    if (!json_grab_int(line, "mode", mode) || mode < 2) {
        std::lock_guard<std::mutex> lock(mu_);
        st_.last_talker = "TPV";
        st_.quality = mode;
        st_.rmc_status = 'V';
        return;
    }
    double lat = 0, lon = 0, alt = 0, hdop = -1;
    const bool have_ll = json_grab_double(line, "lat", lat) && json_grab_double(line, "lon", lon);
    if (!have_ll) return;
    (void)json_grab_double(line, "alt", alt);
    (void)json_grab_double(line, "altHAE", alt);
    (void)json_grab_double(line, "hdop", hdop);
    std::string time;
    auto tp = line.find("\"time\":\"");
    if (tp != std::string::npos) {
        tp += 8;
        const auto te = line.find('"', tp);
        if (te != std::string::npos) time = line.substr(tp, te - tp);
    }
    std::lock_guard<std::mutex> lock(mu_);
    st_.lat = lat;
    st_.lon = lon;
    st_.alt_m = alt;
    if (hdop > 0) st_.hdop = hdop;
    st_.valid = true;
    st_.quality = mode >= 3 ? 1 : 1;
    st_.rmc_status = 'A';
    st_.last_talker = "TPV";
    if (!time.empty()) {
        st_.time_utc = time;
        if (time.size() >= 10) st_.date_utc = time.substr(0, 10);
    }
    last_fix_tp_ = std::chrono::steady_clock::now();
    st_.last_error.clear();
}

void GpsSession::pump_once() {
    if (fd_ < 0) return;
    char tmp[512];
    const ssize_t n = ::read(fd_, tmp, sizeof(tmp));
    if (n > 0) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            st_.bytes += static_cast<uint64_t>(n);
        }
        line_buf_.append(tmp, static_cast<size_t>(n));
        size_t pos = 0;
        while ((pos = line_buf_.find('\n')) != std::string::npos) {
            std::string line = line_buf_.substr(0, pos);
            line_buf_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (via_gpsd_) apply_gpsd_line(line);
            else apply_nmea_line(line);
        }
        if (line_buf_.size() > 4096) line_buf_.clear();
    } else if (n == 0) {
        std::lock_guard<std::mutex> lock(mu_);
        st_.last_error = via_gpsd_ ? "gpsd closed" : "NMEA EOF";
        st_.open = false;
        close_fd();
    }
}

void GpsSession::thread_main(GpsSource source, std::string nmea_tty, int baud, GpsHardware hardware) {
    std::string err;
    bool opened = false;
    bool via_gpsd_session = false;
    if (source == GpsSource::Gpsd || source == GpsSource::Auto) {
        if (open_gpsd(err)) {
            opened = true;
            via_gpsd_session = true;
            std::lock_guard<std::mutex> lock(mu_);
            st_.open = true;
            st_.open_result = "gpsd ok";
            st_.source_label = "gpsd";
            st_.path = "127.0.0.1:2947";
            st_.baud = baud;
            st_.last_error.clear();
        } else if (source == GpsSource::Gpsd) {
            std::lock_guard<std::mutex> lock(mu_);
            st_.open = false;
            st_.open_result = err;
            st_.last_error = err;
            running_ = false;
            return;
        }
    }
    if (!opened) {
        if (open_nmea(nmea_tty, baud, err)) {
            opened = true;
            via_gpsd_session = false;
            std::lock_guard<std::mutex> lock(mu_);
            st_.open = true;
            st_.open_result = "nmea ok";
            st_.source_label = "nmea";
            st_.path = nmea_tty;
            st_.baud = baud;
            st_.last_error.clear();
        } else {
            std::lock_guard<std::mutex> lock(mu_);
            st_.open = false;
            st_.open_result = err;
            st_.last_error = err.empty() ? "No GPS" : err;
            running_ = false;
            return;
        }
    }

    // Auto: if gpsd never produces a fix (or SKY sats) within a few seconds, try raw NMEA.
    // gpsd often holds /dev/serial0 exclusively — NMEA open then fails with a clear error.
    const auto started = std::chrono::steady_clock::now();
    bool tried_nmea_fallback = (source != GpsSource::Auto) || !via_gpsd_session;

    auto last_power_touch = std::chrono::steady_clock::now();

    while (!stop_) {
        if (fd_ < 0) break;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        timeval tv{0, 100000};
        const int sel = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (sel > 0) pump_once();
        else if (sel < 0 && errno != EINTR) break;

        // scrolld may reclaim Grove as I2C; only touch sysfs when needed (see enable_*).
        if (std::chrono::steady_clock::now() - last_power_touch > std::chrono::seconds(5)) {
            last_power_touch = std::chrono::steady_clock::now();
            std::string perr;
            if (hardware == GpsHardware::UnitGpsV11) (void)enable_grove_uart_power(perr);
            else if (hardware == GpsHardware::CapLoRa) (void)enable_cap_power(CapKindHint::Gps, perr);
        }

        if (!tried_nmea_fallback && via_gpsd_session) {
            const auto st = state();
            const auto elapsed = std::chrono::steady_clock::now() - started;
            if (st.valid || st.sats > 0) {
                tried_nmea_fallback = true; // gpsd is useful; stay
            } else if (elapsed > std::chrono::seconds(4)) {
                tried_nmea_fallback = true;
                close_fd();
                std::string nerr;
                if (open_nmea(nmea_tty, baud, nerr)) {
                    via_gpsd_session = false;
                    std::lock_guard<std::mutex> lock(mu_);
                    st_.open = true;
                    st_.open_result = "nmea ok (gpsd no fix)";
                    st_.source_label = "nmea";
                    st_.path = nmea_tty;
                    st_.baud = baud;
                    st_.last_error.clear();
                } else {
                    // Re-open gpsd so Status keeps streaming something.
                    std::string gerr;
                    if (open_gpsd(gerr)) {
                        via_gpsd_session = true;
                        std::lock_guard<std::mutex> lock(mu_);
                        st_.open = true;
                        st_.open_result = "gpsd ok";
                        st_.source_label = "gpsd";
                        st_.path = "127.0.0.1:2947";
                        st_.last_error =
                            "gpsd holds " + nmea_tty + " — stop gpsd to use NMEA, or wait for gpsd fix";
                    } else {
                        std::lock_guard<std::mutex> lock(mu_);
                        st_.open = false;
                        st_.open_result = nerr;
                        st_.last_error = nerr + "; gpsd also gone";
                        break;
                    }
                }
            }
        }
    }
    close_fd();
    {
        std::lock_guard<std::mutex> lock(mu_);
        st_.open = false;
    }
    running_ = false;
}

GpsFix wait_for_fix(int timeout_ms, bool* cancel, const std::string& nmea_tty, int baud,
                    GpsHardware hardware) {
    GpsFix fix;
    if (kDesktop) {
        fix.error = "No GPS fix";
        return fix;
    }
    if (timeout_ms < 100) timeout_ms = 100;
    GpsSession session;
    std::string err;
    const GpsSource src = nmea_tty.empty() ? GpsSource::Auto : GpsSource::Auto;
    if (!session.start(src, nmea_tty, baud, err, hardware)) {
        fix.error = err.empty() ? "No GPS fix" : err;
        return fix;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(cancel)) {
            fix.error = "cancelled";
            session.stop();
            return fix;
        }
        const auto st = session.state();
        if (st.valid) {
            fix.valid = true;
            fix.lat = st.lat;
            fix.lon = st.lon;
            fix.alt_m = st.alt_m;
            fix.hdop = st.hdop;
            fix.sats = st.sats > 0 ? st.sats : st.sats_in_view;
            fix.quality = st.quality;
            fix.rmc_status = st.rmc_status;
            fix.time_utc = st.time_utc;
            fix.date_utc = st.date_utc;
            session.stop();
            return fix;
        }
        if (!st.last_error.empty() && !st.open && st.bytes == 0) {
            // Still starting.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session.stop();
    fix.error = "No GPS fix";
    return fix;
}

} // namespace platform

// kvxputerzero-wifimon — privileged helper for WiFi Analyzer monitor capture.
// Grant: sudo setcap cap_net_admin,cap_net_raw+ep <this binary>
//
// Usage: kvxputerzero-wifimon <iface>
// Stdin:  "C <1-11>\n" set channel, "Q\n" quit
// Stdout: "OK\n" then length-prefixed (u32 LE) radiotap+802.11 frames
//         or a single error line on failure.

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

bool iface_name_ok(const char* name) {
    if (!name || !*name) return false;
    const size_t n = std::strlen(name);
    if (n < 1 || n > 15) return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

int run_cmd(const std::string& cmd) {
    return ::system(cmd.c_str());
}

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string read_cmd(const std::string& cmd) {
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp)) out += buf;
    ::pclose(fp);
    return out;
}

std::string sysfs_driver(const std::string& ifname) {
    char path[256];
    std::snprintf(path, sizeof(path), "/sys/class/net/%s/device/driver", ifname.c_str());
    char link[512];
    const ssize_t n = ::readlink(path, link, sizeof(link) - 1);
    if (n <= 0) return {};
    link[n] = 0;
    const char* slash = std::strrchr(link, '/');
    return slash ? slash + 1 : link;
}

bool phy_has_monitor(const std::string& phy) {
    const std::string out = read_cmd("iw phy " + shell_quote(phy) + " info 2>/dev/null");
    bool in_modes = false;
    std::string line;
    for (size_t i = 0; i <= out.size(); ++i) {
        if (i == out.size() || out[i] == '\n') {
            if (line.find("Supported interface modes:") != std::string::npos) {
                in_modes = true;
            } else if (in_modes) {
                if (line.find("monitor") != std::string::npos) return true;
                if (!line.empty() && line[0] != '\t' && line[0] != ' ') in_modes = false;
            }
            line.clear();
        } else {
            line.push_back(out[i]);
        }
    }
    return false;
}

std::string iface_phy(const std::string& ifname) {
    const std::string out = read_cmd("iw dev " + shell_quote(ifname) + " info 2>/dev/null");
    // "wiphy 1" or look for phy#
    const auto p = out.find("wiphy ");
    if (p != std::string::npos) {
        int n = 0;
        if (std::sscanf(out.c_str() + p, "wiphy %d", &n) == 1) {
            return "phy" + std::to_string(n);
        }
    }
    return {};
}

struct Session {
    std::string requested;
    std::string capture; // may be kvxmon
    std::string phy;
    bool created_mon{false};
    bool unmanaged{false};
    int sock{-1};
};

void restore(Session& s) {
    if (s.sock >= 0) {
        ::close(s.sock);
        s.sock = -1;
    }
    if (!s.capture.empty()) {
        run_cmd("ip link set " + shell_quote(s.capture) + " down 2>/dev/null");
        if (s.created_mon) {
            run_cmd("iw dev " + shell_quote(s.capture) + " del 2>/dev/null");
        } else {
            run_cmd("iw dev " + shell_quote(s.capture) + " set type managed 2>/dev/null");
            run_cmd("ip link set " + shell_quote(s.capture) + " up 2>/dev/null");
        }
    }
    if (s.unmanaged && !s.requested.empty()) {
        run_cmd("nmcli device set " + shell_quote(s.requested) + " managed yes 2>/dev/null");
        s.unmanaged = false;
    }
}

bool setup(Session& s, std::string& err) {
    const std::string driver = sysfs_driver(s.requested);
    if (driver == "brcmfmac") {
        err = "onboard brcmfmac blocked";
        return false;
    }
    s.phy = iface_phy(s.requested);
    if (s.phy.empty()) {
        err = "cannot resolve phy";
        return false;
    }
    if (!phy_has_monitor(s.phy)) {
        err = "phy lacks monitor mode";
        return false;
    }

    run_cmd("nmcli device set " + shell_quote(s.requested) + " managed no 2>/dev/null");
    s.unmanaged = true;

    run_cmd("ip link set " + shell_quote(s.requested) + " down 2>/dev/null");
    if (run_cmd("iw dev " + shell_quote(s.requested) + " set type monitor 2>/dev/null") == 0) {
        s.capture = s.requested;
        s.created_mon = false;
    } else {
        // Create dedicated monitor iface
        run_cmd("iw phy " + shell_quote(s.phy) + " interface add kvxmon type monitor 2>/dev/null");
        s.capture = "kvxmon";
        s.created_mon = true;
        // Bring original back up managed? Leave down while session runs; restore handles it.
    }

    if (run_cmd("ip link set " + shell_quote(s.capture) + " up 2>/dev/null") != 0) {
        err = "failed to bring monitor iface up";
        return false;
    }

    const int ifindex = static_cast<int>(::if_nametoindex(s.capture.c_str()));
    if (ifindex == 0) {
        err = "if_nametoindex failed";
        return false;
    }

    s.sock = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (s.sock < 0) {
        err = "AF_PACKET denied (need cap_net_raw). sudo setcap cap_net_admin,cap_net_raw+ep "
              "/usr/lib/kvxputerzero/kvxputerzero-wifimon";
        return false;
    }

    sockaddr_ll addr{};
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = ifindex;
    if (::bind(s.sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        err = "bind failed";
        return false;
    }

    const int flags = ::fcntl(s.sock, F_GETFL, 0);
    ::fcntl(s.sock, F_SETFL, flags | O_NONBLOCK);
    return true;
}

bool set_channel(Session& s, int ch) {
    if (ch < 1 || ch > 11) return false;
    const std::string c =
        "iw dev " + shell_quote(s.capture) + " set channel " + std::to_string(ch) + " HT20 2>/dev/null";
    return run_cmd(c) == 0;
}

bool write_all(int fd, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t left = len;
    while (left) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || !iface_name_ok(argv[1])) {
        std::fprintf(stderr, "usage: kvxputerzero-wifimon <iface>\n");
        return 2;
    }

    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    Session s;
    s.requested = argv[1];
    std::string err;
    if (!setup(s, err)) {
        std::printf("%s\n", err.c_str());
        std::fflush(stdout);
        restore(s);
        return 1;
    }
    std::printf("OK\n");
    std::fflush(stdout);

    // Non-blocking stdin
    {
        const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
        ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    std::string cmd_buf;
    uint8_t pkt[4096];

    while (!g_stop) {
        pollfd fds[2]{};
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = s.sock;
        fds[1].events = POLLIN;

        const int pr = ::poll(fds, 2, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[128];
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n == 0) break; // EOF
            if (n > 0) {
                cmd_buf.append(buf, buf + n);
                size_t pos;
                while ((pos = cmd_buf.find('\n')) != std::string::npos) {
                    std::string line = cmd_buf.substr(0, pos);
                    cmd_buf.erase(0, pos + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    if (line == "Q" || line == "q") {
                        g_stop = 1;
                        break;
                    }
                    if (line.size() >= 3 && (line[0] == 'C' || line[0] == 'c') && line[1] == ' ') {
                        int ch = 0;
                        if (std::sscanf(line.c_str() + 2, "%d", &ch) == 1) set_channel(s, ch);
                    }
                }
            }
        }

        if (g_stop) break;

        if (fds[1].revents & POLLIN) {
            for (;;) {
                const ssize_t n = ::recv(s.sock, pkt, sizeof(pkt), 0);
                if (n <= 0) break;
                const uint32_t len = static_cast<uint32_t>(n);
                uint8_t hdr[4] = {
                    static_cast<uint8_t>(len & 0xff),
                    static_cast<uint8_t>((len >> 8) & 0xff),
                    static_cast<uint8_t>((len >> 16) & 0xff),
                    static_cast<uint8_t>((len >> 24) & 0xff),
                };
                if (!write_all(STDOUT_FILENO, hdr, 4)) {
                    g_stop = 1;
                    break;
                }
                if (!write_all(STDOUT_FILENO, pkt, static_cast<size_t>(n))) {
                    g_stop = 1;
                    break;
                }
            }
        }
    }

    restore(s);
    return 0;
}

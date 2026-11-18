#include "tcp_probe.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

int set_nonblock(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace

TcpProbeResult tcp_probe(const std::string& host, uint16_t port, int timeout_ms, bool grab_banner,
                         size_t banner_max) {
    TcpProbeResult out;
    if (host.empty() || port == 0) {
        out.error = "bad host/port";
        return out;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    const int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        out.error = gai_strerror(gai);
        return out;
    }

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (set_nonblock(fd) < 0) {
            ::close(fd);
            fd = -1;
            continue;
        }
        const int rc = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (rc == 0) {
            out.open = true;
            break;
        }
        if (errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            timeval tv{};
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            const int sel = ::select(fd + 1, nullptr, &wset, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wset)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
                    out.open = true;
                    break;
                }
            }
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (!out.open) {
        if (out.error.empty()) out.error = "closed / filtered";
        if (fd >= 0) ::close(fd);
        return out;
    }

    if (grab_banner && fd >= 0) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (::select(fd + 1, &rset, nullptr, nullptr, &tv) > 0) {
            std::vector<char> buf(banner_max);
            const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
            if (n > 0) out.banner.assign(buf.data(), static_cast<size_t>(n));
        }
    }
    if (fd >= 0) ::close(fd);
    return out;
}

} // namespace platform

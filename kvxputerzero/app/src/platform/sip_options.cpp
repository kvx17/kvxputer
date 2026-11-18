#include "sip_options.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <sstream>
#include <string>

namespace platform {

SipOptionsResult sip_options(const std::string& host, int timeout_ms, bool* cancel) {
    SipOptionsResult r;
    if (host.empty()) {
        r.error = "host required";
        return r;
    }

    addrinfo hints{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), "5060", &hints, &res) != 0 || !res) {
        r.error = "SIP resolve failed";
        return r;
    }

    int fd = -1;
    addrinfo* chosen = nullptr;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0) {
            chosen = ai;
            break;
        }
    }
    if (fd < 0 || !chosen) {
        freeaddrinfo(res);
        r.error = "SIP socket failed";
        return r;
    }
    const int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    sockaddr_storage local{};
    socklen_t local_len = sizeof(local);
    // Bind ephemeral.
    if (::bind(fd, chosen->ai_addr, 0) != 0) {
        // bind with zero length addr is invalid; use INADDR_ANY
        if (chosen->ai_family == AF_INET) {
            sockaddr_in any{};
            any.sin_family = AF_INET;
            any.sin_addr.s_addr = INADDR_ANY;
            any.sin_port = 0;
            ::bind(fd, reinterpret_cast<sockaddr*>(&any), sizeof(any));
        } else {
            sockaddr_in6 any6{};
            any6.sin6_family = AF_INET6;
            any6.sin6_port = 0;
            ::bind(fd, reinterpret_cast<sockaddr*>(&any6), sizeof(any6));
        }
    }
    getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_len);

    std::string local_ip = "127.0.0.1";
    if (local.ss_family == AF_INET) {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&local)->sin_addr, buf, sizeof(buf));
        local_ip = buf;
    }

    std::ostringstream req;
    req << "OPTIONS sip:100@" << host << " SIP/2.0\r\n"
        << "Via: SIP/2.0/UDP " << local_ip << ";branch=z9hG4bKkvx\r\n"
        << "From: <sip:kvxputer@" << local_ip << ">;tag=kvx\r\n"
        << "To: <sip:100@" << host << ">\r\n"
        << "Call-ID: kvx-" << std::to_string(static_cast<long long>(
                                  std::chrono::steady_clock::now().time_since_epoch().count()))
        << "@kvxputer\r\n"
        << "CSeq: 1 OPTIONS\r\n"
        << "Contact: <sip:kvxputer@" << local_ip << ">\r\n"
        << "Max-Forwards: 70\r\n"
        << "Content-Length: 0\r\n\r\n";
    const std::string payload = req.str();
    if (::sendto(fd, payload.data(), payload.size(), 0, chosen->ai_addr, chosen->ai_addrlen) < 0) {
        ::close(fd);
        freeaddrinfo(res);
        r.error = "SIP send failed";
        return r;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[512];
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            r.status_line = "cancelled";
            r.ok = true;
            ::close(fd);
            freeaddrinfo(res);
            return r;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv{0, 100000};
        if (select(fd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            sockaddr_storage from{};
            socklen_t fromlen = sizeof(from);
            const ssize_t n = ::recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromlen);
            if (n > 0) {
                buf[n] = 0;
                std::string resp(buf, static_cast<size_t>(n));
                const auto eol = resp.find("\r\n");
                r.status_line = eol == std::string::npos ? resp.substr(0, 80) : resp.substr(0, eol);
                r.ok = true;
                ::close(fd);
                freeaddrinfo(res);
                return r;
            }
        }
    }
    ::close(fd);
    freeaddrinfo(res);
    r.status_line = "no reply";
    r.ok = true;
    return r;
}

} // namespace platform

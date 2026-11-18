#include "http_get.h"

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace platform {
namespace {

bool parse_url(const std::string& url, std::string& scheme, std::string& host, uint16_t& port,
               std::string& path) {
    scheme.clear();
    host.clear();
    path = "/";
    port = 80;
    auto pos = url.find("://");
    if (pos == std::string::npos) return false;
    scheme = url.substr(0, pos);
    std::string rest = url.substr(pos + 3);
    auto slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    path = slash == std::string::npos ? "/" : rest.substr(slash);
    auto colon = authority.find(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        try {
            port = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
        } catch (...) {
            return false;
        }
    } else {
        host = authority;
        if (scheme == "https") port = 443;
    }
    return !host.empty();
}

} // namespace

HttpResult http_request(const std::string& method, const std::string& url, const std::string& body,
                        int timeout_sec, size_t body_cap) {
    HttpResult out;
    std::string scheme, host, path;
    uint16_t port = 80;
    if (!parse_url(url, scheme, host, port, path)) {
        out.error = "bad URL";
        return out;
    }
    if (scheme == "https") {
        out.error = "HTTPS unavailable";
        return out;
    }
    if (scheme != "http") {
        out.error = "unsupported scheme";
        return out;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        out.error = "DNS failed";
        return out;
    }
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        timeval tv{};
        tv.tv_sec = timeout_sec;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        out.error = "connect failed";
        return out;
    }

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\nHost: " << host << "\r\nConnection: close\r\n";
    if (!body.empty()) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    if (!body.empty()) req << body;
    const std::string req_s = req.str();
    if (::send(fd, req_s.data(), req_s.size(), 0) < 0) {
        ::close(fd);
        out.error = "send failed";
        return out;
    }

    std::string raw;
    char buf[1024];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > body_cap + 8192) break;
    }
    ::close(fd);

    const auto hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        out.error = "bad response";
        return out;
    }
    out.headers = raw.substr(0, hdr_end);
    out.body = raw.substr(hdr_end + 4);
    if (out.body.size() > body_cap) out.body.resize(body_cap);
    if (out.headers.rfind("HTTP/", 0) == 0) {
        const auto sp = out.headers.find(' ');
        if (sp != std::string::npos) {
            try {
                out.status = std::stoi(out.headers.substr(sp + 1));
            } catch (...) {
            }
        }
    }
    return out;
}

} // namespace platform

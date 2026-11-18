#include "ldap_read.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <thread>
#include <vector>

namespace platform {
namespace {

void ber_len(std::vector<uint8_t>& out, size_t len) {
    if (len < 128) {
        out.push_back(static_cast<uint8_t>(len));
        return;
    }
    out.push_back(0x81);
    out.push_back(static_cast<uint8_t>(len));
}

std::vector<uint8_t> bind_simple(const std::string& dn, const std::string& pw) {
    std::vector<uint8_t> inner;
    inner.push_back(0x02);
    inner.push_back(0x01);
    inner.push_back(0x03); // version 3
    inner.push_back(0x04);
    ber_len(inner, dn.size());
    inner.insert(inner.end(), dn.begin(), dn.end());
    inner.push_back(0x80);
    ber_len(inner, pw.size());
    inner.insert(inner.end(), pw.begin(), pw.end());

    std::vector<uint8_t> bind;
    bind.push_back(0x60);
    ber_len(bind, inner.size());
    bind.insert(bind.end(), inner.begin(), inner.end());

    std::vector<uint8_t> msg;
    msg.push_back(0x30);
    const size_t seq = 3 + bind.size();
    ber_len(msg, seq);
    msg.push_back(0x02);
    msg.push_back(0x01);
    msg.push_back(0x01);
    msg.insert(msg.end(), bind.begin(), bind.end());
    return msg;
}

std::vector<uint8_t> search_req(const std::string& base) {
    static const uint8_t tail[] = {0x0a, 0x01, 0x02, 0x0a, 0x01, 0x00, 0x02, 0x01, 0x14, 0x02, 0x01, 0x00,
                                   0x01, 0x01, 0x00, 0x87, 0x0b, 'o',  'b',  'j',  'e',  'c',  't',  'C',
                                   'l',  'a',  's',  's',  0x30, 0x00};
    std::vector<uint8_t> inner;
    inner.push_back(0x04);
    ber_len(inner, base.size());
    inner.insert(inner.end(), base.begin(), base.end());
    inner.insert(inner.end(), std::begin(tail), std::end(tail));

    std::vector<uint8_t> search;
    search.push_back(0x63);
    ber_len(search, inner.size());
    search.insert(search.end(), inner.begin(), inner.end());

    std::vector<uint8_t> msg;
    msg.push_back(0x30);
    ber_len(msg, 3 + search.size());
    msg.push_back(0x02);
    msg.push_back(0x01);
    msg.push_back(0x02);
    msg.insert(msg.end(), search.begin(), search.end());
    return msg;
}

bool connect_tcp(const std::string& host, uint16_t port, int timeout_ms, int& fd_out, std::string& err) {
    fd_out = -1;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    const int gai = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (gai != 0 || !res) {
        err = "LDAP resolve failed";
        return false;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        const int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        if (ai->ai_family == AF_INET) {
            reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_port = htons(port);
        } else if (ai->ai_family == AF_INET6) {
            reinterpret_cast<sockaddr_in6*>(ai->ai_addr)->sin6_port = htons(port);
        }
        const int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) break;
        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            if (select(fd + 1, nullptr, &wfds, nullptr, &tv) > 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr == 0) break;
            }
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        err = "LDAP connect failed";
        return false;
    }
    fd_out = fd;
    return true;
}

bool is_password_attr(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (unsigned char c : name) lower.push_back(static_cast<char>(std::tolower(c)));
    return lower == "userpassword" || lower == "unicodepwd";
}

void extract_printable(const std::vector<uint8_t>& buf, std::vector<std::string>& lines, int& entries) {
    // Best-effort: scan for printable OCTET STRING-ish runs after tag 0x04.
    std::string pending_attr;
    for (size_t i = 0; i + 2 < buf.size(); ++i) {
        if (buf[i] != 0x04) continue;
        size_t len = buf[i + 1];
        size_t off = i + 2;
        if (len & 0x80) {
            const size_t n = len & 0x7f;
            if (n == 0 || n > 2 || off + n > buf.size()) continue;
            len = 0;
            for (size_t k = 0; k < n; ++k) len = (len << 8) | buf[off++];
        }
        if (off + len > buf.size() || len == 0 || len > 256) continue;
        std::string s;
        s.reserve(len);
        bool printable = true;
        for (size_t k = 0; k < len; ++k) {
            const unsigned char c = buf[off + k];
            if (c < 32 || c > 126) {
                printable = false;
                break;
            }
            s.push_back(static_cast<char>(c));
        }
        if (!printable || s.empty()) continue;
        if (pending_attr.empty()) {
            pending_attr = s;
        } else {
            if (!is_password_attr(pending_attr)) {
                lines.push_back(pending_attr + ": " + s);
                if (pending_attr == "dn" || pending_attr == "objectClass") ++entries;
            }
            pending_attr.clear();
        }
        i = off + len - 1;
        if (static_cast<int>(lines.size()) > 200) return;
    }
}

} // namespace

LdapReadResult ldap_read(const std::string& host, const std::string& base_dn, const std::string& bind_dn,
                         const std::string& password, int timeout_ms, bool* cancel) {
    LdapReadResult r;
    if (host.empty()) {
        r.error = "host required";
        return r;
    }
    int fd = -1;
    if (!connect_tcp(host, 389, timeout_ms, fd, r.error)) return r;

    const auto bind = bind_simple(bind_dn, password);
    if (::write(fd, bind.data(), bind.size()) < 0) {
        ::close(fd);
        r.error = "LDAP write failed";
        return r;
    }

    auto read_for = [&](int ms, std::vector<uint8_t>& out) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        char tmp[1024];
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && *cancel) return;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            timeval tv{0, 100000};
            if (select(fd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
                const ssize_t n = ::read(fd, tmp, sizeof(tmp));
                if (n > 0) out.insert(out.end(), tmp, tmp + n);
                else if (n == 0) return;
            }
            if (out.size() > 64 * 1024) return;
        }
    };

    std::vector<uint8_t> resp;
    read_for(1500, resp);

    const auto search = search_req(base_dn.empty() ? std::string("dc=local,dc=lan") : base_dn);
    if (::write(fd, search.data(), search.size()) < 0) {
        ::close(fd);
        r.error = "LDAP write failed";
        return r;
    }
    read_for(timeout_ms, resp);
    ::close(fd);

    int entries = 0;
    extract_printable(resp, r.lines, entries);
    if (r.lines.empty()) r.lines.push_back("no printable attributes (or empty result)");
    if (static_cast<int>(r.lines.size()) > 20) r.lines.resize(20);
    r.ok = true;
    return r;
}

} // namespace platform

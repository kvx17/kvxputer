#include "dns_query.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

std::string system_resolver() {
    std::ifstream in("/etc/resolv.conf");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("nameserver", 0) == 0) {
            std::string ip = line.substr(10);
            while (!ip.empty() && ip.front() == ' ') ip.erase(ip.begin());
            while (!ip.empty() && (ip.back() == '\r' || ip.back() == ' ')) ip.pop_back();
            if (!ip.empty()) return ip;
        }
    }
    return "8.8.8.8";
}

void encode_name(std::vector<uint8_t>& out, const std::string& name) {
    size_t i = 0;
    while (i < name.size()) {
        size_t j = name.find('.', i);
        if (j == std::string::npos) j = name.size();
        const size_t len = j - i;
        out.push_back(static_cast<uint8_t>(len));
        for (size_t k = i; k < j; ++k) out.push_back(static_cast<uint8_t>(name[k]));
        i = j + 1;
    }
    out.push_back(0);
}

} // namespace

bool dns_query(const std::string& name, DnsType type, const std::string& resolver_ip,
               std::vector<DnsAnswer>& answers, std::string& error) {
    answers.clear();
    error.clear();
    if (name.empty()) {
        error = "empty name";
        return false;
    }
    const std::string resolver = resolver_ip.empty() ? system_resolver() : resolver_ip;

    std::vector<uint8_t> pkt;
    pkt.push_back(0x12);
    pkt.push_back(0x34); // id
    pkt.push_back(0x01);
    pkt.push_back(0x00); // recursion desired
    pkt.push_back(0x00);
    pkt.push_back(0x01); // qdcount
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    encode_name(pkt, name);
    pkt.push_back(0x00);
    pkt.push_back(static_cast<uint8_t>(static_cast<uint16_t>(type) >> 8));
    pkt.push_back(static_cast<uint8_t>(static_cast<uint16_t>(type) & 0xff));
    pkt.push_back(0x00);
    pkt.push_back(0x01); // IN

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error = "socket fail";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    if (inet_pton(AF_INET, resolver.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        error = "bad resolver";
        return false;
    }
    timeval tv{};
    tv.tv_sec = 3;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        error = "send fail";
        return false;
    }
    uint8_t buf[2048];
    const ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, nullptr, nullptr);
    ::close(fd);
    if (n < 12) {
        error = "no response";
        return false;
    }

    const uint16_t ancount = (static_cast<uint16_t>(buf[6]) << 8) | buf[7];
    // Skip question
    size_t off = 12;
    while (off < static_cast<size_t>(n) && buf[off] != 0) {
        if ((buf[off] & 0xC0) == 0xC0) {
            off += 2;
            break;
        }
        off += 1 + buf[off];
    }
    if (off < static_cast<size_t>(n) && buf[off] == 0) ++off;
    off += 4; // qtype+qclass

    for (uint16_t i = 0; i < ancount && off + 10 <= static_cast<size_t>(n); ++i) {
        if ((buf[off] & 0xC0) == 0xC0) off += 2;
        else {
            while (off < static_cast<size_t>(n) && buf[off] != 0) {
                if ((buf[off] & 0xC0) == 0xC0) {
                    off += 2;
                    break;
                }
                off += 1 + buf[off];
            }
            if (off < static_cast<size_t>(n) && buf[off] == 0) ++off;
        }
        if (off + 10 > static_cast<size_t>(n)) break;
        const uint16_t rtype = (static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1];
        const uint16_t rdlen = (static_cast<uint16_t>(buf[off + 8]) << 8) | buf[off + 9];
        off += 10;
        if (off + rdlen > static_cast<size_t>(n)) break;
        DnsAnswer ans;
        if (rtype == 1 && rdlen == 4) {
            char ip[64];
            snprintf(ip, sizeof(ip), "%u.%u.%u.%u", buf[off], buf[off + 1], buf[off + 2], buf[off + 3]);
            ans.text = std::string("A ") + ip;
        } else if (rtype == 28 && rdlen == 16) {
            char ip[128];
            inet_ntop(AF_INET6, &buf[off], ip, sizeof(ip));
            ans.text = std::string("AAAA ") + ip;
        } else if (rtype == 16) {
            // TXT
            std::string t;
            size_t p = off;
            while (p < off + rdlen) {
                const uint8_t len = buf[p++];
                if (p + len > off + rdlen) break;
                t.append(reinterpret_cast<char*>(&buf[p]), len);
                p += len;
            }
            ans.text = "TXT " + t;
        } else if (rtype == 5) {
            ans.text = "CNAME";
        } else if (rtype == 15 && rdlen >= 2) {
            const uint16_t pref = (static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1];
            ans.text = "MX " + std::to_string(pref);
        } else {
            ans.text = "type " + std::to_string(rtype) + " len " + std::to_string(rdlen);
        }
        answers.push_back(std::move(ans));
        off += rdlen;
    }
    if (answers.empty()) answers.push_back({"(no answers)"});
    return true;
}

} // namespace platform

#include "roku_ecp.h"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace tools::roku {
namespace {

struct HttpResult {
    int status{-1};
    std::string body;
    std::string error;
};

HttpResult http_ecp(const std::string& method, const std::string& ip, const std::string& path, int timeout_sec,
                    size_t body_cap) {
    HttpResult out;
    if (ip.empty()) {
        out.error = "no ip";
        return out;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = "8060";
    if (getaddrinfo(ip.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
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
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << ip << "\r\n";
    req << "User-Agent: kvxputerzero\r\n";
    req << "Connection: close\r\n";
    if (method == "POST" || method == "PUT") {
        req << "Content-Length: 0\r\n";
    }
    req << "\r\n";
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
    const std::string headers = raw.substr(0, hdr_end);
    out.body = raw.substr(hdr_end + 4);
    if (out.body.size() > body_cap) out.body.resize(body_cap);
    if (headers.rfind("HTTP/", 0) == 0) {
        const auto sp = headers.find(' ');
        if (sp != std::string::npos) {
            try {
                out.status = std::stoi(headers.substr(sp + 1));
            } catch (...) {
            }
        }
    }
    return out;
}

std::string scrape_tag(const std::string& xml, const char* tag) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const auto a = xml.find(open);
    if (a == std::string::npos) return {};
    const auto start = a + open.size();
    const auto b = xml.find(close, start);
    if (b == std::string::npos) return {};
    std::string v = xml.substr(start, b - start);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) {
        v.erase(v.begin());
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) {
        v.pop_back();
    }
    return v;
}

std::string header_value(const std::string& text, const char* name) {
    std::string lower_name = name;
    for (char& c : lower_name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    lower_name += ':';
    std::string lower = text;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const auto i = lower.find(lower_name);
    if (i == std::string::npos) return {};
    auto start = text.find(':', i);
    if (start == std::string::npos) return {};
    ++start;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    auto end = text.find_first_of("\r\n", start);
    if (end == std::string::npos) end = text.size();
    return text.substr(start, end - start);
}

std::string extract_ip_from_location(const std::string& loc) {
    auto start = loc.find("://");
    if (start == std::string::npos) return {};
    start += 3;
    auto end = loc.find(':', start);
    const auto slash = loc.find('/', start);
    if (end == std::string::npos || (slash != std::string::npos && slash < end)) end = slash;
    if (end == std::string::npos) end = loc.size();
    return loc.substr(start, end - start);
}

Status map_http(const HttpResult& r) {
    if (!r.error.empty()) return Status::Unreachable;
    if (r.status == 403) return Status::Forbidden;
    if (r.status >= 200 && r.status < 300) return Status::Ok;
    return Status::Unreachable;
}

Status post_key_path(const std::string& ip, const std::string& kind, const std::string& key, int timeout_sec) {
    if (ip.empty() || key.empty()) return Status::Unreachable;
    const std::string path = "/" + kind + "/" + key;
    return map_http(http_ecp("POST", ip, path, timeout_sec, 256));
}

void scrape_apps(const std::string& xml, std::vector<App>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < xml.size() && out.size() < 200) {
        const auto a = xml.find("<app", pos);
        if (a == std::string::npos) break;
        const auto id_attr = xml.find("id=\"", a);
        const auto tag_end = xml.find('>', a);
        if (id_attr == std::string::npos || tag_end == std::string::npos || id_attr > tag_end) {
            pos = a + 4;
            continue;
        }
        const auto id_start = id_attr + 4;
        const auto id_end = xml.find('"', id_start);
        if (id_end == std::string::npos) break;
        const std::string id = xml.substr(id_start, id_end - id_start);
        const auto name_start = tag_end + 1;
        const auto name_end = xml.find("</app>", name_start);
        if (name_end == std::string::npos) break;
        std::string name = xml.substr(name_start, name_end - name_start);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\n' || name.front() == '\r')) {
            name.erase(name.begin());
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '\n' || name.back() == '\r')) {
            name.pop_back();
        }
        if (!id.empty() && !name.empty()) out.push_back({id, name});
        pos = name_end + 6;
    }
}

void scrape_destinations(const std::string& xml, std::vector<std::string>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < xml.size() && out.size() < 16) {
        const auto a = xml.find("<name>", pos);
        if (a == std::string::npos) break;
        const auto start = a + 6;
        const auto b = xml.find("</name>", start);
        if (b == std::string::npos) break;
        std::string n = xml.substr(start, b - start);
        if (!n.empty()) out.push_back(n);
        pos = b + 7;
    }
}

} // namespace

const char* status_message(Status st) {
    switch (st) {
    case Status::Ok:
        return "OK";
    case Status::Forbidden:
        return "Enable Control by mobile apps";
    case Status::Cancelled:
        return "Cancelled";
    case Status::Unreachable:
    default:
        return "Roku unreachable";
    }
}

std::string lit_key(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return std::string("Lit_") + c;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Lit_%%%02X", static_cast<unsigned char>(c));
    return buf;
}

std::vector<Device> ssdp_discover(int timeout_ms, const std::atomic<bool>* cancel) {
    std::vector<Device> devices;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return devices;

    int ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);

    static const char kMsg[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: roku:ecp\r\n"
        "\r\n";
    if (::sendto(fd, kMsg, sizeof(kMsg) - 1, 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) < 0) {
        ::close(fd);
        return devices;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::set<std::string> seen;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && cancel->load()) break;
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 200) <= 0) continue;
        char buf[2048];
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const ssize_t n = ::recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        buf[n] = '\0';
        const std::string text(buf, static_cast<size_t>(n));
        std::string loc = header_value(text, "LOCATION");
        if (loc.empty()) loc = header_value(text, "Location");
        std::string ip = extract_ip_from_location(loc);
        if (ip.empty()) {
            char ipbuf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
            ip = ipbuf;
        }
        if (ip.empty() || !seen.insert(ip).second) continue;
        Device d;
        d.ip = ip;
        d.name = ip;
        d.serial = header_value(text, "USN");
        devices.push_back(std::move(d));
    }
    ::close(fd);
    return devices;
}

Status query_device_info(const std::string& ip, Device& out, int timeout_sec) {
    if (ip.empty()) return Status::Unreachable;
    const auto r = http_ecp("GET", ip, "/query/device-info", timeout_sec, 8192);
    const Status st = map_http(r);
    if (st != Status::Ok) return st;
    out.ip = ip;
    out.name = scrape_tag(r.body, "user-device-name");
    if (out.name.empty()) out.name = scrape_tag(r.body, "friendly-device-name");
    if (out.name.empty()) out.name = ip;
    out.model = scrape_tag(r.body, "model-name");
    out.serial = scrape_tag(r.body, "serial-number");
    const std::string fr = scrape_tag(r.body, "supports-find-remote");
    out.supports_find_remote = (fr == "true" || fr == "True" || fr == "1");
    const std::string hp = scrape_tag(r.body, "headphones-connected");
    out.headphones_connected = (hp == "true" || hp == "True" || hp == "1");
    return Status::Ok;
}

Status query_apps(const std::string& ip, std::vector<App>& out, int timeout_sec) {
    if (ip.empty()) return Status::Unreachable;
    const auto r = http_ecp("GET", ip, "/query/apps", timeout_sec, 65536);
    const Status st = map_http(r);
    if (st != Status::Ok) return st;
    scrape_apps(r.body, out);
    return Status::Ok;
}

Status query_audio_device(const std::string& ip, AudioInfo& out, int timeout_sec) {
    out = {};
    if (ip.empty()) return Status::Unreachable;
    const auto r = http_ecp("GET", ip, "/query/audio-device", timeout_sec, 8192);
    const Status st = map_http(r);
    if (st != Status::Ok) {
        out.raw_note = status_message(st);
        return st;
    }
    const std::string muted = scrape_tag(r.body, "muted");
    out.muted = (muted == "true" || muted == "True" || muted == "1");
    const std::string vol = scrape_tag(r.body, "volume");
    if (!vol.empty()) {
        try {
            out.volume = std::stoi(vol);
        } catch (...) {
            out.volume = -1;
        }
    }
    scrape_destinations(r.body, out.destinations);
    return Status::Ok;
}

Status launch_app(const std::string& ip, const std::string& app_id, int timeout_sec) {
    if (ip.empty() || app_id.empty()) return Status::Unreachable;
    return map_http(http_ecp("POST", ip, "/launch/" + app_id, timeout_sec, 256));
}

Status keypress(const std::string& ip, const std::string& key, int timeout_sec) {
    return post_key_path(ip, "keypress", key, timeout_sec);
}

Status keydown(const std::string& ip, const std::string& key, int timeout_sec) {
    return post_key_path(ip, "keydown", key, timeout_sec);
}

Status keyup(const std::string& ip, const std::string& key, int timeout_sec) {
    return post_key_path(ip, "keyup", key, timeout_sec);
}

CommandQueue::CommandQueue() : worker_([this]() { worker_loop(); }) {}

CommandQueue::~CommandQueue() {
    release_held();
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CommandQueue::set_ip(std::string ip) {
    release_held();
    std::lock_guard<std::mutex> lock(mu_);
    ip_ = std::move(ip);
}

std::string CommandQueue::ip() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ip_;
}

void CommandQueue::set_result_callback(ResultFn cb) {
    std::lock_guard<std::mutex> lock(mu_);
    result_cb_ = std::move(cb);
}

void CommandQueue::push(Job job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_) return;
        if (job.kind == Kind::Down) {
            held_.push_back(job.key);
        } else if (job.kind == Kind::Up) {
            for (auto it = held_.begin(); it != held_.end(); ++it) {
                if (*it == job.key) {
                    held_.erase(it);
                    break;
                }
            }
        }
        q_.push(std::move(job));
    }
    cv_.notify_one();
}

void CommandQueue::enqueue_press(std::string key) {
    push({Kind::Press, std::move(key)});
}

void CommandQueue::enqueue_down(std::string key) {
    push({Kind::Down, std::move(key)});
}

void CommandQueue::enqueue_up(std::string key) {
    push({Kind::Up, std::move(key)});
}

void CommandQueue::release_held() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& k : held_) {
            q_.push({Kind::Up, k});
        }
        held_.clear();
    }
    cv_.notify_one();
}

void CommandQueue::worker_loop() {
    while (true) {
        Job job;
        std::string ip;
        ResultFn cb;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]() { return stop_ || !q_.empty(); });
            if (stop_ && q_.empty()) return;
            job = std::move(q_.front());
            q_.pop();
            ip = ip_;
            cb = result_cb_;
        }
        if (ip.empty()) continue;
        Status st = Status::Unreachable;
        switch (job.kind) {
        case Kind::Press:
            st = keypress(ip, job.key);
            break;
        case Kind::Down:
            st = keydown(ip, job.key);
            break;
        case Kind::Up:
            st = keyup(ip, job.key);
            break;
        }
        if (cb && job.kind != Kind::Up) {
            cb(job.key, st);
        }
    }
}

} // namespace tools::roku

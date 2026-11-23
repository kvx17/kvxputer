#include "file_share_server.h"

#include "file_share_page.h"
#include "fs_root.h"
#include "net_info.h"
#include "user_paths.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace tools::file_share {
namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxUpload = 64u * 1024u * 1024u;

std::string sfmt(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return {};
    if (static_cast<size_t>(n) < sizeof(buf)) return std::string(buf, static_cast<size_t>(n));
    std::string out(static_cast<size_t>(n), '\0');
    va_list ap2;
    va_start(ap2, fmt);
    vsnprintf(out.data(), out.size() + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

void set_nonblock(int fd, bool on) {
    const int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return;
    fcntl(fd, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
}

int listen_on(const std::string& addr, uint16_t port, std::string& err) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::strerror(errno);
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
        err = "bad address";
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || ::listen(fd, 16) != 0) {
        err = std::strerror(errno);
        ::close(fd);
        return -1;
    }
    set_nonblock(fd, true);
    return fd;
}

bool send_all(int fd, const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool send_all(int fd, const std::string& s) {
    return send_all(fd, s.data(), s.size());
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

std::string header_value(const std::string& text, const std::string& name) {
    const std::string needle = name + ":";
    for (const auto& line : split_lines(text)) {
        if (line.size() < needle.size()) continue;
        bool match = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(line[i])) !=
                std::tolower(static_cast<unsigned char>(needle[i]))) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        size_t i = needle.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        return line.substr(i);
    }
    return {};
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string url_encode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out.push_back(static_cast<char>(c));
        } else {
            out += sfmt("%%%02X", c);
        }
    }
    return out;
}

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c);
        }
    }
    return out;
}

bool has_hidden_component(const std::string& rel) {
    std::stringstream ss(rel);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty() && part[0] == '.') return true;
    }
    return false;
}

void send_response(int fd, int code, const char* reason, const std::string& ctype, const std::string& body,
                   bool head_only = false, const std::string& extra_headers = {}) {
    std::string h = sfmt("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                         "X-Content-Type-Options: nosniff\r\nConnection: close\r\n",
                         code, reason, ctype.c_str(), body.size());
    if (!extra_headers.empty()) h += extra_headers;
    h += "\r\n";
    send_all(fd, h);
    if (!head_only) send_all(fd, body);
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) out += sfmt("\\u%04x", c);
            else out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

std::string query_param(const std::string& target_with_q, const std::string& key) {
    const auto q = target_with_q.find('?');
    if (q == std::string::npos) return {};
    std::string qs = target_with_q.substr(q + 1);
    const std::string needle = key + "=";
    size_t start = 0;
    while (start < qs.size()) {
        size_t amp = qs.find('&', start);
        if (amp == std::string::npos) amp = qs.size();
        const std::string part = qs.substr(start, amp - start);
        if (part.rfind(needle, 0) == 0) return url_decode(part.substr(needle.size()));
        start = amp + 1;
    }
    return {};
}

std::string ext_lower(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = name.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

const char* content_type_for(const std::string& name) {
    const std::string e = ext_lower(name);
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif") return "image/gif";
    if (e == "webp") return "image/webp";
    if (e == "bmp") return "image/bmp";
    if (e == "svg") return "image/svg+xml";
    if (e == "html" || e == "htm") return "text/html; charset=utf-8";
    if (e == "css") return "text/css; charset=utf-8";
    if (e == "js") return "text/javascript; charset=utf-8";
    if (e == "json") return "application/json; charset=utf-8";
    if (e == "xml") return "application/xml; charset=utf-8";
    if (e == "md" || e == "markdown" || e == "txt" || e == "log" || e == "csv" || e == "yml" || e == "yaml" ||
        e == "toml" || e == "ini" || e == "cfg" || e == "conf" || e == "c" || e == "h" || e == "cpp" || e == "hpp" ||
        e == "py" || e == "rs" || e == "go" || e == "sh" || e == "bat" || e == "env") {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

bool is_text_ext(const std::string& name) {
    const char* ct = content_type_for(name);
    return std::strncmp(ct, "text/", 5) == 0 || std::strstr(ct, "json") || std::strstr(ct, "xml");
}

bool is_image_ext(const std::string& name) {
    return std::strncmp(content_type_for(name), "image/", 6) == 0;
}

std::string normalize_rel(const std::string& path_in) {
    std::string p = path_in;
    if (p.empty()) return {};
    if (p[0] == '/') p = p.substr(1);
    while (!p.empty() && p.front() == '/') p.erase(p.begin());
    return p;
}

std::string read_body(int fd, size_t content_length, const std::string& already, std::atomic<bool>& cancel,
                      size_t max_len = 1024 * 1024) {
    if (content_length > max_len) return {};
    std::string body = already;
    if (body.size() > content_length) body.resize(content_length);
    int idle = 0;
    char buf[4096];
    while (body.size() < content_length && !cancel && idle < 100) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 100) <= 0) {
            ++idle;
            continue;
        }
        idle = 0;
        const ssize_t n = ::recv(fd, buf, std::min(sizeof(buf), content_length - body.size()), 0);
        if (n <= 0) break;
        body.append(buf, static_cast<size_t>(n));
    }
    return body;
}

std::vector<std::string> parse_json_paths(const std::string& body) {
    // Minimal: {"paths":["/a","/b"]}
    std::vector<std::string> out;
    const auto key = body.find("\"paths\"");
    if (key == std::string::npos) return out;
    const auto lb = body.find('[', key);
    const auto rb = body.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return out;
    size_t i = lb + 1;
    while (i < rb) {
        while (i < rb && (body[i] == ' ' || body[i] == ',' || body[i] == '\n' || body[i] == '\r')) ++i;
        if (i >= rb || body[i] != '"') break;
        ++i;
        std::string s;
        while (i < rb && body[i] != '"') {
            if (body[i] == '\\' && i + 1 < rb) {
                s.push_back(body[i + 1]);
                i += 2;
            } else {
                s.push_back(body[i++]);
            }
        }
        if (i < rb && body[i] == '"') ++i;
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

void serve_file(int fd, const std::string& path, bool head_only, std::atomic<bool>& cancel, bool as_download) {
    const int f = ::open(path.c_str(), O_RDONLY);
    if (f < 0) {
        send_response(fd, 404, "Not Found", "text/plain", "not found\n", head_only);
        return;
    }
    struct stat sb{};
    if (fstat(f, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        ::close(f);
        send_response(fd, 404, "Not Found", "text/plain", "not found\n", head_only);
        return;
    }
    const std::string name = fs::path(path).filename().string();
    const char* ctype = as_download ? "application/octet-stream" : content_type_for(name);
    std::string h = sfmt("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
                         "X-Content-Type-Options: nosniff\r\nConnection: close\r\n",
                         ctype, static_cast<long long>(sb.st_size));
    if (as_download) {
        h += "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
    }
    h += "\r\n";
    send_all(fd, h);
    if (!head_only) {
        char buf[16384];
        while (!cancel) {
            const ssize_t n = ::read(f, buf, sizeof(buf));
            if (n <= 0 || !send_all(fd, buf, static_cast<size_t>(n))) break;
        }
    }
    ::close(f);
}

void accept_upload(int fd, const std::string& path, size_t content_length, const std::string& already,
                   std::atomic<bool>& cancel) {
    if (content_length > kMaxUpload) {
        send_response(fd, 413, "Payload Too Large", "text/plain", "too large\n");
        return;
    }
    const int f = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (f < 0) {
        send_response(fd, errno == EEXIST ? 409 : 500, errno == EEXIST ? "Conflict" : "Error", "text/plain",
                      "cannot create\n");
        return;
    }
    size_t got = std::min(already.size(), content_length);
    bool ok = ::write(f, already.data(), got) == static_cast<ssize_t>(got);
    char buf[16384];
    int idle = 0;
    while (ok && got < content_length && !cancel && idle < 100) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 100) <= 0) {
            ++idle;
            continue;
        }
        idle = 0;
        const ssize_t n = ::recv(fd, buf, std::min(sizeof(buf), content_length - got), 0);
        if (n <= 0) break;
        ok = ::write(f, buf, static_cast<size_t>(n)) == n;
        got += static_cast<size_t>(n);
    }
    ::close(f);
    if (ok && got == content_length) {
        send_response(fd, 201, "Created", "text/plain", "ok\n");
    } else {
        ::unlink(path.c_str());
        send_response(fd, 400, "Bad Request", "text/plain", "upload failed\n");
    }
}

std::string parse_form_field(const std::string& body, const std::string& name) {
    // application/x-www-form-urlencoded
    const std::string key = name + "=";
    size_t start = 0;
    while (start < body.size()) {
        size_t amp = body.find('&', start);
        if (amp == std::string::npos) amp = body.size();
        const std::string part = body.substr(start, amp - start);
        if (part.rfind(key, 0) == 0) return url_decode(part.substr(key.size()));
        start = amp + 1;
    }
    return {};
}

std::string cookie_token(const std::string& headers) {
    const std::string cookie = header_value(headers, "Cookie");
    const std::string key = "KVXSHARE=";
    size_t pos = cookie.find(key);
    while (pos != std::string::npos) {
        if (pos == 0 || cookie[pos - 1] == ' ' || cookie[pos - 1] == ';') {
            size_t end = cookie.find(';', pos);
            if (end == std::string::npos) end = cookie.size();
            return cookie.substr(pos + key.size(), end - (pos + key.size()));
        }
        pos = cookie.find(key, pos + 1);
    }
    return {};
}

std::string make_token() {
    static const char kAlpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(kAlpha) - 2);
    std::string t;
    t.reserve(32);
    for (int i = 0; i < 32; ++i) t.push_back(kAlpha[dist(gen)]);
    return t;
}

std::string random_password(int len = 12) {
    static const char kAlpha[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(kAlpha) - 2);
    std::string t;
    t.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) t.push_back(kAlpha[dist(gen)]);
    return t;
}

uint16_t parse_hex_port(const std::string& hex) {
    unsigned long v = 0;
    for (char c : hex) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned long>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned long>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned long>(c - 'A' + 10);
    }
    return static_cast<uint16_t>(v);
}

std::string inode_for_port(uint16_t port) {
    std::ifstream in("/proc/net/tcp");
    if (!in) return {};
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        // sl local_address rem_address st ... inode
        std::istringstream iss(line);
        std::string sl, local, rem, st, skip;
        unsigned long inode = 0;
        if (!(iss >> sl >> local >> rem >> st)) continue;
        if (st != "0A") continue; // LISTEN
        const auto colon = local.find(':');
        if (colon == std::string::npos) continue;
        if (parse_hex_port(local.substr(colon + 1)) != port) continue;
        for (int i = 0; i < 6; ++i) iss >> skip;
        iss >> inode;
        if (inode != 0) return std::to_string(inode);
    }
    // Also try tcp6
    std::ifstream in6("/proc/net/tcp6");
    if (!in6) return {};
    std::getline(in6, line);
    while (std::getline(in6, line)) {
        std::istringstream iss(line);
        std::string sl, local, rem, st, skip;
        unsigned long inode = 0;
        if (!(iss >> sl >> local >> rem >> st)) continue;
        if (st != "0A") continue;
        const auto colon = local.find(':');
        if (colon == std::string::npos) continue;
        if (parse_hex_port(local.substr(colon + 1)) != port) continue;
        for (int i = 0; i < 6; ++i) iss >> skip;
        iss >> inode;
        if (inode != 0) return std::to_string(inode);
    }
    return {};
}

std::string process_for_inode(const std::string& inode) {
    if (inode.empty()) return {};
    const std::string needle = "socket:[" + inode + "]";
    DIR* proc = opendir("/proc");
    if (!proc) return {};
    while (dirent* de = readdir(proc)) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
        const std::string pid = de->d_name;
        const std::string fd_dir = "/proc/" + pid + "/fd";
        DIR* fds = opendir(fd_dir.c_str());
        if (!fds) continue;
        bool hit = false;
        while (dirent* fe = readdir(fds)) {
            char link[256];
            const ssize_t n = ::readlink((fd_dir + "/" + fe->d_name).c_str(), link, sizeof(link) - 1);
            if (n <= 0) continue;
            link[n] = 0;
            if (needle == link) {
                hit = true;
                break;
            }
        }
        closedir(fds);
        if (!hit) continue;
        std::ifstream cmd("/proc/" + pid + "/comm");
        std::string name;
        std::getline(cmd, name);
        closedir(proc);
        if (name.empty()) return "pid " + pid;
        return name + " (pid " + pid + ")";
    }
    closedir(proc);
    return {};
}

} // namespace

ListenResult listen_first_free(uint16_t first, uint16_t last) {
    ListenResult out;
    if (last < first) {
        out.error = "bad port range";
        return out;
    }
    std::string first_occupant;
    for (uint16_t p = first; p <= last; ++p) {
        std::string err;
        const int fd = listen_on("0.0.0.0", p, err);
        if (fd >= 0) {
            out.fd = fd;
            out.port = p;
            if (p != first && !first_occupant.empty()) out.occupant = first_occupant;
            return out;
        }
        if (p == first) {
            first_occupant = port_occupant(p);
            if (first_occupant.empty()) first_occupant = err;
            out.occupant = first_occupant;
        }
        if (p == last) {
            out.error = err.empty() ? "no free port" : err;
            if (out.occupant.empty()) out.occupant = port_occupant(p);
        }
    }
    return out;
}

std::string port_occupant(uint16_t port) {
    return process_for_inode(inode_for_port(port));
}

Auth load_or_create_auth() {
    Auth a;
    const std::string path = tools::config_file("file_share_auth");
    {
        std::ifstream in(path);
        std::string user, pass;
        if (in && std::getline(in, user) && std::getline(in, pass) && !user.empty() && !pass.empty()) {
            a.user = user;
            a.password = pass;
            return a;
        }
    }
    a.user = "kvx";
    a.password = random_password(12);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out) {
            out << a.user << '\n' << a.password << '\n';
            out.close();
            ::chmod(path.c_str(), 0600);
        }
    }
    return a;
}

std::string lan_url(uint16_t port, std::string& error) {
    error.clear();
    const auto iface = platform::default_route_iface(error);
    if (iface.ipv4.empty()) {
        if (error.empty()) error = "no IPv4 address";
        return {};
    }
    return sfmt("http://%s:%u/", iface.ipv4.c_str(), static_cast<unsigned>(port));
}

Server::Server(std::string root, bool writable, bool require_auth)
    : root_(std::move(root)), writable_(writable), require_auth_(require_auth) {
    if (require_auth_) auth_ = load_or_create_auth();
    snap_.auth = auth_;
}

Server::~Server() {
    stop();
}

void Server::start() {
    if (started_) return;
    started_ = true;
    cancel_ = false;
    thread_ = std::thread([this]() { run(); });
}

void Server::stop() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    started_ = false;
}

Snapshot Server::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_;
}

void Server::run() {
    auto listen = listen_first_free(8080, 8099);
    if (listen.fd < 0) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.running = false;
        snap_.error = listen.error.empty() ? "listen failed" : listen.error;
        snap_.occupant = listen.occupant;
        snap_.auth = auth_;
        return;
    }

    std::string url_err;
    std::string url = lan_url(listen.port, url_err);
    {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.running = true;
        snap_.port = listen.port;
        snap_.url = url;
        snap_.error = url.empty() ? url_err : std::string();
        snap_.occupant = listen.occupant;
        snap_.auth = auth_;
    }

    while (!cancel_) {
        pollfd p{listen.fd, POLLIN, 0};
        if (::poll(&p, 1, 200) <= 0) continue;
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        const int cfd = ::accept(listen.fd, reinterpret_cast<sockaddr*>(&sa), &len);
        if (cfd < 0) continue;
        set_nonblock(cfd, false);
        handle(cfd);
        ::close(cfd);
    }
    ::close(listen.fd);
    std::lock_guard<std::mutex> lock(mu_);
    snap_.running = false;
}

void Server::handle(int fd) {
    std::string req;
    char buf[2048];
    int idle = 0;
    size_t head_end = std::string::npos;
    while (!cancel_ && idle < 30 && req.size() < 16384) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 100) <= 0) {
            ++idle;
            continue;
        }
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
        head_end = req.find("\r\n\r\n");
        if (head_end != std::string::npos) break;
    }
    if (head_end == std::string::npos) {
        send_response(fd, 400, "Bad Request", "text/plain", "bad request\n");
        return;
    }
    const std::string head = req.substr(0, head_end);
    const std::string body_start = req.substr(head_end + 4);
    const auto lines = split_lines(head);
    const auto rl = split_ws(lines.empty() ? std::string() : lines[0]);
    if (rl.size() < 2) {
        send_response(fd, 400, "Bad Request", "text/plain", "bad request\n");
        return;
    }
    const std::string method = rl[0];
    const std::string raw_target = rl[1];
    std::string path = raw_target;
    const auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);
    path = url_decode(path);

    size_t content_length = 0;
    {
        const std::string clv = header_value(head, "Content-Length");
        if (!clv.empty()) content_length = static_cast<size_t>(std::strtoull(clv.c_str(), nullptr, 10));
    }

    auto authed = [&]() {
        if (!require_auth_) return true;
        const std::string tok = cookie_token(head);
        if (tok.empty()) return false;
        std::lock_guard<std::mutex> lock(mu_);
        return std::find(sessions_.begin(), sessions_.end(), tok) != sessions_.end();
    };

    auto resolve = [&](const std::string& url_path, std::string& abs_out, std::string& err_out) -> bool {
        err_out.clear();
        abs_out.clear();
        if (url_path.empty() || url_path[0] != '/' || url_path.find('\0') != std::string::npos) {
            err_out = "bad path";
            return false;
        }
        const std::string rel = normalize_rel(url_path);
        if (has_hidden_component(rel)) {
            err_out = "forbidden";
            return false;
        }
        abs_out = platform::resolve_under_root(root_, rel, err_out);
        return !abs_out.empty();
    };

    if (method == "POST" && path == "/login" && require_auth_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 64 * 1024);
        const std::string user = parse_form_field(body, "username");
        const std::string pass = parse_form_field(body, "password");
        if (user == auth_.user && pass == auth_.password) {
            const std::string token = make_token();
            {
                std::lock_guard<std::mutex> lock(mu_);
                sessions_.push_back(token);
                if (sessions_.size() > 8) sessions_.erase(sessions_.begin());
            }
            send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n", false,
                          "Set-Cookie: KVXSHARE=" + token + "; Path=/; HttpOnly\r\n");
        } else {
            send_response(fd, 401, "Unauthorized", "application/json", "{\"ok\":false,\"error\":\"bad credentials\"}\n");
        }
        return;
    }

    if (method == "POST" && path == "/logout") {
        const std::string tok = cookie_token(head);
        {
            std::lock_guard<std::mutex> lock(mu_);
            sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), tok), sessions_.end());
        }
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n", false,
                      "Set-Cookie: KVXSHARE=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n");
        return;
    }

    // SPA shell is public; APIs require auth.
    if ((method == "GET" || method == "HEAD") && (path == "/" || path.empty())) {
        send_response(fd, 200, "OK", "text/html; charset=utf-8", FILE_SHARE_PAGE, method == "HEAD");
        return;
    }

    if (require_auth_ && !authed()) {
        send_response(fd, 401, "Unauthorized", "application/json", "{\"error\":\"auth\"}\n");
        return;
    }

    if (method == "GET" && path == "/api/status") {
        send_response(fd, 200, "OK", "application/json",
                      sfmt("{\"writable\":%s,\"root\":\"%s\"}\n", writable_ ? "true" : "false",
                           json_escape(root_).c_str()));
        return;
    }

    if (method == "GET" && path == "/api/list") {
        const std::string req_path = query_param(raw_target, "path");
        const std::string url_path = req_path.empty() ? "/" : req_path;
        std::string abs, err;
        if (!resolve(url_path, abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        std::error_code ec;
        if (!fs::is_directory(abs, ec)) {
            send_response(fd, 400, "Bad Request", "text/plain", "not a directory\n");
            return;
        }
        std::string json = "{\"path\":\"" + json_escape(url_path) + "\",\"items\":[";
        bool first = true;
        for (const auto& de : fs::directory_iterator(abs, ec)) {
            const std::string name = de.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code ec2;
            const bool is_dir = de.is_directory(ec2);
            uint64_t size = 0;
            if (!is_dir) {
                std::error_code ec3;
                size = static_cast<uint64_t>(de.file_size(ec3));
            }
            if (!first) json += ',';
            first = false;
            json += sfmt("{\"name\":\"%s\",\"dir\":%s,\"size\":%llu}", json_escape(name).c_str(),
                         is_dir ? "true" : "false", static_cast<unsigned long long>(size));
        }
        json += "]}\n";
        send_response(fd, 200, "OK", "application/json", json);
        return;
    }

    if (method == "GET" && (path == "/api/raw" || path == "/api/download")) {
        const std::string req_path = query_param(raw_target, "path");
        std::string abs, err;
        if (!resolve(req_path, abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        serve_file(fd, abs, false, cancel_, path == "/api/download");
        return;
    }

    if (method == "GET" && path == "/api/preview") {
        const std::string req_path = query_param(raw_target, "path");
        std::string abs, err;
        if (!resolve(req_path, abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        const std::string name = fs::path(abs).filename().string();
        if (!is_text_ext(name)) {
            send_response(fd, 400, "Bad Request", "text/plain", "not a text preview\n");
            return;
        }
        std::ifstream in(abs, std::ios::binary);
        if (!in) {
            send_response(fd, 404, "Not Found", "text/plain", "not found\n");
            return;
        }
        constexpr size_t kMaxPreview = 512 * 1024;
        std::string data(kMaxPreview + 1, '\0');
        in.read(data.data(), static_cast<std::streamsize>(data.size()));
        const auto got = static_cast<size_t>(std::max<std::streamsize>(0, in.gcount()));
        const bool truncated = got > kMaxPreview;
        data.resize(std::min(got, kMaxPreview));
        for (char& c : data) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u == 0) c = ' ';
        }
        send_response(fd, 200, "OK", "application/json",
                      "{\"text\":\"" + json_escape(data) + "\",\"truncated\":" + (truncated ? "true" : "false") +
                          "}\n");
        return;
    }

    if (method == "PUT" && path == "/api/upload" && writable_) {
        const std::string req_path = query_param(raw_target, "path");
        std::string abs, err;
        if (!resolve(req_path, abs, err) || req_path.empty() || req_path.back() == '/') {
            send_response(fd, 400, "Bad Request", "text/plain", "need a file path\n");
            return;
        }
        // Overwrite if exists: remove then create.
        std::error_code ec;
        if (fs::exists(abs, ec)) fs::remove(abs, ec);
        accept_upload(fd, abs, content_length, body_start, cancel_);
        return;
    }

    if (method == "POST" && path == "/api/mkdir" && writable_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 64 * 1024);
        const std::string req_path = parse_form_field(body, "path");
        std::string abs, err;
        if (!resolve(req_path, abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        std::error_code ec;
        if (!fs::create_directories(abs, ec) && !fs::is_directory(abs, ec)) {
            send_response(fd, 500, "Error", "text/plain", ec.message() + "\n");
            return;
        }
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    if (method == "POST" && path == "/api/create" && writable_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 64 * 1024);
        const std::string req_path = parse_form_field(body, "path");
        std::string abs, err;
        if (!resolve(req_path, abs, err) || req_path.back() == '/') {
            send_response(fd, 400, "Bad Request", "text/plain", "need a file path\n");
            return;
        }
        const int f = ::open(abs.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (f < 0) {
            send_response(fd, errno == EEXIST ? 409 : 500, errno == EEXIST ? "Conflict" : "Error", "text/plain",
                          "cannot create\n");
            return;
        }
        ::close(f);
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    if (method == "POST" && path == "/api/rename" && writable_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 64 * 1024);
        const std::string req_path = parse_form_field(body, "path");
        const std::string new_name = parse_form_field(body, "name");
        if (new_name.empty() || new_name.find('/') != std::string::npos || new_name[0] == '.') {
            send_response(fd, 400, "Bad Request", "text/plain", "bad name\n");
            return;
        }
        std::string abs, err;
        if (!resolve(req_path, abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        const fs::path dest = fs::path(abs).parent_path() / new_name;
        if (!platform::path_is_under_root(root_, dest.string())) {
            send_response(fd, 403, "Forbidden", "text/plain", "forbidden\n");
            return;
        }
        std::error_code ec;
        fs::rename(abs, dest, ec);
        if (ec) {
            send_response(fd, 500, "Error", "text/plain", ec.message() + "\n");
            return;
        }
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    if (method == "POST" && path == "/api/delete" && writable_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 256 * 1024);
        auto paths = parse_json_paths(body);
        if (paths.empty()) {
            const std::string one = parse_form_field(body, "path");
            if (!one.empty()) paths.push_back(one);
        }
        if (paths.empty()) {
            send_response(fd, 400, "Bad Request", "text/plain", "no paths\n");
            return;
        }
        for (const auto& p : paths) {
            std::string abs, err;
            if (!resolve(p, abs, err)) {
                send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
                return;
            }
            if (abs == root_ || platform::resolve_under_root(root_, "", err) == abs) {
                send_response(fd, 403, "Forbidden", "text/plain", "cannot delete root\n");
                return;
            }
            std::error_code ec;
            fs::remove_all(abs, ec);
            if (ec) {
                send_response(fd, 500, "Error", "text/plain", ec.message() + "\n");
                return;
            }
        }
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    if (method == "POST" && (path == "/api/copy" || path == "/api/move") && writable_) {
        const std::string body = read_body(fd, content_length, body_start, cancel_, 64 * 1024);
        const std::string src = parse_form_field(body, "src");
        const std::string dest = parse_form_field(body, "dest");
        std::string src_abs, dest_abs, err;
        if (!resolve(src, src_abs, err) || !resolve(dest, dest_abs, err)) {
            send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
            return;
        }
        std::error_code ec;
        if (path == "/api/move") {
            fs::rename(src_abs, dest_abs, ec);
            if (ec) {
                fs::copy(src_abs, dest_abs, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (!ec) fs::remove_all(src_abs, ec);
            }
        } else {
            fs::copy(src_abs, dest_abs, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            send_response(fd, 500, "Error", "text/plain", ec.message() + "\n");
            return;
        }
        send_response(fd, 200, "OK", "application/json", "{\"ok\":true}\n");
        return;
    }

    // Compatibility: direct file GET/PUT under root for simple clients.
    if (path.empty() || path[0] != '/') {
        send_response(fd, 404, "Not Found", "text/plain", "not found\n");
        return;
    }
    std::string abs, err;
    if (!resolve(path, abs, err)) {
        send_response(fd, 403, "Forbidden", "text/plain", err + "\n");
        return;
    }
    if (method == "GET" || method == "HEAD") {
        std::error_code ec;
        if (fs::is_regular_file(abs, ec)) {
            serve_file(fd, abs, method == "HEAD", cancel_, false);
        } else {
            send_response(fd, 404, "Not Found", "text/plain", "not found\n");
        }
    } else if (method == "PUT" && writable_) {
        accept_upload(fd, abs, content_length, body_start, cancel_);
    } else {
        send_response(fd, 405, "Method Not Allowed", "text/plain", "not allowed\n");
    }
}

} // namespace tools::file_share

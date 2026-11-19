// Tool screens for kvxputerzero: channel tool menus, Discovery, and every ToolId run screen.
//
// Navigation model
// ----------------
// Tools are addressed by (ToolId, tool_param). tool_param encodes a tiny "stage[:arg]" state so
// a single ToolId can show a menu, a prompt, and a run screen one after another. Sub-steps are
// opened through nav(), which calls vm.open_tool(...) and, when we are already on
// AppPage::ToolRun, bounces through AppPage::ToolPrompt so the ScreenManager sees a page change
// and rebuilds the screen. The ScreenManager must therefore build BOTH AppPage::ToolRun and
// AppPage::ToolPrompt via make_tool_run_screen(), AppPage::ToolMenu via make_channel_tool_menu()
// and AppPage::Discovery via make_discovery_screen().
//
// ToolId::None is used for small generic helpers: "info:<text>" (one-line result list),
// "ethlink", "gpsfolder" and "filter" (Discovery filter prompt).
//
// All blocking work runs in RunScreen workers (cancel = Esc). Platform helpers report errors as
// strings, so nothing here hangs on desktop builds that lack BlueZ, nmcli, ALSA, etc.

#include "tool_screens.h"

#include "cap_screens.h"
#include "gps_screens.h"
#include "audio.h"
#include "ble_scan.h"
#include "dns_query.h"
#include "fs_root.h"
#include "gps_fix.h"
#include "http_get.h"
#include "i2c_scan.h"
#include "ir_lib.h"
#include "ir_lirc.h"
#include "ldap_read.h"
#include "net_info.h"
#include "doc_screen.h"
#include "info_screen.h"
#include "options_list_screen.h"
#include "prompt_screen.h"
#include "result_list_screen.h"
#include "run_screen.h"
#include "shell_util.h"
#include "sip_options.h"
#include "tagtinker_lib.h"
#include "tcp_probe.h"
#include "tvbgone_lib.h"
#include "uart_session.h"
#include "user_paths.h"
#include "wifi_scan.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace app {
namespace {

namespace fs = std::filesystem;
using model::ToolId;
using screen::MenuEntry;
using VM = viewmodel::BaseViewModel;
using Screen = std::unique_ptr<screen::KvxScreen>;
using Cancel = std::atomic<bool>;
using Log = std::function<void(const std::string&)>;
using Status = std::function<void(const std::string&)>;
using Worker = screen::RunScreen::WorkerFn;

// ---------------------------------------------------------------------------------------------
// Small string / file helpers
// ---------------------------------------------------------------------------------------------

std::string sfmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string sfmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> split_ws(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

std::string printable(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) out.push_back((c >= 32 && c < 127) ? static_cast<char>(c) : (c == '\t' ? ' ' : '.'));
    return out;
}

std::string shorten(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n > 1 ? n - 1 : n) + "~";
}

std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

std::string read_config_value(const char* name, const std::string& def) {
    const std::string v = trim(read_text_file(tools::config_file(name)));
    return v.empty() ? def : v;
}

std::string home_dir() {
    if (const char* h = std::getenv("HOME")) {
        if (*h) return h;
    }
    return tools::config_dir();
}

std::string ensure_subdir(const std::string& name) {
    const fs::path p = fs::path(tools::config_dir()) / name;
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
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

bool valid_host(const std::string& h) {
    if (h.empty() || h.size() > 253) return false;
    for (unsigned char c : h) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
    }
    return true;
}

bool parse_port(const std::string& s, uint16_t& out) {
    const std::string t = trim(s);
    if (t.empty() || t.size() > 5) return false;
    for (unsigned char c : t) {
        if (!std::isdigit(c)) return false;
    }
    const long v = std::strtol(t.c_str(), nullptr, 10);
    if (v < 1 || v > 65535) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

// "host", "host:port" or "host port".
bool split_host_port(const std::string& in, std::string& host, uint16_t& port, uint16_t def_port) {
    const std::string t = trim(in);
    if (t.empty()) return false;
    port = def_port;
    const auto toks = split_ws(t);
    if (toks.size() >= 2) {
        host = toks[0];
        if (!parse_port(toks[1], port)) return false;
        return valid_host(host);
    }
    const auto colon = t.rfind(':');
    if (colon != std::string::npos && t.find(':') == colon) {
        host = t.substr(0, colon);
        if (!parse_port(t.substr(colon + 1), port)) return false;
    } else {
        host = t;
    }
    return valid_host(host);
}

// The platform helpers take a plain bool* cancel flag; std::atomic<bool> is a single byte on
// every target we build for, so the worker's cancel atomic can be handed over directly.
static_assert(sizeof(std::atomic<bool>) == sizeof(bool), "atomic<bool> must be bool-sized");
bool* cancel_ptr(Cancel& c) {
    return reinterpret_cast<bool*>(&c);
}

bool sleep_cancel(Cancel& cancel, int ms) {
    int left = ms;
    while (left > 0 && !cancel) {
        const int step = std::min(left, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        left -= step;
    }
    return !cancel;
}

void log_lines(const Log& log, const std::string& text) {
    for (const auto& l : split_lines(text)) {
        if (!l.empty()) log(l);
    }
}

std::string timestamp_name() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

// ---------------------------------------------------------------------------------------------
// tool_param "stage[:arg]" + navigation
// ---------------------------------------------------------------------------------------------

struct Stage {
    std::string name;
    std::string arg;
};

Stage parse_stage(const std::string& p) {
    const auto c = p.find(':');
    if (c == std::string::npos) return {p, {}};
    return {p.substr(0, c), p.substr(c + 1)};
}

void nav(VM& vm, ToolId id, const std::string& stage, const std::string& arg = {}) {
    const auto prev = vm.current_page();
    std::string param = stage;
    if (!arg.empty()) param += ":" + arg;
    vm.open_tool(id, param);
    // Same-page navigation is ignored by the ScreenManager, so alternate between two pages.
    if (prev == model::AppPage::ToolRun) vm.show_page(model::AppPage::ToolPrompt);
}

void nav_info(VM& vm, const std::string& text) {
    nav(vm, ToolId::None, "info", text);
}

// ---------------------------------------------------------------------------------------------
// Screen builders
// ---------------------------------------------------------------------------------------------

Screen make_options(VM& vm, AssetManager& assets, const std::string& title, std::vector<MenuEntry> entries) {
    if (entries.empty()) entries.push_back({"(empty)", [&vm]() { vm.go_back(); }});
    return std::make_unique<screen::OptionsListScreen>(vm, assets, title, std::move(entries));
}

Screen make_results(VM& vm, AssetManager& assets, const std::string& title, std::vector<std::string> lines,
                    std::function<void()> on_save = nullptr) {
    return std::make_unique<screen::ResultListScreen>(vm, assets, title, std::move(lines), std::move(on_save));
}

Screen make_prompt(VM& vm, AssetManager& assets, const std::string& title, const std::string& hint,
                   const std::string& initial, screen::PromptScreen::ConfirmFn fn) {
    return std::make_unique<screen::PromptScreen>(vm, assets, title, hint, initial, std::move(fn));
}

Screen make_run(VM& vm, AssetManager& assets, const std::string& title, Worker worker) {
    return std::make_unique<screen::RunScreen>(vm, assets, title, std::move(worker));
}

Screen make_info(VM& vm, AssetManager& assets, const std::string& title, const std::string& text) {
    // Documents / help / confirmations — never menu rows.
    return std::make_unique<screen::DocScreen>(vm, assets, title, text);
}

Screen make_info_card(VM& vm, AssetManager& assets, const std::string& title,
                      screen::InfoScreen::Fields fields, const std::string& status = "OK",
                      view::StatusPillKind kind = view::StatusPillKind::Ok) {
    return std::make_unique<screen::InfoScreen>(vm, assets, title, std::move(fields), status, kind);
}

Screen make_info_card(VM& vm, AssetManager& assets, const std::string& title,
                      screen::InfoScreen::WorkerFn worker) {
    return std::make_unique<screen::InfoScreen>(vm, assets, title, std::move(worker));
}

// Prompt then continue to `next_stage` with the typed value as arg.
Screen ask_then(VM& vm, AssetManager& assets, ToolId id, const std::string& title, const std::string& hint,
                const std::string& initial, const std::string& next_stage) {
    return make_prompt(vm, assets, title, hint, initial,
                       [&vm, id, next_stage](const std::string& v) { nav(vm, id, next_stage, trim(v)); });
}

MenuEntry stage_entry(VM& vm, ToolId id, const std::string& label, const std::string& stage,
                      const std::string& arg = {}) {
    return {label, [&vm, id, stage, arg]() { nav(vm, id, stage, arg); }};
}

// ---------------------------------------------------------------------------------------------
// Socket helpers (POSIX)
// ---------------------------------------------------------------------------------------------

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
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

std::string peer_string(const sockaddr_in& sa) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
    return sfmt("%s:%u", ip, static_cast<unsigned>(ntohs(sa.sin_port)));
}

// Returns an accepted (blocking) fd or -1 on timeout/cancel.
int accept_poll(int lfd, Cancel& cancel, int timeout_ms, std::string* peer = nullptr) {
    if (cancel) return -1;
    pollfd p{lfd, POLLIN, 0};
    const int r = ::poll(&p, 1, timeout_ms);
    if (r <= 0) return -1;
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    const int fd = ::accept(lfd, reinterpret_cast<sockaddr*>(&sa), &len);
    if (fd < 0) return -1;
    set_nonblock(fd, false);
    if (peer) *peer = peer_string(sa);
    return fd;
}

int connect_tcp(const std::string& host, uint16_t port, int timeout_ms, Cancel& cancel, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    const int gai = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (gai != 0 || !res) {
        err = std::string("resolve failed: ") + gai_strerror(gai);
        return -1;
    }
    int result = -1;
    for (addrinfo* ai = res; ai && result < 0; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        set_nonblock(fd, true);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            int waited = 0;
            while (waited < timeout_ms && !cancel) {
                pollfd p{fd, POLLOUT, 0};
                const int pr = ::poll(&p, 1, 100);
                if (pr > 0) {
                    int soerr = 0;
                    socklen_t sl = sizeof(soerr);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                    rc = soerr == 0 ? 0 : -1;
                    if (soerr != 0) errno = soerr;
                    break;
                }
                waited += 100;
            }
            if (rc != 0 && (waited >= timeout_ms || cancel)) errno = cancel ? ECANCELED : ETIMEDOUT;
        }
        if (rc == 0) {
            set_nonblock(fd, false);
            result = fd;
        } else {
            err = std::strerror(errno);
            ::close(fd);
        }
    }
    freeaddrinfo(res);
    return result;
}

bool send_all(int fd, const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (len > 0) {
        const ssize_t n = ::send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool send_all(int fd, const std::string& s) {
    return send_all(fd, s.data(), s.size());
}

// 1 = got n bytes, 0 = timeout/cancel, -1 = closed/error.
int read_exact(int fd, uint8_t* buf, size_t n, Cancel& cancel, int total_ms) {
    size_t got = 0;
    int waited = 0;
    while (got < n) {
        if (cancel || waited >= total_ms) return 0;
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 100);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            waited += 100;
            continue;
        }
        const ssize_t k = ::recv(fd, buf + got, n - got, 0);
        if (k <= 0) return -1;
        got += static_cast<size_t>(k);
    }
    return 1;
}

// Splits a byte stream into log lines.
struct LineSplitter {
    std::string buf;
    void feed(const std::string& data, const Log& log) {
        for (char c : data) {
            if (c == '\n') {
                emit(log);
            } else if (c != '\r') {
                buf.push_back(c);
                if (buf.size() >= 72) emit(log);
            }
        }
    }
    void emit(const Log& log) {
        if (!buf.empty()) log(printable(buf));
        buf.clear();
    }
};

struct TelnetFilter {
    int state = 0; // 0 data, 1 IAC, 2 option byte follows, 3 subneg, 4 subneg IAC
    uint8_t cmd = 0;
    std::string process(int fd, const uint8_t* in, size_t n) {
        std::string out;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = in[i];
            switch (state) {
            case 0:
                if (b == 255) state = 1;
                else out.push_back(static_cast<char>(b));
                break;
            case 1:
                if (b == 255) {
                    out.push_back(static_cast<char>(255));
                    state = 0;
                } else if (b >= 251 && b <= 254) {
                    cmd = b;
                    state = 2;
                } else if (b == 250) {
                    state = 3;
                } else {
                    state = 0;
                }
                break;
            case 2: {
                uint8_t reply[3] = {255, 0, b};
                if (cmd == 253) reply[1] = 252; // DO -> WONT
                else if (cmd == 251) reply[1] = 254; // WILL -> DONT
                if (reply[1] != 0) send_all(fd, reply, 3);
                state = 0;
                break;
            }
            case 3:
                if (b == 255) state = 4;
                break;
            case 4:
                state = (b == 240) ? 0 : 3;
                break;
            }
        }
        return out;
    }
};

// Reads until the peer closes or cancel. Echoes received text to the log.
void session_loop(int fd, Cancel& cancel, const Log& log, bool telnet) {
    LineSplitter ls;
    TelnetFilter tf;
    uint8_t buf[1024];
    while (!cancel) {
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 200);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        if (telnet) ls.feed(tf.process(fd, buf, static_cast<size_t>(n)), log);
        else ls.feed(std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n)), log);
    }
    ls.emit(log);
}

// ---------------------------------------------------------------------------------------------
// SSDP (UDP multicast) helper
// ---------------------------------------------------------------------------------------------

struct SsdpHit {
    std::string from;
    std::string location;
    std::string server;
    std::string st;
};

std::string header_value(const std::string& text, const std::string& name) {
    for (const auto& line : split_lines(text)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (lower(trim(line.substr(0, colon))) == lower(name)) return trim(line.substr(colon + 1));
    }
    return {};
}

std::vector<SsdpHit> ssdp_search(const std::string& st, int wait_ms, Cancel& cancel, std::string& err) {
    std::vector<SsdpHit> hits;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        err = std::strerror(errno);
        return hits;
    }
    int ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);
    const std::string msg = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: " +
                            st + "\r\n\r\n";
    if (::sendto(fd, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) < 0) {
        err = std::strerror(errno);
        ::close(fd);
        return hits;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    std::set<std::string> seen;
    while (!cancel && std::chrono::steady_clock::now() < deadline) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 200) <= 0) continue;
        char buf[2048];
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const ssize_t n = ::recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        buf[n] = '\0';
        SsdpHit h;
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        h.from = ip;
        const std::string text(buf, static_cast<size_t>(n));
        h.location = header_value(text, "LOCATION");
        h.server = header_value(text, "SERVER");
        h.st = header_value(text, "ST");
        const std::string key = h.from + "|" + h.location + "|" + h.st;
        if (seen.insert(key).second) hits.push_back(std::move(h));
    }
    ::close(fd);
    return hits;
}

// ---------------------------------------------------------------------------------------------
// Goertzel (DTMF / spectrum / morse)
// ---------------------------------------------------------------------------------------------

double goertzel_power(const int16_t* x, size_t n, double freq, double rate) {
    const double w = 2.0 * M_PI * freq / rate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0;
    double s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double energy_of(const std::vector<int16_t>& x) {
    double e = 0;
    for (int16_t v : x) e += static_cast<double>(v) * v;
    return e;
}

// ---------------------------------------------------------------------------------------------
// Channel menu helpers
// ---------------------------------------------------------------------------------------------

std::string captures_dir() {
    const std::string saved = trim(read_text_file(tools::config_file("captures_path.txt")));
    if (!saved.empty()) return saved;
    return ensure_subdir("captures");
}

std::vector<std::string> ethernet_link_lines() {
    std::string err;
    const auto ifaces = platform::list_net_ifaces(err);
    std::vector<std::string> lines;
    for (const auto& i : ifaces) {
        if (i.kind != "ethernet") continue;
        lines.push_back(sfmt("%s  %s%s", i.name.c_str(), i.up ? "UP" : "DOWN",
                             i.state.empty() ? "" : (" (" + i.state + ")").c_str()));
        if (!i.hw_address.empty()) lines.push_back("MAC " + i.hw_address);
        if (!i.ipv4.empty()) {
            lines.push_back(i.prefix >= 0 ? sfmt("IPv4 %s/%d", i.ipv4.c_str(), i.prefix) : "IPv4 " + i.ipv4);
        }
        if (!i.gateway.empty()) lines.push_back("GW " + i.gateway);
        if (!i.dns.empty()) lines.push_back("DNS " + i.dns);
    }
    if (lines.empty()) lines.push_back("No ethernet interface found");
    if (!err.empty()) lines.push_back("note: " + err);
    return lines;
}

// ---------------------------------------------------------------------------------------------
// Net tools: NetToolkit
// ---------------------------------------------------------------------------------------------

std::vector<uint16_t> parse_port_list(const std::string& spec) {
    std::vector<uint16_t> out;
    if (spec.empty()) {
        static const uint16_t kCommon[] = {21, 22, 23, 25, 53, 80, 110, 139, 143, 443, 445, 993, 995, 1883, 3306, 3389, 5900, 8080, 8443};
        out.assign(std::begin(kCommon), std::end(kCommon));
        return out;
    }
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        const auto dash = part.find('-');
        if (dash != std::string::npos) {
            uint16_t a = 0;
            uint16_t b = 0;
            if (parse_port(part.substr(0, dash), a) && parse_port(part.substr(dash + 1), b) && a <= b) {
                for (uint32_t p = a; p <= b && out.size() < 1024; ++p) out.push_back(static_cast<uint16_t>(p));
            }
        } else {
            uint16_t p = 0;
            if (parse_port(part, p) && out.size() < 1024) out.push_back(p);
        }
    }
    return out;
}

Worker port_scan_worker(std::string arg) {
    return [arg](Cancel& cancel, const Log& log, const Status& status) {
        const auto toks = split_ws(arg);
        if (toks.empty() || !valid_host(toks[0])) {
            log("usage: host [ports e.g. 22,80,443 or 1-1024]");
            return;
        }
        const auto ports = parse_port_list(toks.size() > 1 ? toks[1] : "");
        if (ports.empty()) {
            log("no valid ports");
            return;
        }
        log(sfmt("scanning %s (%zu ports)", toks[0].c_str(), ports.size()));
        int open = 0;
        for (size_t i = 0; i < ports.size() && !cancel; ++i) {
            status(sfmt("port %u  (%zu/%zu)", ports[i], i + 1, ports.size()));
            const auto r = platform::tcp_probe(toks[0], ports[i], 400, false);
            if (r.open) {
                ++open;
                log(sfmt("%u open", ports[i]));
            } else if (i == 0 && !r.error.empty() && r.error.find("resolve") != std::string::npos) {
                log("error: " + r.error);
                return;
            }
        }
        log(sfmt("done: %d open", open));
    };
}

Worker banner_worker(std::string arg) {
    return [arg](Cancel&, const Log& log, const Status& status) {
        std::string host;
        uint16_t port = 0;
        if (!split_host_port(arg, host, port, 80)) {
            log("usage: host port");
            return;
        }
        status("connecting...");
        const auto r = platform::tcp_probe(host, port, 3000, true, 512);
        if (!r.open) {
            log("closed/unreachable" + (r.error.empty() ? std::string() : ": " + r.error));
            return;
        }
        log(sfmt("%s:%u open", host.c_str(), port));
        if (r.banner.empty()) log("(no banner)");
        else {
            for (const auto& l : split_lines(r.banner)) {
                if (!l.empty()) log(printable(l));
            }
        }
    };
}

Worker shell_worker(std::string title_cmd_desc, std::string cmd) {
    return [title_cmd_desc, cmd](Cancel& cancel, const Log& log, const Status& status) {
        status(title_cmd_desc);
        std::string out;
        const int rc = platform::run_shell(cmd + " 2>&1", out);
        if (cancel) return;
        if (out.empty()) log("(no output)");
        for (const auto& l : split_lines(out)) log(printable(l));
        status(rc == 0 ? "done" : sfmt("exit %d", rc));
    };
}

Worker ping_worker(std::string arg) {
    const std::string host = trim(arg);
    if (!valid_host(host)) {
        return [](Cancel&, const Log& log, const Status&) { log("invalid host"); };
    }
    return shell_worker("pinging " + host, "timeout 15 ping -n -c 4 -W 2 " + host);
}

Worker trace_worker(std::string arg) {
    const std::string host = trim(arg);
    if (!valid_host(host)) {
        return [](Cancel&, const Log& log, const Status&) { log("invalid host"); };
    }
    const std::string cmd = "if command -v traceroute >/dev/null 2>&1; then timeout 45 traceroute -n -w 1 -q 1 -m 15 " + host +
                            "; elif command -v tracepath >/dev/null 2>&1; then timeout 45 tracepath -n -m 15 " + host +
                            "; else echo 'traceroute/tracepath not installed'; fi";
    return shell_worker("tracing " + host, cmd);
}

Worker dig_worker(std::string arg) {
    return [arg](Cancel&, const Log& log, const Status& status) {
        const auto toks = split_ws(arg);
        if (toks.empty() || !valid_host(toks[0])) {
            log("usage: name [A|AAAA|CNAME|TXT|MX] [resolver ip]");
            return;
        }
        platform::DnsType type = platform::DnsType::A;
        std::string tname = "A";
        std::string resolver;
        for (size_t i = 1; i < toks.size(); ++i) {
            const std::string u = [&]() {
                std::string s = toks[i];
                for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return s;
            }();
            if (u == "A") type = platform::DnsType::A;
            else if (u == "AAAA") type = platform::DnsType::AAAA;
            else if (u == "CNAME") type = platform::DnsType::CNAME;
            else if (u == "TXT") type = platform::DnsType::TXT;
            else if (u == "MX") type = platform::DnsType::MX;
            else if (valid_host(toks[i])) {
                resolver = toks[i];
                continue;
            }
            tname = u;
        }
        status("querying " + toks[0]);
        std::vector<platform::DnsAnswer> answers;
        std::string err;
        if (!platform::dns_query(toks[0], type, resolver, answers, err)) {
            log("dns error: " + err);
            return;
        }
        log(sfmt("%s %s: %zu answer(s)", toks[0].c_str(), tname.c_str(), answers.size()));
        for (const auto& a : answers) log(printable(a.text));
    };
}

Worker curl_worker(std::string arg) {
    return [arg](Cancel&, const Log& log, const Status& status) {
        std::string url = trim(arg);
        if (url.empty()) {
            log("usage: url");
            return;
        }
        if (url.find("://") == std::string::npos) url = "http://" + url;
        status("GET " + url);
        const auto r = platform::http_request("GET", url, {}, 8, 4096);
        if (!r.error.empty()) {
            log("error: " + r.error);
            return;
        }
        log(sfmt("HTTP %d", r.status));
        for (const auto& l : split_lines(r.headers)) {
            if (!l.empty()) log(printable(l));
        }
        log("---");
        for (const auto& l : split_lines(r.body)) log(printable(l));
    };
}

Screen make_net_toolkit(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::NetToolkit;
    if (st.name.empty()) {
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "Port Scan", "ask_scan"));
        e.push_back(stage_entry(vm, id, "Banner Grab", "ask_banner"));
        e.push_back(stage_entry(vm, id, "Ping", "ask_ping"));
        e.push_back(stage_entry(vm, id, "Dig", "ask_dig"));
        e.push_back(stage_entry(vm, id, "Trace", "ask_trace"));
        e.push_back(stage_entry(vm, id, "Curl-lite", "ask_curl"));
        return make_options(vm, assets, "Net Toolkit", std::move(e));
    }
    if (st.name == "ask_scan")
        return ask_then(vm, assets, id, "Port Scan", "host [ports: 22,80 or 1-1024]", "", "scan");
    if (st.name == "ask_banner") return ask_then(vm, assets, id, "Banner Grab", "host port", "", "banner");
    if (st.name == "ask_ping") return ask_then(vm, assets, id, "Ping", "host or IP", "", "ping");
    if (st.name == "ask_dig")
        return ask_then(vm, assets, id, "Dig", "name [A|AAAA|MX|TXT|CNAME] [resolver]", "", "dig");
    if (st.name == "ask_trace") return ask_then(vm, assets, id, "Trace", "host or IP", "", "trace");
    if (st.name == "ask_curl") return ask_then(vm, assets, id, "Curl-lite", "URL", "http://", "curl");
    if (st.name == "scan") return make_run(vm, assets, "Port Scan", port_scan_worker(st.arg));
    if (st.name == "banner") return make_run(vm, assets, "Banner Grab", banner_worker(st.arg));
    if (st.name == "ping") return make_run(vm, assets, "Ping", ping_worker(st.arg));
    if (st.name == "dig") return make_run(vm, assets, "Dig", dig_worker(st.arg));
    if (st.name == "trace") return make_run(vm, assets, "Trace", trace_worker(st.arg));
    if (st.name == "curl") return make_run(vm, assets, "Curl-lite", curl_worker(st.arg));
    return make_info(vm, assets, "Net Toolkit", "Unknown step");
}

// ---------------------------------------------------------------------------------------------
// Net tools: ScanHosts
// ---------------------------------------------------------------------------------------------

std::string neigh_mac(const std::string& line) {
    const auto toks = split_ws(line);
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i] == "lladdr") return toks[i + 1];
    }
    return {};
}

Worker scan_hosts_worker(std::string param) {
    return [param](Cancel& cancel, const Log& log, const Status& status) {
        std::string err;
        platform::NetIface pick;
        const std::string want = trim(param);
        if (!want.empty()) {
            pick = platform::find_iface(want, err);
        } else {
            pick = platform::default_route_iface(err);
            if (pick.name.empty()) {
                for (const auto& i : platform::list_net_ifaces(err)) {
                    if (i.up && !i.ipv4.empty() && i.name != "lo") {
                        pick = i;
                        break;
                    }
                }
            }
        }
        if (pick.name.empty() || !valid_host(pick.name)) {
            log("no usable interface" + (err.empty() ? std::string() : ": " + err));
            return;
        }
        log(sfmt("iface %s (%s) %s/%d", pick.name.c_str(), pick.kind.c_str(), pick.ipv4.c_str(), pick.prefix));
        if (pick.ipv4.empty()) {
            log("interface has no IPv4 address");
            return;
        }

        std::string out;
        status("reading neighbour table");
        platform::run_shell("ip neigh show dev " + pick.name + " 2>&1", out);
        for (const auto& l : split_lines(out)) {
            if (!l.empty() && l.find("FAILED") == std::string::npos) log("known " + printable(l));
        }

        in_addr ia{};
        if (inet_pton(AF_INET, pick.ipv4.c_str(), &ia) != 1) {
            log("bad local address");
            return;
        }
        int prefix = pick.prefix > 0 ? pick.prefix : 24;
        if (prefix < 24) prefix = 24; // cap sweep at /24
        if (prefix >= 31) {
            log("subnet too small to sweep");
            return;
        }
        const uint32_t ip = ntohl(ia.s_addr);
        const uint32_t mask = ~((1u << (32 - prefix)) - 1u);
        const uint32_t net = ip & mask;
        const uint32_t count = 1u << (32 - prefix);
        std::vector<std::string> targets;
        for (uint32_t h = 1; h + 1 < count && targets.size() < 254; ++h) {
            const uint32_t a = net + h;
            if (a == ip) continue;
            in_addr t{};
            t.s_addr = htonl(a);
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &t, buf, sizeof(buf));
            targets.emplace_back(buf);
        }
        log(sfmt("sweeping %zu hosts", targets.size()));
        constexpr size_t kBatch = 24;
        int alive = 0;
        for (size_t i = 0; i < targets.size() && !cancel; i += kBatch) {
            std::string cmd = "for ip in";
            for (size_t j = i; j < std::min(i + kBatch, targets.size()); ++j) cmd += " " + targets[j];
            cmd += "; do (ping -n -c 1 -W 1 $ip >/dev/null 2>&1 && echo $ip) & done; wait";
            status(sfmt("sweep %zu/%zu", std::min(i + kBatch, targets.size()), targets.size()));
            platform::run_shell(cmd, out);
            for (const auto& l : split_lines(out)) {
                if (!trim(l).empty()) {
                    ++alive;
                    log("alive " + trim(l));
                }
            }
        }
        if (cancel) return;
        platform::run_shell("ip neigh show dev " + pick.name + " 2>&1", out);
        log("--- neighbours ---");
        for (const auto& l : split_lines(out)) {
            if (l.empty() || l.find("FAILED") != std::string::npos || l.find("INCOMPLETE") != std::string::npos) continue;
            const auto toks = split_ws(l);
            const std::string mac = neigh_mac(l);
            log(sfmt("%s %s", toks.empty() ? "?" : toks[0].c_str(), mac.empty() ? "-" : mac.c_str()));
        }
        status(sfmt("done: %d alive", alive));
    };
}

// ---------------------------------------------------------------------------------------------
// Net tools: TCP listen / client / telnet / SOCKS4 / honeypot
// ---------------------------------------------------------------------------------------------

Worker listen_worker(uint16_t port) {
    return [port](Cancel& cancel, const Log& log, const Status& status) {
        std::string err;
        int lfd = listen_on("0.0.0.0", port, err);
        if (lfd < 0) {
            log("listen failed: " + err);
            status("error");
            return;
        }
        status(sfmt("listening :%u", port));
        log(sfmt("listening on 0.0.0.0:%u", port));
        while (!cancel) {
            std::string peer;
            int cfd = accept_poll(lfd, cancel, 250, &peer);
            if (cfd < 0) continue;
            log("peer " + peer);
            status("connected " + peer);
            session_loop(cfd, cancel, log, false);
            close_fd(cfd);
            log("closed " + peer);
            status(sfmt("listening :%u", port));
        }
        close_fd(lfd);
    };
}

Worker client_worker(std::string arg, uint16_t def_port, bool telnet) {
    return [arg, def_port, telnet](Cancel& cancel, const Log& log, const Status& status) {
        std::string host;
        uint16_t port = 0;
        if (!split_host_port(arg, host, port, def_port)) {
            log("usage: host:port");
            return;
        }
        status(sfmt("connecting %s:%u", host.c_str(), port));
        std::string err;
        int fd = connect_tcp(host, port, 5000, cancel, err);
        if (fd < 0) {
            log("connect failed: " + err);
            return;
        }
        log(sfmt("connected to %s:%u", host.c_str(), port));
        status("connected");
        session_loop(fd, cancel, log, telnet);
        close_fd(fd);
        log("connection closed");
    };
}

void relay(int a, int b, Cancel& cancel) {
    uint8_t buf[4096];
    while (!cancel) {
        pollfd p[2] = {{a, POLLIN, 0}, {b, POLLIN, 0}};
        const int r = ::poll(p, 2, 200);
        if (r < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (r == 0) continue;
        for (int i = 0; i < 2; ++i) {
            if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t n = ::recv(p[i].fd, buf, sizeof(buf), 0);
            if (n <= 0) return;
            if (!send_all(i == 0 ? b : a, buf, static_cast<size_t>(n))) return;
        }
    }
}

void socks4_handle(int cfd, Cancel& cancel, const Log& log) {
    uint8_t hdr[8];
    auto fail = [&](const char* why) {
        uint8_t rep[8] = {0, 91, 0, 0, 0, 0, 0, 0};
        send_all(cfd, rep, 8);
        log(std::string("socks4: ") + why);
        ::close(cfd);
    };
    if (read_exact(cfd, hdr, 8, cancel, 3000) != 1) {
        ::close(cfd);
        return;
    }
    if (hdr[0] != 4 || hdr[1] != 1) return fail("unsupported request");
    const uint16_t port = static_cast<uint16_t>((hdr[2] << 8) | hdr[3]);
    auto read_cstr = [&](std::string& s) {
        s.clear();
        for (int i = 0; i < 255; ++i) {
            uint8_t c = 0;
            if (read_exact(cfd, &c, 1, cancel, 3000) != 1) return false;
            if (c == 0) return true;
            s.push_back(static_cast<char>(c));
        }
        return false;
    };
    std::string userid;
    if (!read_cstr(userid)) return fail("bad userid");
    std::string host;
    if (hdr[4] == 0 && hdr[5] == 0 && hdr[6] == 0 && hdr[7] != 0) {
        if (!read_cstr(host) || !valid_host(host)) return fail("bad hostname");
    } else {
        host = sfmt("%u.%u.%u.%u", hdr[4], hdr[5], hdr[6], hdr[7]);
    }
    std::string err;
    int up = connect_tcp(host, port, 5000, cancel, err);
    if (up < 0) return fail(("connect failed " + host + ": " + err).c_str());
    uint8_t ok[8] = {0, 90, hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7]};
    send_all(cfd, ok, 8);
    log(sfmt("CONNECT %s:%u", host.c_str(), port));
    relay(cfd, up, cancel);
    ::close(up);
    ::close(cfd);
}

Worker socks4_worker(std::string param) {
    return [param](Cancel& cancel, const Log& log, const Status& status) {
        std::string addr = "127.0.0.1";
        uint16_t port = 1080;
        const std::string p = trim(param);
        if (p.rfind("bind:", 0) == 0) {
            // Explicit opt-in for a non-loopback listener: bind:ADDR[:PORT]
            std::string spec = p.substr(5);
            const auto colon = spec.rfind(':');
            if (colon != std::string::npos) {
                if (!parse_port(spec.substr(colon + 1), port)) {
                    log("bad port in bind:ADDR[:PORT]");
                    return;
                }
                spec = spec.substr(0, colon);
            }
            in_addr probe{};
            if (inet_pton(AF_INET, spec.c_str(), &probe) != 1) {
                log("bind address must be IPv4");
                return;
            }
            addr = spec;
            if (addr != "127.0.0.1") log("WARNING: proxy is open to the network on " + addr);
        } else if (!p.empty() && !parse_port(p, port)) {
            log("bad port: " + p);
            return;
        }
        std::string err;
        int lfd = listen_on(addr, port, err);
        if (lfd < 0) {
            log("listen failed: " + err);
            status("error");
            return;
        }
        log(sfmt("SOCKS4 on %s:%u", addr.c_str(), port));
        status(sfmt("listening %s:%u", addr.c_str(), port));

        struct Conn {
            std::thread th;
            std::shared_ptr<std::atomic<bool>> done;
        };
        std::vector<Conn> conns;
        while (!cancel) {
            for (auto it = conns.begin(); it != conns.end();) {
                if (it->done->load()) {
                    it->th.join();
                    it = conns.erase(it);
                } else {
                    ++it;
                }
            }
            const int cfd = accept_poll(lfd, cancel, 250);
            if (cfd < 0) continue;
            if (conns.size() >= 16) {
                ::close(cfd);
                continue;
            }
            Conn c;
            c.done = std::make_shared<std::atomic<bool>>(false);
            auto done = c.done;
            Log lg = log;
            c.th = std::thread([cfd, done, lg, &cancel]() {
                socks4_handle(cfd, cancel, lg);
                done->store(true);
            });
            conns.push_back(std::move(c));
        }
        for (auto& c : conns) c.th.join();
        close_fd(lfd);
    };
}

Worker honeypot_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        const uint16_t ports[] = {22, 23, 80};
        std::vector<std::pair<int, uint16_t>> ls;
        for (uint16_t p : ports) {
            std::string err;
            const int fd = listen_on("0.0.0.0", p, err);
            if (fd < 0) log(sfmt("bind %u failed: %s", p, err.c_str()));
            else ls.emplace_back(fd, p);
        }
        if (ls.empty()) {
            log("no ports bound (ports <1024 need root/CAP_NET_BIND_SERVICE)");
            status("error");
            return;
        }
        std::string plist;
        for (auto& l : ls) plist += (plist.empty() ? "" : ",") + std::to_string(l.second);
        log("honeypot on " + plist);
        status("listening " + plist);
        int hits = 0;
        while (!cancel) {
            std::vector<pollfd> pfds;
            for (auto& l : ls) pfds.push_back({l.first, POLLIN, 0});
            if (::poll(pfds.data(), pfds.size(), 250) <= 0) continue;
            for (size_t i = 0; i < pfds.size(); ++i) {
                if (!(pfds[i].revents & POLLIN)) continue;
                sockaddr_in sa{};
                socklen_t len = sizeof(sa);
                int cfd = ::accept(ls[i].first, reinterpret_cast<sockaddr*>(&sa), &len);
                if (cfd < 0) continue;
                ++hits;
                std::string first;
                pollfd cp{cfd, POLLIN, 0};
                if (::poll(&cp, 1, 250) > 0) {
                    char buf[64];
                    const ssize_t n = ::recv(cfd, buf, sizeof(buf), MSG_DONTWAIT);
                    if (n > 0) first.assign(buf, static_cast<size_t>(n));
                }
                send_all(cfd, std::string("honeypot\n"));
                ::close(cfd);
                log(sfmt("%s -> :%u %s", peer_string(sa).c_str(), ls[i].second, shorten(printable(first), 24).c_str()));
                status(sfmt("%d hit(s)", hits));
            }
        }
        for (auto& l : ls) close_fd(l.first);
    };
}

Screen make_tcp_tool(VM& vm, AssetManager& assets, ToolId id, const Stage& st) {
    const char* title = id == ToolId::ListenTcp ? "Listen TCP" : (id == ToolId::Telnet ? "TelNET" : "Client TCP");
    if (st.name != "run") {
        const char* hint = id == ToolId::ListenTcp ? "port to listen on" : "host:port";
        const std::string initial = id == ToolId::Telnet ? "" : (id == ToolId::ListenTcp ? "4444" : "");
        return ask_then(vm, assets, id, title, hint, initial, "run");
    }
    if (id == ToolId::ListenTcp) {
        uint16_t port = 0;
        if (!parse_port(st.arg, port)) return make_info(vm, assets, title, "Invalid port");
        return make_run(vm, assets, title, listen_worker(port));
    }
    return make_run(vm, assets, title, client_worker(st.arg, id == ToolId::Telnet ? 23 : 80, id == ToolId::Telnet));
}

// ---------------------------------------------------------------------------------------------
// Net tools: LanWatch / PineAP hunter / Open networks / AP detail
// ---------------------------------------------------------------------------------------------

Worker lan_watch_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        std::string err;
        const auto route = platform::default_route_iface(err);
        if (route.gateway.empty() || !valid_host(route.gateway)) {
            log("no default gateway" + (err.empty() ? std::string() : ": " + err));
            return;
        }
        const std::string gw = route.gateway;
        log(sfmt("watching gateway %s via %s", gw.c_str(), route.name.c_str()));
        std::string last;
        int polls = 0;
        while (!cancel) {
            std::string out;
            platform::run_shell("ping -n -c 1 -W 1 " + gw + " >/dev/null 2>&1; ip neigh show " + gw + " 2>&1", out);
            std::string mac;
            for (const auto& l : split_lines(out)) {
                mac = neigh_mac(l);
                if (!mac.empty()) break;
            }
            ++polls;
            if (mac.empty()) {
                status(sfmt("gw %s: no MAC (%d)", gw.c_str(), polls));
            } else {
                if (last.empty()) {
                    log("gateway MAC " + mac);
                } else if (mac != last) {
                    log("ALERT gateway MAC changed " + last + " -> " + mac);
                    std::string berr;
                    platform::beep(berr);
                }
                last = mac;
                status(sfmt("gw %s %s (%d)", gw.c_str(), mac.c_str(), polls));
            }
            sleep_cancel(cancel, 3000);
        }
    };
}

Worker pineap_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        std::map<std::string, std::set<std::string>> ssids_by_bssid;
        std::map<std::string, std::set<std::string>> bssids_by_ssid;
        std::set<std::string> reported;
        int scans = 0;
        bool ever_ok = false;
        while (!cancel) {
            std::string err;
            const auto aps = platform::wifi_scan(err, cancel_ptr(cancel));
            if (aps.empty() && !err.empty()) {
                if (cancel) break;
                log("scan error: " + err);
                if (!ever_ok) return;
                sleep_cancel(cancel, 3000);
                continue;
            }
            ever_ok = true;
            ++scans;
            for (const auto& ap : aps) {
                if (ap.ssid.empty() || ap.bssid.empty()) continue;
                ssids_by_bssid[ap.bssid].insert(ap.ssid);
                bssids_by_ssid[ap.ssid].insert(ap.bssid);
            }
            for (const auto& kv : ssids_by_bssid) {
                if (kv.second.size() < 2) continue;
                const std::string key = "b" + kv.first + std::to_string(kv.second.size());
                if (!reported.insert(key).second) continue;
                log(sfmt("PINEAP? %s sends %zu SSIDs", kv.first.c_str(), kv.second.size()));
                for (const auto& s : kv.second) log("  " + shorten(s, 30));
            }
            for (const auto& kv : bssids_by_ssid) {
                if (kv.second.size() < 2) continue;
                const std::string key = "s" + kv.first + std::to_string(kv.second.size());
                if (!reported.insert(key).second) continue;
                log(sfmt("MULTI-BSSID \"%s\" x%zu (mesh or twin)", shorten(kv.first, 20).c_str(), kv.second.size()));
            }
            status(sfmt("scan %d: %zu APs, %zu flagged", scans, aps.size(), reported.size()));
            sleep_cancel(cancel, 3000);
        }
    };
}

struct WifiCache {
    std::mutex mu;
    std::vector<platform::WifiAp> aps;
};

WifiCache& wifi_cache() {
    static WifiCache c;
    return c;
}

std::vector<platform::WifiAp> cached_aps(bool open_only) {
    auto& c = wifi_cache();
    std::lock_guard<std::mutex> lock(c.mu);
    std::vector<platform::WifiAp> out;
    for (const auto& ap : c.aps) {
        if (!open_only || ap.open) out.push_back(ap);
    }
    return out;
}

std::string ap_summary(const platform::WifiAp& ap) {
    return sfmt("%s %ddBm ch%d%s", ap.ssid.empty() ? "<hidden>" : shorten(ap.ssid, 18).c_str(), ap.rssi, ap.channel,
                ap.open ? " open" : "");
}

Worker wifi_scan_worker(bool open_only) {
    return [open_only](Cancel& cancel, const Log& log, const Status& status) {
        status("scanning...");
        std::string err;
        auto aps = platform::wifi_scan(err, cancel_ptr(cancel));
        if (aps.empty() && !err.empty()) {
            log("scan error: " + err);
            return;
        }
        std::sort(aps.begin(), aps.end(), [](const platform::WifiAp& a, const platform::WifiAp& b) { return a.rssi > b.rssi; });
        {
            auto& c = wifi_cache();
            std::lock_guard<std::mutex> lock(c.mu);
            c.aps = aps;
        }
        int shown = 0;
        for (const auto& ap : aps) {
            if (open_only && !ap.open) continue;
            ++shown;
            log(ap_summary(ap));
        }
        if (shown == 0) log(open_only ? "no open networks" : "no networks");
        log("Esc, then reopen the tool to pick one");
        status(sfmt("%d found", shown));
    };
}

Worker wifi_connect_worker(std::string ssid) {
    return [ssid](Cancel&, const Log& log, const Status& status) {
        status("connecting to " + ssid);
        std::string err;
        if (platform::wifi_connect_open(ssid, err)) {
            log("connected to " + ssid);
        } else {
            log("connect failed: " + err);
        }
    };
}

Screen make_open_networks(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::OpenNetworks;
    if (st.name == "scan") return make_run(vm, assets, "Open networks", wifi_scan_worker(true));
    if (st.name == "connect") return make_run(vm, assets, "Connect", wifi_connect_worker(st.arg));
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Scan open networks", "scan"));
    for (const auto& ap : cached_aps(true)) {
        if (ap.ssid.empty()) continue;
        e.push_back(stage_entry(vm, id, "Join " + ap_summary(ap), "connect", ap.ssid));
    }
    return make_options(vm, assets, "Open networks", std::move(e));
}

Screen make_ap_detail(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::ApDetail;
    if (st.name == "scan") return make_run(vm, assets, "AP scan", wifi_scan_worker(false));
    if (st.name == "detail") {
        const auto aps = cached_aps(false);
        const size_t idx = static_cast<size_t>(std::strtoul(st.arg.c_str(), nullptr, 10));
        if (idx >= aps.size()) return make_info(vm, assets, "AP detail", "AP no longer in cache");
        const auto& ap = aps[idx];
        std::vector<std::string> lines = {
            "SSID: " + (ap.ssid.empty() ? std::string("<hidden>") : ap.ssid),
            "BSSID: " + ap.bssid,
            sfmt("Channel: %d", ap.channel),
            sfmt("RSSI: %d dBm", ap.rssi),
            "Security: " + ap.security,
            std::string("Open: ") + (ap.open ? "yes" : "no"),
        };
        return make_results(vm, assets, "AP detail", std::move(lines));
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Scan APs", "scan"));
    const auto aps = cached_aps(false);
    for (size_t i = 0; i < aps.size(); ++i) e.push_back(stage_entry(vm, id, ap_summary(aps[i]), "detail", std::to_string(i)));
    return make_options(vm, assets, "AP detail", std::move(e));
}

// ---------------------------------------------------------------------------------------------
// Net tools: File share / Dead drop (tiny HTTP server)
// ---------------------------------------------------------------------------------------------

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
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') out.push_back(static_cast<char>(c));
        else out += sfmt("%%%02X", c);
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

void send_response(int fd, int code, const char* reason, const std::string& ctype, const std::string& body,
                   bool head_only = false) {
    std::string h = sfmt("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                         "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                         code, reason, ctype.c_str(), body.size());
    send_all(fd, h);
    if (!head_only) send_all(fd, body);
}

bool has_hidden_component(const std::string& rel) {
    std::stringstream ss(rel);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty() && part[0] == '.') return true;
    }
    return false;
}

std::string dir_listing_html(const fs::path& dir, const std::string& url_path, bool writable) {
    std::vector<std::pair<std::string, bool>> entries;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        const std::string name = de.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code ec2;
        entries.emplace_back(name, de.is_directory(ec2));
    }
    std::sort(entries.begin(), entries.end());
    std::string base = url_path;
    if (base.empty() || base.back() != '/') base.push_back('/');
    std::string h = "<!doctype html><meta charset=utf-8><title>kvxputerzero</title><h3>" + html_escape(base) + "</h3><ul>";
    if (base != "/") h += "<li><a href=\"../\">../</a></li>";
    for (const auto& e : entries) {
        h += "<li><a href=\"" + url_encode(base + e.first) + (e.second ? "/" : "") + "\">" + html_escape(e.first) +
             (e.second ? "/" : "") + "</a></li>";
    }
    h += "</ul>";
    if (writable) {
        h += "<hr><input type=file id=f><button onclick=\"var x=f.files[0];if(x)fetch('/'+encodeURIComponent(x.name),"
             "{method:'PUT',body:x}).then(r=>{alert(r.status==201?'uploaded':'failed '+r.status);location.reload()})\">"
             "Upload</button>";
    }
    return h;
}

void serve_file(int fd, const std::string& path, bool head_only, Cancel& cancel) {
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
    const std::string h = sfmt("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\n"
                               "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                               static_cast<long long>(sb.st_size));
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

void accept_upload(int fd, const std::string& path, size_t content_length, const std::string& already, Cancel& cancel,
                   const Log& log) {
    constexpr size_t kMaxUpload = 64u * 1024u * 1024u;
    if (content_length > kMaxUpload) {
        send_response(fd, 413, "Payload Too Large", "text/plain", "too large\n");
        return;
    }
    const int f = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (f < 0) {
        send_response(fd, errno == EEXIST ? 409 : 500, errno == EEXIST ? "Conflict" : "Error", "text/plain", "cannot create\n");
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
        log(sfmt("uploaded %zu bytes", got));
        send_response(fd, 201, "Created", "text/plain", "ok\n");
    } else {
        ::unlink(path.c_str());
        send_response(fd, 400, "Bad Request", "text/plain", "upload failed\n");
    }
}

void http_handle(int fd, const std::string& root, bool writable, Cancel& cancel, const Log& log, const std::string& peer) {
    std::string req;
    char buf[2048];
    int idle = 0;
    size_t head_end = std::string::npos;
    while (!cancel && idle < 30 && req.size() < 16384) {
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
    std::string target = rl[1];
    const auto q = target.find('?');
    if (q != std::string::npos) target = target.substr(0, q);
    const std::string path = url_decode(target);
    log(method + " " + shorten(printable(path), 30) + " " + peer);
    if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos) {
        send_response(fd, 400, "Bad Request", "text/plain", "bad path\n");
        return;
    }
    const std::string rel = path.substr(1);
    if (has_hidden_component(rel)) {
        send_response(fd, 403, "Forbidden", "text/plain", "forbidden\n");
        return;
    }
    std::string err;
    const std::string abs = platform::resolve_under_root(root, rel, err);
    if (abs.empty()) {
        send_response(fd, 403, "Forbidden", "text/plain", "forbidden\n");
        return;
    }
    if (method == "GET" || method == "HEAD") {
        std::error_code ec;
        if (fs::is_directory(abs, ec)) {
            send_response(fd, 200, "OK", "text/html; charset=utf-8", dir_listing_html(abs, path, writable), method == "HEAD");
        } else if (fs::is_regular_file(abs, ec)) {
            serve_file(fd, abs, method == "HEAD", cancel);
        } else {
            send_response(fd, 404, "Not Found", "text/plain", "not found\n");
        }
    } else if (method == "PUT" && writable) {
        if (rel.empty() || rel.find('/') != std::string::npos) {
            send_response(fd, 400, "Bad Request", "text/plain", "flat names only\n");
            return;
        }
        size_t cl = 0;
        const std::string clv = header_value(head, "Content-Length");
        if (!clv.empty()) cl = static_cast<size_t>(std::strtoull(clv.c_str(), nullptr, 10));
        accept_upload(fd, abs, cl, body_start, cancel, log);
    } else {
        send_response(fd, 405, "Method Not Allowed", "text/plain", "not allowed\n");
    }
}

Worker http_share_worker(std::string root, bool writable, std::string label) {
    return [root, writable, label](Cancel& cancel, const Log& log, const Status& status) {
        constexpr uint16_t kPort = 8080;
        std::string err;
        int lfd = listen_on("0.0.0.0", kPort, err);
        if (lfd < 0) {
            log("listen failed: " + err);
            status("error");
            return;
        }
        log(label + " on :8080");
        log("root " + root);
        log(writable ? "PUT uploads enabled" : "read-only, dotfiles hidden");
        status("serving :8080");
        int served = 0;
        while (!cancel) {
            std::string peer;
            int cfd = accept_poll(lfd, cancel, 250, &peer);
            if (cfd < 0) continue;
            http_handle(cfd, root, writable, cancel, log, peer);
            ::close(cfd);
            status(sfmt("serving :8080 (%d req)", ++served));
        }
        close_fd(lfd);
    };
}

// ---------------------------------------------------------------------------------------------
// Net tools: Roku / WireGuard / Printer / UPnP
// ---------------------------------------------------------------------------------------------

struct RokuState {
    std::mutex mu;
    std::string last = "-";
};

RokuState& roku_state() {
    static RokuState s;
    return s;
}

void roku_send_key(const std::string& ip, const std::string& key) {
    std::thread([ip, key]() {
        const auto r = platform::http_request("POST", "http://" + ip + ":8060/keypress/" + key, {}, 2, 256);
        auto& s = roku_state();
        std::lock_guard<std::mutex> lock(s.mu);
        s.last = r.error.empty() ? (r.status == 200 ? key + " OK" : sfmt("%s HTTP %d", key.c_str(), r.status))
                                 : shorten(r.error, 24);
    }).detach();
}

Worker roku_find_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        status("SSDP search...");
        std::string err;
        const auto hits = ssdp_search("roku:ecp", 3000, cancel, err);
        if (!err.empty()) {
            log("ssdp error: " + err);
            return;
        }
        if (hits.empty()) {
            log("no Roku found; set the IP manually");
            return;
        }
        for (const auto& h : hits) log("Roku " + h.from + " " + shorten(h.location, 30));
        write_text_file(tools::config_file("roku_ip.txt"), hits[0].from + "\n");
        log("saved " + hits[0].from + "; Esc and reopen Roku");
    };
}

Screen make_roku(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Roku;
    if (st.name == "setip") {
        return make_prompt(vm, assets, "Roku IP", "IPv4 address of the Roku", read_config_value("roku_ip.txt", ""),
                           [&vm, id](const std::string& v) {
                               const std::string ip = trim(v);
                               if (valid_host(ip)) write_text_file(tools::config_file("roku_ip.txt"), ip + "\n");
                               nav(vm, id, "");
                           });
    }
    if (st.name == "find") return make_run(vm, assets, "Roku SSDP", roku_find_worker());
    const std::string ip = read_config_value("roku_ip.txt", "");
    std::string last;
    {
        auto& s = roku_state();
        std::lock_guard<std::mutex> lock(s.mu);
        last = s.last;
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "IP: " + (ip.empty() ? std::string("(not set)") : ip) + "  [" + last + "]", ""));
    e.push_back(stage_entry(vm, id, "Set IP...", "setip"));
    e.push_back(stage_entry(vm, id, "Discover (SSDP)", "find"));
    if (!ip.empty() && valid_host(ip)) {
        static const char* kKeys[] = {"Home", "Back", "Select", "Up", "Down", "Left", "Right", "Play", "Rev", "Fwd",
                                      "InstantReplay", "Info", "VolumeUp", "VolumeDown", "VolumeMute", "PowerOff"};
        for (const char* k : kKeys) {
            const std::string key = k;
            e.push_back({key, [ip, key]() { roku_send_key(ip, key); }});
        }
    }
    return make_options(vm, assets, "Roku Remote", std::move(e));
}

std::string wg_prefix() {
    return geteuid() == 0 ? "" : "sudo -n ";
}

Screen make_wireguard(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::WireGuard;
    const std::string conf = tools::config_file("wg0.conf");
    if (st.name == "help") {
        return make_info(vm, assets, "WireGuard help",
                         std::string("# Config\n") + conf +
                             "\n\nCopy your wg config there (chmod 600).\n"
                             "Interface name = wg0.\n\n"
                             "# Notes\n"
                             "- wg-quick needs root: uses `sudo -n` unless run as root.\n"
                             "- Needs wireguard-tools installed.");
    }
    if (st.name == "up" || st.name == "down" || st.name == "status") {
        std::string cmd;
        if (st.name == "status") {
            cmd = wg_prefix() + "wg show";
        } else {
            if (!fs::exists(conf)) {
                return make_info_card(vm, assets, "WireGuard",
                                      {{"config", conf}, {"hint", "Copy your wg0.conf there first."}}, "missing",
                                      view::StatusPillKind::Missing);
            }
            cmd = "timeout 30 " + wg_prefix() + "wg-quick " + st.name + " " + shell_quote(conf);
        }
        return make_run(vm, assets, "WireGuard " + st.name, shell_worker("wg " + st.name, cmd));
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Status (wg show)", "status"));
    e.push_back(stage_entry(vm, id, "Up", "up"));
    e.push_back(stage_entry(vm, id, "Down", "down"));
    e.push_back(stage_entry(vm, id, "Config help", "help"));
    return make_options(vm, assets, "WireGuard", std::move(e));
}

Worker printer_ports_worker(std::string ip) {
    return [ip](Cancel& cancel, const Log& log, const Status& status) {
        struct P {
            uint16_t port;
            const char* name;
        };
        static const P kPorts[] = {{9100, "JetDirect/RAW"}, {515, "LPD"}, {631, "IPP"}, {80, "HTTP"}, {443, "HTTPS"}};
        for (const auto& p : kPorts) {
            if (cancel) return;
            status(sfmt("probing %u", p.port));
            const auto r = platform::tcp_probe(ip, p.port, 1000, false);
            log(sfmt("%u %-13s %s", p.port, p.name, r.open ? "OPEN" : "closed"));
        }
    };
}

Worker printer_send_worker(std::string ip, std::string path) {
    return [ip, path](Cancel& cancel, const Log& log, const Status& status) {
        std::string err;
        const std::string abs = platform::resolve_under_root(home_dir(), path, err);
        if (abs.empty()) {
            log("path rejected: " + err);
            return;
        }
        std::ifstream in(abs, std::ios::binary);
        if (!in) {
            log("cannot open " + abs);
            return;
        }
        std::error_code ec;
        const auto size = fs::file_size(abs, ec);
        if (ec || size > 8u * 1024u * 1024u) {
            log("file missing or larger than 8 MB");
            return;
        }
        status("connecting " + ip + ":9100");
        int fd = connect_tcp(ip, 9100, 4000, cancel, err);
        if (fd < 0) {
            log("connect failed: " + err);
            return;
        }
        char buf[4096];
        size_t sent = 0;
        while (in && !cancel) {
            in.read(buf, sizeof(buf));
            const auto n = in.gcount();
            if (n <= 0) break;
            if (!send_all(fd, buf, static_cast<size_t>(n))) {
                log("send failed");
                break;
            }
            sent += static_cast<size_t>(n);
            status(sfmt("sent %zu/%ju", sent, static_cast<uintmax_t>(size)));
        }
        close_fd(fd);
        log(sfmt("sent %zu bytes to %s", sent, ip.c_str()));
    };
}

Screen make_printer(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Printer;
    if (st.name.empty()) {
        return ask_then(vm, assets, id, "Printer", "printer IP", read_config_value("printer_ip.txt", ""), "menu");
    }
    if (st.name == "menu") {
        const std::string ip = st.arg;
        if (!valid_host(ip)) return make_info(vm, assets, "Printer", "Invalid IP");
        write_text_file(tools::config_file("printer_ip.txt"), ip + "\n");
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "Check ports", "ports", ip));
        e.push_back(stage_entry(vm, id, "Send file", "askfile", ip));
        return make_options(vm, assets, "Printer " + ip, std::move(e));
    }
    if (st.name == "ports") return make_run(vm, assets, "Printer ports", printer_ports_worker(st.arg));
    if (st.name == "askfile") {
        const std::string ip = st.arg;
        return make_prompt(vm, assets, "Send file", "path under home -> " + ip + ":9100", "", [&vm, id, ip](const std::string& v) {
            nav(vm, id, "send", ip + "|" + trim(v));
        });
    }
    if (st.name == "send") {
        const auto bar = st.arg.find('|');
        if (bar == std::string::npos) return make_info(vm, assets, "Printer", "Bad request");
        return make_run(vm, assets, "Print", printer_send_worker(st.arg.substr(0, bar), st.arg.substr(bar + 1)));
    }
    return make_info(vm, assets, "Printer", "Unknown step");
}

Worker upnp_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        status("M-SEARCH ssdp:all ...");
        std::string err;
        const auto hits = ssdp_search("ssdp:all", 4000, cancel, err);
        if (!err.empty()) {
            log("ssdp error: " + err);
            return;
        }
        if (hits.empty()) log("no UPnP devices answered");
        std::set<std::string> seen;
        for (const auto& h : hits) {
            const std::string loc = h.location.empty() ? ("(" + h.from + ")") : h.location;
            if (seen.insert(loc).second) log(shorten(loc, 60));
        }
        status(sfmt("%zu location(s)", seen.size()));
    };
}

// ---------------------------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------------------------

enum class BleMode { Scan, Hunter, Sniffer, Flipper, Airtag, Skimmer, DroneId };

std::string hex_digits(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// mfg_hex is continuous hex (company id little-endian + payload), entries joined with ';'.
// Apple company 0x004C => "4c00", Find My type 0x12. Matches firmware parseFindMy().
struct FindMyInfo {
    uint8_t battery = 0xff;
    bool separated = false;
    std::string key_prefix;
};

bool parse_find_my_mfg(const std::string& mfg_hex, FindMyInfo& out) {
    const std::string all = hex_digits(mfg_hex);
    for (size_t i = 0; i + 10 <= all.size(); ++i) {
        if (all.compare(i, 4, "4c00") != 0) continue;
        if (all.compare(i + 4, 2, "12") != 0) continue;
        unsigned status = 0;
        try {
            status = std::stoul(all.substr(i + 8, 2), nullptr, 16);
        } catch (...) {
            continue;
        }
        out.battery = static_cast<uint8_t>((status >> 6) & 0x03);
        out.separated = (status & 0x20) != 0;
        out.key_prefix.clear();
        if (i + 16 <= all.size()) {
            out.key_prefix = all.substr(i + 10, 6);
            for (char& c : out.key_prefix) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return true;
    }
    return false;
}

bool looks_like_flipper(const platform::BleDevice& d) {
    // Mirror firmware looksLikeFlipper(): name, or 16-bit service UUID 0x3080..0x3083
    // in BlueZ UUIDs / ServiceData / raw advert leftovers.
    if (contains_ci(d.name, "flipper")) return true;
    static const char* kIds[] = {"3080", "3081", "3082", "3083"};
    static const char* kFull[] = {
        "00003080-0000-1000-8000-00805f9b34fb",
        "00003081-0000-1000-8000-00805f9b34fb",
        "00003082-0000-1000-8000-00805f9b34fb",
        "00003083-0000-1000-8000-00805f9b34fb",
    };
    for (const char* full : kFull) {
        if (contains_ci(d.services, full)) return true;
    }
    const std::string svc = hex_digits(d.services);
    const std::string mfg = hex_digits(d.mfg_hex);
    for (const char* id : kIds) {
        // Prefer Bluetooth base UUID form (...0000308x00001000...) over random hex hits.
        const std::string base = std::string("0000") + id + "00001000";
        if (svc.find(base) != std::string::npos) return true;
        if (svc.find(id) != std::string::npos) return true;
        if (mfg.find(id) != std::string::npos) return true;
    }
    return false;
}

const char* find_my_battery_label(uint8_t batt) {
    switch (batt) {
    case 0:
        return "FULL";
    case 1:
        return "MED";
    case 2:
        return "LOW";
    case 3:
        return "CRIT";
    default:
        return "?";
    }
}

bool ble_matches(BleMode mode, const platform::BleDevice& d, std::string& tag) {
    switch (mode) {
    case BleMode::Flipper:
        tag = "FLIPPER";
        return looks_like_flipper(d);
    case BleMode::Airtag: {
        FindMyInfo info;
        if (parse_find_my_mfg(d.mfg_hex, info)) {
            tag = std::string("FINDMY ") + find_my_battery_label(info.battery) +
                  (info.separated ? " sep" : " near");
            if (!info.key_prefix.empty()) tag += " " + info.key_prefix;
            return true;
        }
        tag = "FINDMY";
        return contains_ci(d.name, "airtag") || contains_ci(d.name, "find my") ||
               contains_ci(d.name, "findmy");
    }
    case BleMode::Skimmer: {
        tag = "SKIMMER?";
        static const char* kNames[] = {"hc-03", "hc-05", "hc-06", "hc-08", "bluetoothab", "linvor", "jdy-"};
        for (const char* n : kNames) {
            if (contains_ci(d.name, n)) return true;
        }
        return false;
    }
    case BleMode::DroneId:
        tag = contains_ci(d.services, "fffa") ? "ODID" : "";
        return true;
    default:
        tag.clear();
        return true;
    }
}

std::string rssi_bar(int rssi) {
    const int n = std::max(0, std::min(20, (rssi + 100) * 20 / 70));
    return std::string(static_cast<size_t>(n), '#');
}

Worker ble_worker(BleMode mode, std::string target) {
    return [mode, target](Cancel& cancel, const Log& log, const Status& status) {
        std::map<std::string, int> last_rssi;
        bool ever_ok = false;
        bool noted = false;
        int scans = 0;
        // Wall apps need a longer live-discovery window: BlueZ only exposes
        // ManufacturerData/UUIDs/RSSI while Discovering=yes (NimBLE-equivalent).
        const int window =
            (mode == BleMode::Flipper || mode == BleMode::Airtag || mode == BleMode::Skimmer) ? 8 : 4;
        status("scanning...");
        while (!cancel) {
            std::vector<platform::BleDevice> devs;
            std::string err;
            if (!platform::ble_scan_once(devs, err, window, cancel_ptr(cancel))) {
                if (cancel) break;
                log("BLE: " + err);
                if (!ever_ok) return;
                sleep_cancel(cancel, 2000);
                continue;
            }
            ever_ok = true;
            ++scans;
            if (mode == BleMode::DroneId && !noted) {
                log("ODID parse unavailable (no adv data); listing BLE devices");
                noted = true;
            }
            if (mode == BleMode::Flipper && !noted) {
                log("looking for name Flipper or UUID 3080-3083");
                noted = true;
            }
            if (mode == BleMode::Airtag && !noted) {
                log("looking for Apple Find My mfg 4C:00 type 12");
                noted = true;
            }
            if (mode == BleMode::Hunter) {
                const platform::BleDevice* best = nullptr;
                for (const auto& d : devs) {
                    if (!target.empty() && !contains_ci(d.name, target) && !contains_ci(d.address, target)) continue;
                    if (!best || d.rssi > best->rssi) best = &d;
                }
                if (best) log(sfmt("%-14s %4d %s", shorten(best->name, 14).c_str(), best->rssi, rssi_bar(best->rssi).c_str()));
                else log(target.empty() ? "no devices" : "target not seen");
            } else {
                int shown = 0;
                for (const auto& d : devs) {
                    std::string tag;
                    if (!ble_matches(mode, d, tag)) continue;
                    ++shown;
                    const auto it = last_rssi.find(d.address);
                    const bool fresh = it == last_rssi.end();
                    if (mode != BleMode::Sniffer && !fresh && std::abs(it->second - d.rssi) < 8) continue;
                    last_rssi[d.address] = d.rssi;
                    const std::string label =
                        (d.name.empty() || d.name == "(unknown)") ? shorten(d.address, 14) : shorten(d.name, 14);
                    std::string line = sfmt("%s %s %d", label.c_str(), d.address.c_str(), d.rssi);
                    if (mode == BleMode::Airtag) {
                        FindMyInfo info;
                        if (parse_find_my_mfg(d.mfg_hex, info)) {
                            const float meters =
                                std::pow(10.0f, (-59.0f - static_cast<float>(d.rssi)) / 20.0f);
                            const float m = std::max(0.1f, std::min(99.0f, meters));
                            line = sfmt("%s %ddBm ~%.1fm %s%s",
                                        shorten(d.address, 17).c_str(), d.rssi, static_cast<double>(m),
                                        find_my_battery_label(info.battery),
                                        info.separated ? " sep" : " near");
                            if (!info.key_prefix.empty()) line += std::string(" ") + info.key_prefix;
                        }
                    }
                    if (!tag.empty() && mode != BleMode::Airtag) line += " [" + tag + "]";
                    log(line);
                    if (mode == BleMode::Sniffer || mode == BleMode::Flipper) {
                        if (!d.services.empty()) log("  svc " + shorten(d.services, 48));
                        if (!d.mfg_hex.empty()) log("  mfg " + shorten(d.mfg_hex, 48));
                    }
                }
                status(sfmt("scan %d: %d match / %zu seen", scans, shown, devs.size()));
            }
            if (mode == BleMode::Hunter) status(sfmt("scan %d", scans));
        }
    };
}

Screen make_ble_disconnect(VM& vm, AssetManager& assets) {
    std::string err;
    const bool ok = platform::ble_power_off(err);
    screen::InfoScreen::Fields fields = {{"result", ok ? "Bluetooth powered off" : "Could not power off"}};
    if (!err.empty()) fields.push_back({"note", err});
    return make_info_card(vm, assets, "BLE Disconnect", std::move(fields), ok ? "OK" : "error",
                          ok ? view::StatusPillKind::Ok : view::StatusPillKind::Error);
}

// ---------------------------------------------------------------------------------------------
// DTMF / Morse / Microphone
// ---------------------------------------------------------------------------------------------

constexpr float kDtmfRows[4] = {697, 770, 852, 941};
constexpr float kDtmfCols[4] = {1209, 1336, 1477, 1633};
constexpr char kDtmfLayout[4][4] = {{'1', '2', '3', 'A'}, {'4', '5', '6', 'B'}, {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};

Worker dtmf_listen_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        constexpr int kRate = 8000;
        status("listening...");
        char last = 0;
        std::string decoded;
        while (!cancel) {
            std::vector<int16_t> pcm;
            std::string err;
            if (!platform::capture_pcm(pcm, kRate, 120, err, cancel_ptr(cancel))) {
                if (!cancel) log("capture: " + err);
                return;
            }
            if (pcm.size() < 200) continue;
            const double e = energy_of(pcm);
            const double n = static_cast<double>(pcm.size());
            char found = 0;
            if (std::sqrt(e / n) > 300.0) {
                double rp[4];
                double cp[4];
                int rb = 0;
                int cb = 0;
                for (int i = 0; i < 4; ++i) {
                    rp[i] = goertzel_power(pcm.data(), pcm.size(), kDtmfRows[i], kRate);
                    cp[i] = goertzel_power(pcm.data(), pcm.size(), kDtmfCols[i], kRate);
                    if (rp[i] > rp[rb]) rb = i;
                    if (cp[i] > cp[cb]) cb = i;
                }
                const double norm = n * e;
                auto second = [](const double* p, int best) {
                    double s = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (i != best) s = std::max(s, p[i]);
                    }
                    return s;
                };
                if (rp[rb] / norm > 0.1 && cp[cb] / norm > 0.1 && second(rp, rb) < 0.3 * rp[rb] && second(cp, cb) < 0.3 * cp[cb]) {
                    found = kDtmfLayout[rb][cb];
                }
            }
            if (found && found != last) {
                decoded.push_back(found);
                log(sfmt("key %c   seq: %s", found, decoded.c_str()));
                status("decoded " + std::string(1, found));
            }
            last = found;
        }
    };
}

Screen make_dtmf(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Dtmf;
    if (st.name == "listen") return make_run(vm, assets, "DTMF Listen", dtmf_listen_worker());
    if (st.name == "dialer") {
        std::vector<MenuEntry> e;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const char k = kDtmfLayout[r][c];
                const float lo = kDtmfRows[r];
                const float hi = kDtmfCols[c];
                e.push_back({sfmt("%c   (%.0f + %.0f Hz)", k, lo, hi), [&vm, lo, hi]() {
                                 std::string err;
                                 if (!platform::play_dual_tone(lo, hi, 150, err)) nav_info(vm, "Audio: " + err);
                             }});
            }
        }
        return make_options(vm, assets, "DTMF Dialer", std::move(e));
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Dialer", "dialer"));
    e.push_back(stage_entry(vm, id, "Listen", "listen"));
    return make_options(vm, assets, "DTMF", std::move(e));
}

const char* morse_code(char c) {
    static const char* kLetters[26] = {".-",   "-...", "-.-.", "-..",  ".",   "..-.", "--.",  "....", "..",
                                       ".---", "-.-",  ".-..", "--",   "-.",  "---",  ".--.", "--.-", ".-.",
                                       "...",  "-",    "..-",  "...-", ".--", "-..-", "-.--", "--.."};
    static const char* kDigits[10] = {"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    return nullptr;
}

char morse_decode(const std::string& code) {
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (code == morse_code(c)) return c;
    }
    for (char c = '0'; c <= '9'; ++c) {
        if (code == morse_code(c)) return c;
    }
    return '?';
}

int morse_wpm() {
    const int v = std::atoi(read_config_value("morse_wpm.txt", "15").c_str());
    return std::max(5, std::min(40, v == 0 ? 15 : v));
}

Worker morse_play_worker(std::string text, bool raw_symbols) {
    return [text, raw_symbols](Cancel& cancel, const Log& log, const Status& status) {
        const int unit = 1200 / morse_wpm();
        constexpr float kFreq = 700.0f;
        std::string err;
        auto play_symbols = [&](const std::string& sym) {
            for (char s : sym) {
                if (cancel) return false;
                if (s == '.' || s == '-') {
                    if (!platform::play_tone(kFreq, s == '.' ? unit : unit * 3, err)) {
                        log("audio: " + err);
                        return false;
                    }
                    if (!sleep_cancel(cancel, unit)) return false;
                }
            }
            return true;
        };
        if (raw_symbols) {
            log(text);
            status(sfmt("%d WPM", morse_wpm()));
            play_symbols(text);
            return;
        }
        status(sfmt("%d WPM", morse_wpm()));
        for (char c : text) {
            if (cancel) return;
            if (c == ' ') {
                log("/");
                if (!sleep_cancel(cancel, unit * 4)) return;
                continue;
            }
            const char* code = morse_code(c);
            if (!code) continue;
            log(sfmt("%c  %s", std::toupper(static_cast<unsigned char>(c)), code));
            if (!play_symbols(code)) return;
            if (!sleep_cancel(cancel, unit * 2)) return;
        }
        log("done");
    };
}

Worker morse_mic_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        constexpr int kRate = 8000;
        constexpr int kWin = 80; // 10 ms
        const int unit_ms = 1200 / morse_wpm();
        status("listening 8s for ~700 Hz tone...");
        std::vector<int16_t> pcm;
        std::string err;
        if (!platform::capture_pcm(pcm, kRate, 8000, err, cancel_ptr(cancel))) {
            log("capture: " + err);
            return;
        }
        std::vector<double> pw;
        double mx = 0;
        for (size_t i = 0; i + kWin <= pcm.size(); i += kWin) {
            pw.push_back(goertzel_power(pcm.data() + i, kWin, 700.0, kRate));
            mx = std::max(mx, pw.back());
        }
        if (pw.empty() || mx < 1e9) {
            log("no tone detected");
            return;
        }
        std::string text;
        std::string symbol;
        int run = 0;
        bool on = false;
        auto flush_on = [&](int ms) { symbol.push_back(ms < unit_ms * 2 ? '.' : '-'); };
        auto flush_off = [&](int ms) {
            if (ms >= unit_ms * 2 && !symbol.empty()) {
                text.push_back(morse_decode(symbol));
                symbol.clear();
                if (ms >= unit_ms * 5) text.push_back(' ');
            }
        };
        for (double p : pw) {
            const bool now = p > 0.3 * mx;
            if (now == on) {
                ++run;
                continue;
            }
            if (run > 0) {
                if (on) flush_on(run * 10);
                else flush_off(run * 10);
            }
            on = now;
            run = 1;
        }
        if (on && run > 0) flush_on(run * 10);
        if (!symbol.empty()) text.push_back(morse_decode(symbol));
        log("decoded: " + (text.empty() ? std::string("(nothing)") : text));
    };
}

Screen make_morse(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Morse;
    if (st.name == "ask_text") return ask_then(vm, assets, id, "Morse Text", "text to send", "", "text");
    if (st.name == "ask_key") return ask_then(vm, assets, id, "Morse Key", "dots and dashes, e.g. .- -...", "", "key");
    if (st.name == "text") return make_run(vm, assets, "Morse Send", morse_play_worker(st.arg, false));
    if (st.name == "key") return make_run(vm, assets, "Morse Key", morse_play_worker(st.arg, true));
    if (st.name == "mic") return make_run(vm, assets, "Morse Mic", morse_mic_worker());
    if (st.name == "letters") {
        std::vector<std::string> lines;
        for (char c = 'A'; c <= 'Z'; ++c) lines.push_back(sfmt("%c  %s", c, morse_code(c)));
        for (char c = '0'; c <= '9'; ++c) lines.push_back(sfmt("%c  %s", c, morse_code(c)));
        return make_results(vm, assets, "Morse Letters", std::move(lines));
    }
    if (st.name == "ask_speed") {
        return make_prompt(vm, assets, "Morse Speed", "words per minute (5-40)", std::to_string(morse_wpm()),
                           [&vm, id](const std::string& v) {
                               const int w = std::max(5, std::min(40, std::atoi(trim(v).c_str())));
                               write_text_file(tools::config_file("morse_wpm.txt"), std::to_string(w) + "\n");
                               nav(vm, id, "");
                           });
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Text", "ask_text"));
    e.push_back(stage_entry(vm, id, "Key (dots/dashes)", "ask_key"));
    e.push_back(stage_entry(vm, id, "Letters", "letters"));
    e.push_back(stage_entry(vm, id, sfmt("Speed (%d WPM)", morse_wpm()), "ask_speed"));
    e.push_back(stage_entry(vm, id, "Mic (decode)", "mic"));
    return make_options(vm, assets, "Morse", std::move(e));
}

Worker mic_spectrum_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        constexpr int kRate = 8000;
        static const double kBands[] = {250, 500, 1000, 1500, 2000, 2500, 3000, 3500};
        while (!cancel) {
            std::vector<int16_t> pcm;
            std::string err;
            if (!platform::capture_pcm(pcm, kRate, 250, err, cancel_ptr(cancel))) {
                if (!cancel) log("capture: " + err);
                return;
            }
            if (pcm.empty()) continue;
            const double n = static_cast<double>(pcm.size());
            const double rms = std::sqrt(energy_of(pcm) / n);
            status(sfmt("rms %.0f", rms));
            // Two compact rows of four bands per capture keeps the small display readable.
            std::string row;
            int idx = 0;
            for (double f : kBands) {
                const double amp = 2.0 * std::sqrt(std::max(0.0, goertzel_power(pcm.data(), pcm.size(), f, kRate))) / n;
                const int bars = static_cast<int>(std::min(12.0, std::log10(1.0 + amp) * 3.0));
                row += sfmt("%.0f:", f) + std::string(static_cast<size_t>(bars), '|') + " ";
                if (++idx % 4 == 0) {
                    log(row);
                    row.clear();
                }
            }
        }
    };
}

std::string newest_recording() {
    const fs::path dir = fs::path(tools::config_dir()) / "mic";
    std::string best;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (de.path().extension() != ".wav") continue;
        const std::string p = de.path().string();
        if (best.empty() || p > best) best = p;
    }
    return best;
}

Worker mic_record_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        constexpr int kRate = 16000;
        status("recording 5s...");
        std::vector<int16_t> pcm;
        std::string err;
        if (!platform::capture_pcm(pcm, kRate, 5000, err, cancel_ptr(cancel))) {
            log("capture: " + err);
            return;
        }
        const std::string dir = ensure_subdir("mic");
        const std::string path = dir + "/rec-" + timestamp_name() + ".wav";
        if (!platform::write_wav(path, pcm, kRate, err)) {
            log("write failed: " + err);
            return;
        }
        log("saved " + path);
    };
}

Worker mic_play_worker() {
    return [](Cancel&, const Log& log, const Status& status) {
        const std::string path = newest_recording();
        if (path.empty()) {
            log("no recordings yet");
            return;
        }
        status("playing");
        log(path);
        std::string err;
        if (!platform::play_wav(path, err)) log("play failed: " + err);
    };
}

Screen make_microphone(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Microphone;
    if (st.name == "spectrum") return make_run(vm, assets, "Mic Spectrum", mic_spectrum_worker());
    if (st.name == "record") return make_run(vm, assets, "Mic Record", mic_record_worker());
    if (st.name == "play") return make_run(vm, assets, "Mic Play", mic_play_worker());
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Spectrum", "spectrum"));
    e.push_back(stage_entry(vm, id, "Record (5s)", "record"));
    e.push_back(stage_entry(vm, id, "Play last", "play"));
    return make_options(vm, assets, "Microphone", std::move(e));
}

// ---------------------------------------------------------------------------------------------
// MQTT 3.1.1 (minimal)
// ---------------------------------------------------------------------------------------------

struct MqttState {
    std::mutex mu;
    int fd = -1;
    std::string broker;
};

MqttState& mqtt_state() {
    static MqttState s;
    return s;
}

int mqtt_fd() {
    auto& s = mqtt_state();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.fd;
}

std::string mqtt_broker() {
    auto& s = mqtt_state();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.broker;
}

void mqtt_drop(int fd) {
    auto& s = mqtt_state();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.fd == fd && fd >= 0) {
        ::close(fd);
        s.fd = -1;
        s.broker.clear();
    }
}

void mqtt_put_str(std::string& out, const std::string& s) {
    out.push_back(static_cast<char>((s.size() >> 8) & 0xFF));
    out.push_back(static_cast<char>(s.size() & 0xFF));
    out += s;
}

bool mqtt_send(int fd, uint8_t header, const std::string& payload) {
    std::string pkt;
    pkt.push_back(static_cast<char>(header));
    size_t len = payload.size();
    do {
        uint8_t b = len % 128;
        len /= 128;
        if (len > 0) b |= 0x80;
        pkt.push_back(static_cast<char>(b));
    } while (len > 0);
    pkt += payload;
    return send_all(fd, pkt);
}

// 1 = packet read, 0 = nothing (timeout/cancel), -1 = closed or malformed.
int mqtt_read(int fd, Cancel& cancel, int wait_ms, uint8_t& header, std::string& payload) {
    const int r = read_exact(fd, &header, 1, cancel, wait_ms);
    if (r != 1) return r;
    size_t len = 0;
    size_t mult = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = 0;
        if (read_exact(fd, &b, 1, cancel, 3000) != 1) return -1;
        len += (b & 0x7F) * mult;
        mult *= 128;
        if (!(b & 0x80)) break;
    }
    if (len > 65536) return -1;
    payload.assign(len, '\0');
    if (len > 0 && read_exact(fd, reinterpret_cast<uint8_t*>(&payload[0]), len, cancel, 5000) != 1) return -1;
    return 1;
}

Worker mqtt_connect_worker(std::string arg) {
    return [arg](Cancel& cancel, const Log& log, const Status& status) {
        std::string host;
        uint16_t port = 0;
        if (!split_host_port(arg, host, port, 1883)) {
            log("usage: host[:port]");
            return;
        }
        mqtt_drop(mqtt_fd());
        status(sfmt("connecting %s:%u", host.c_str(), port));
        std::string err;
        int fd = connect_tcp(host, port, 5000, cancel, err);
        if (fd < 0) {
            log("connect failed: " + err);
            return;
        }
        std::string p;
        mqtt_put_str(p, "MQTT");
        p.push_back(4);    // level 3.1.1
        p.push_back(0x02); // clean session
        p.push_back(0);    // keepalive 0 = disabled
        p.push_back(0);
        mqtt_put_str(p, sfmt("kvxzero-%04x", static_cast<unsigned>(std::time(nullptr) & 0xFFFF)));
        if (!mqtt_send(fd, 0x10, p)) {
            log("send failed");
            ::close(fd);
            return;
        }
        uint8_t ack[4];
        if (read_exact(fd, ack, 4, cancel, 5000) != 1 || ack[0] != 0x20) {
            log("no CONNACK");
            ::close(fd);
            return;
        }
        if (ack[3] != 0) {
            log(sfmt("broker refused (code %u)", ack[3]));
            ::close(fd);
            return;
        }
        {
            auto& s = mqtt_state();
            std::lock_guard<std::mutex> lock(s.mu);
            s.fd = fd;
            s.broker = sfmt("%s:%u", host.c_str(), port);
        }
        log(sfmt("connected to %s:%u", host.c_str(), port));
        status("connected");
    };
}

Worker mqtt_sub_worker(std::string topic) {
    return [topic](Cancel& cancel, const Log& log, const Status& status) {
        const int fd = mqtt_fd();
        if (fd < 0) {
            log("not connected; use Connect first");
            return;
        }
        std::string p;
        p.push_back(0);
        p.push_back(1);
        mqtt_put_str(p, topic);
        p.push_back(0); // QoS 0
        if (!mqtt_send(fd, 0x82, p)) {
            log("send failed");
            mqtt_drop(fd);
            return;
        }
        log("subscribing " + topic);
        status("sub " + topic);
        while (!cancel) {
            uint8_t hdr = 0;
            std::string pl;
            const int r = mqtt_read(fd, cancel, 200, hdr, pl);
            if (r < 0) {
                log("connection closed");
                mqtt_drop(fd);
                return;
            }
            if (r == 0) continue;
            const int type = hdr >> 4;
            if (type == 9) {
                log("subscribed");
            } else if (type == 3 && pl.size() >= 2) {
                const size_t tl = (static_cast<uint8_t>(pl[0]) << 8) | static_cast<uint8_t>(pl[1]);
                if (pl.size() < 2 + tl) continue;
                const std::string t = pl.substr(2, tl);
                size_t off = 2 + tl;
                const int qos = (hdr >> 1) & 3;
                if (qos > 0 && pl.size() >= off + 2) {
                    std::string ackp = pl.substr(off, 2);
                    off += 2;
                    if (qos == 1) mqtt_send(fd, 0x40, ackp);
                }
                log(shorten(printable(t), 24) + ": " + printable(pl.substr(off)));
            }
        }
    };
}

Worker mqtt_pub_worker(std::string arg) {
    return [arg](Cancel&, const Log& log, const Status&) {
        const int fd = mqtt_fd();
        if (fd < 0) {
            log("not connected; use Connect first");
            return;
        }
        const std::string t = trim(arg);
        const auto sp = t.find(' ');
        const std::string topic = sp == std::string::npos ? t : t.substr(0, sp);
        const std::string msg = sp == std::string::npos ? "" : trim(t.substr(sp + 1));
        if (topic.empty()) {
            log("usage: topic message");
            return;
        }
        std::string p;
        mqtt_put_str(p, topic);
        p += msg;
        if (!mqtt_send(fd, 0x30, p)) {
            log("send failed");
            mqtt_drop(fd);
            return;
        }
        log("published to " + topic);
    };
}

Screen make_mqtt(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::Mqtt;
    if (st.name == "help") {
        return make_info(vm, assets, "MQTT help",
                         "Minimal MQTT 3.1.1, QoS 0, no TLS/auth.\n\n"
                         "# Connect\n"
                         "host[:port] (default 1883)\n\n"
                         "# Subscribe\n"
                         "topic filter (e.g. # or home/+)\n\n"
                         "# Publish\n"
                         "`topic message`\n\n"
                         "# Notes\n"
                         "- Connection stays open between screens.\n"
                         "- Disconnect closes it.");
    }
    if (st.name == "ask_connect") return ask_then(vm, assets, id, "MQTT Connect", "broker host[:port]", mqtt_broker(), "connect");
    if (st.name == "ask_sub") return ask_then(vm, assets, id, "MQTT Subscribe", "topic filter", "#", "sub");
    if (st.name == "ask_pub") return ask_then(vm, assets, id, "MQTT Publish", "topic message", "", "pub");
    if (st.name == "connect") return make_run(vm, assets, "MQTT Connect", mqtt_connect_worker(st.arg));
    if (st.name == "sub") return make_run(vm, assets, "MQTT Sub", mqtt_sub_worker(st.arg.empty() ? "#" : st.arg));
    if (st.name == "pub") return make_run(vm, assets, "MQTT Pub", mqtt_pub_worker(st.arg));
    if (st.name == "disconnect") {
        const int fd = mqtt_fd();
        if (fd >= 0) {
            mqtt_send(fd, 0xE0, {});
            mqtt_drop(fd);
        }
        return make_info(vm, assets, "MQTT", "Disconnected");
    }
    const std::string b = mqtt_broker();
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Help", "help"));
    e.push_back(stage_entry(vm, id, "Connect", "ask_connect"));
    e.push_back(stage_entry(vm, id, "Subscribe", "ask_sub"));
    e.push_back(stage_entry(vm, id, "Publish", "ask_pub"));
    e.push_back(stage_entry(vm, id, "Disconnect", "disconnect"));
    const std::string title = b.empty() ? "MQTT · off" : ("MQTT · " + b);
    return make_options(vm, assets, title, std::move(e));
}

// ---------------------------------------------------------------------------------------------
// LLM chat
// ---------------------------------------------------------------------------------------------

std::string json_escape(const std::string& s) {
    std::string out;
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

// Extract the first string value for "key" (no nesting awareness; good enough for LLM replies).
std::string json_extract_string(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos) return {};
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = body.find('"', pos);
    if (pos == std::string::npos) return {};
    std::string out;
    for (size_t i = pos + 1; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') return out;
        if (c != '\\' || i + 1 >= body.size()) {
            out.push_back(c);
            continue;
        }
        const char n = body[++i];
        switch (n) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back(' '); break;
        case 'r': break;
        case 'u':
            if (i + 4 < body.size()) {
                const long cp = std::strtol(body.substr(i + 1, 4).c_str(), nullptr, 16);
                out.push_back(cp > 0 && cp < 128 ? static_cast<char>(cp) : '?');
                i += 4;
            }
            break;
        default: out.push_back(n);
        }
    }
    return out;
}

Worker llm_chat_worker(std::string prompt) {
    return [prompt](Cancel&, const Log& log, const Status& status) {
        const std::string url = read_config_value("llm_url.txt", "http://127.0.0.1:11434/api/generate");
        const std::string model = read_config_value("llm_model.txt", "llama3.2");
        const bool openai = url.find("/chat/completions") != std::string::npos;
        std::string payload;
        if (openai) {
            payload = "{\"model\":\"" + json_escape(model) + "\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":\"" +
                      json_escape(prompt) + "\"}]}";
        } else {
            payload = "{\"model\":\"" + json_escape(model) + "\",\"stream\":false,\"prompt\":\"" + json_escape(prompt) + "\"}";
        }
        log("> " + prompt);
        status("waiting for " + url + " (<=45s, Esc after reply)");
        const auto r = platform::http_request("POST", url, payload, 45, 65536);
        if (!r.error.empty()) {
            log("error: " + r.error);
            return;
        }
        std::string text = json_extract_string(r.body, openai ? "content" : "response");
        if (text.empty()) text = r.body;
        if (r.status != 200) log(sfmt("HTTP %d", r.status));
        for (const auto& l : split_lines(text)) {
            if (!l.empty()) log(printable(l));
        }
        status("done");
    };
}

Screen make_llm(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::LlmChat;
    if (st.name == "set_url") {
        return make_prompt(vm, assets, "LLM URL", "e.g. http://host:11434/api/generate",
                           read_config_value("llm_url.txt", "http://127.0.0.1:11434/api/generate"),
                           [&vm, id](const std::string& v) {
                               const std::string u = trim(v);
                               if (!u.empty()) write_text_file(tools::config_file("llm_url.txt"), u + "\n");
                               nav(vm, id, "");
                           });
    }
    if (st.name == "set_model") {
        return make_prompt(vm, assets, "LLM model", "model name", read_config_value("llm_model.txt", "llama3.2"),
                           [&vm, id](const std::string& v) {
                               const std::string m = trim(v);
                               if (!m.empty()) write_text_file(tools::config_file("llm_model.txt"), m + "\n");
                               nav(vm, id, "");
                           });
    }
    if (st.name == "ask_chat") return ask_then(vm, assets, id, "LLM Chat", "message", "", "chat");
    if (st.name == "chat") return make_run(vm, assets, "LLM Chat", llm_chat_worker(st.arg));
    if (st.name == "uart") {
        return make_info(vm, assets, "UART LLM",
                         "Accessory: UART-attached LLM module.\n\n"
                         "Wire it to the Grove UART, then use Modules > UART Shell to talk to it.");
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Chat", "ask_chat"));
    e.push_back(stage_entry(vm, id, "Settings: URL", "set_url"));
    e.push_back(stage_entry(vm, id, "Settings: Model", "set_model"));
    e.push_back(stage_entry(vm, id, "UART LLM (accessory)", "uart"));
    return make_options(vm, assets, "LLM Chat", std::move(e));
}

// ---------------------------------------------------------------------------------------------
// Scripts / GPS / Modules
// ---------------------------------------------------------------------------------------------

bool safe_file_name(const std::string& n) {
    return !n.empty() && n.find('/') == std::string::npos && n.find("..") == std::string::npos && n[0] != '.';
}

bool safe_rel_path(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find("..") != std::string::npos) return false;
    for (unsigned char c : rel) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '/')) return false;
    }
    return true;
}

fs::path pcconnect_bundle_root(AssetManager& assets) {
    const fs::path py = assets.resolve("pcconnect/pcconnect.py");
    if (py.empty()) return {};
    return py.parent_path();
}

std::string pcconnect_export_dir() {
    return ensure_subdir("pcconnect");
}

std::vector<std::string> list_pcconnect_scripts(const fs::path& root) {
    std::vector<std::string> out;
    if (root.empty() || !fs::exists(root)) return out;
    static const char* kRootFiles[] = {"pcconnect.py", "protocol.py", "requirements.txt", "README.md"};
    for (const char* name : kRootFiles) {
        if (fs::exists(root / name)) out.emplace_back(name);
    }
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(root / "apps", ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const std::string n = de.path().filename().string();
        if (n.size() > 3 && n.substr(n.size() - 3) == ".py") out.push_back("apps/" + n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool copy_pcconnect_tree(const fs::path& src, const fs::path& dst, std::string& err) {
    std::error_code ec;
    if (src.empty() || !fs::exists(src)) {
        err = "bundled PC Connect scripts not found";
        return false;
    }
    fs::create_directories(dst, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    for (fs::recursive_directory_iterator it(src, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            err = ec.message();
            return false;
        }
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (ec || rel.empty() || rel.string().find("..") != std::string::npos) continue;
        const fs::path target = dst / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                err = "copy failed: " + rel.string() + ": " + ec.message();
                return false;
            }
        }
    }
    return true;
}

Screen make_pcconnect_howto(VM& vm, AssetManager& assets) {
    return make_info(vm, assets, "How to use",
                     "PC Connect drives Cardputer radio tools from a PC over USB.\n"
                     "\n"
                     "# Steps\n"
                     "1. On a Cardputer with kvxputer (full build, not lite), open PC Connect "
                     "(home tile or USB -> PC Connect). Screen shows Link: waiting.\n"
                     "\n"
                     "2. On Linux/macOS PC, copy the scripts (Browse / Export), then:\n"
                     "`pip install -r requirements.txt`\n"
                     "`python3 pcconnect.py`\n"
                     "\n"
                     "3. Cardputer shows Link: yes. Pick a tool on the PC menu. "
                     "q quits; Esc on Cardputer ends the session.\n"
                     "\n"
                     "# Notes\n"
                     "- Optional: `--port /dev/ttyACM0`\n"
                     "- These scripts do not run on kvxputerzero itself — Zero only ships and exports them.");
}

Screen make_pcconnect_needs(VM& vm, AssetManager& assets) {
    return make_info(vm, assets, "What you need",
                     std::string("# Hardware\n") +
                         "- Cardputer with kvxputer installed (PC Connect app)\n"
                         "- Data-capable USB cable\n"
                         "- Linux or macOS PC (Windows not supported)\n"
                         "\n"
                         "# Software on the PC\n"
                         "- Python 3.8+\n"
                         "- pyserial (`pip install -r requirements.txt`)\n"
                         "- User in uucp (Arch) or dialout (Debian/Ubuntu)\n"
                         "\n"
                         "# On the Cardputer\n"
                         "- Open PC Connect before or while the PC script runs\n"
                         "- Close other serial monitors (pio, Arduino, …)\n"
                         "\n"
                         "# Optional modules\n"
                         "- IR RX, CC1101 / RF433R, RFID2/RC522, PN532 — same Config as on-device apps.\n"
                         "\n"
                         "# Export path\n" +
                         pcconnect_export_dir());
}

Screen make_pcconnect_scripts(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::PcConnectScripts;
    const fs::path root = pcconnect_bundle_root(assets);
    if (root.empty()) {
        return make_info(vm, assets, "PC Connect", "Bundled scripts missing from assets.");
    }

    if (st.name == "view") {
        if (!safe_rel_path(st.arg)) return make_info(vm, assets, "PC Connect", "Bad path");
        const fs::path path = root / st.arg;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) return make_info(vm, assets, "PC Connect", "Not found: " + st.arg);
        const std::string text = read_text_file(path.string());
        std::ostringstream body;
        body << "# " << st.arg << "\n\n";
        int n = 0;
        for (const auto& l : split_lines(text)) {
            body << l << '\n';
            if (++n >= 120) {
                body << "\n… truncated …\n";
                break;
            }
        }
        return make_info(vm, assets, st.arg, body.str());
    }

    std::vector<MenuEntry> e;
    e.push_back({"How to use", [&vm]() { vm.open_tool(ToolId::PcConnectHowTo); }});
    e.push_back({"What you need", [&vm]() { vm.open_tool(ToolId::PcConnectNeeds); }});
    e.push_back({"Export all to config", [&vm]() { vm.open_tool(ToolId::PcConnectExport); }});
    for (const auto& rel : list_pcconnect_scripts(root)) {
        e.push_back(stage_entry(vm, id, rel, "view", rel));
    }
    if (e.size() <= 3) e.push_back({"(no scripts found)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "PC Connect scripts", std::move(e));
}

Screen make_pcconnect_export(VM& vm, AssetManager& assets) {
    const fs::path root = pcconnect_bundle_root(assets);
    const std::string dest = pcconnect_export_dir();
    return make_run(vm, assets, "Export PC Connect", [root, dest](Cancel& cancel, const Log& log, const Status& status) {
        (void)cancel;
        status("Copying scripts…");
        if (root.empty()) {
            log("bundled PC Connect scripts not found");
            return;
        }
        std::string err;
        if (!copy_pcconnect_tree(root, dest, err)) {
            log(err.empty() ? "export failed" : err);
            return;
        }
        log("Exported to:");
        log(dest);
        log("");
        log("On a PC:");
        log("  cd " + dest);
        log("  pip install -r requirements.txt");
        log("  python3 pcconnect.py");
        log("");
        log("Cardputer must run PC Connect.");
    });
}

Worker script_worker(std::string name) {
    return [name](Cancel& cancel, const Log& log, const Status& status) {
        const std::string sdir = ensure_subdir("scripts");
        const std::string data_root = ensure_subdir("script-data");
        std::string err;
        const std::string path = platform::resolve_under_root(sdir, name, err);
        if (path.empty() || !safe_file_name(name)) {
            log("bad script name");
            return;
        }
        const std::string text = read_text_file(path);
        if (text.empty() && !fs::exists(path)) {
            log("script not found");
            return;
        }
        log("file ops confined to " + data_root);
        int n = 0;
        for (const auto& raw : split_lines(text)) {
            if (cancel) return;
            ++n;
            const std::string line = trim(raw);
            if (line.empty() || line[0] == '#') continue;
            const auto sp = line.find(' ');
            const std::string cmd = sp == std::string::npos ? line : line.substr(0, sp);
            const std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp + 1));
            status(sfmt("line %d: %s", n, cmd.c_str()));
            if (cmd == "print") {
                log(rest);
            } else if (cmd == "sleep") {
                const int secs = std::max(0, std::min(60, std::atoi(rest.c_str())));
                if (!sleep_cancel(cancel, secs * 1000)) return;
            } else if (cmd == "write") {
                const auto sp2 = rest.find(' ');
                const std::string rel = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
                const std::string content = sp2 == std::string::npos ? "" : trim(rest.substr(sp2 + 1));
                const std::string abs = platform::resolve_under_root(data_root, rel, err);
                if (abs.empty() || rel.find("..") != std::string::npos) {
                    log(sfmt("line %d: path rejected", n));
                    return;
                }
                std::error_code ec;
                fs::create_directories(fs::path(abs).parent_path(), ec);
                if (!write_text_file(abs, content + "\n")) {
                    log(sfmt("line %d: write failed", n));
                    return;
                }
                log("wrote " + rel);
            } else if (cmd == "read") {
                const std::string abs = platform::resolve_under_root(data_root, rest, err);
                if (abs.empty() || rest.find("..") != std::string::npos) {
                    log(sfmt("line %d: path rejected", n));
                    return;
                }
                const std::string content = read_text_file(abs);
                if (content.empty() && !fs::exists(abs)) log(sfmt("line %d: no such file", n));
                else {
                    for (const auto& cl : split_lines(content)) log(printable(cl));
                }
            } else {
                log(sfmt("line %d: unknown command '%s'", n, cmd.c_str()));
                return;
            }
        }
        log("script finished");
    };
}

Screen make_scripts(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::ScriptsRunner;
    if (st.name == "run") {
        if (!safe_file_name(st.arg)) return make_info(vm, assets, "Scripts", "Bad script name");
        return make_run(vm, assets, st.arg, script_worker(st.arg));
    }
    const std::string dir = ensure_subdir("scripts");
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const std::string n = de.path().filename().string();
        if (safe_file_name(n)) names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    std::vector<MenuEntry> e;
    for (const auto& n : names) e.push_back(stage_entry(vm, id, n, "run", n));
    if (e.empty()) e.push_back({"No scripts in " + dir, [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Local scripts", std::move(e));
}

Worker uart_worker(std::string arg) {
    return [arg](Cancel& cancel, const Log& log, const Status& status) {
        const auto toks = split_ws(arg);
        int baud = toks.empty() ? 115200 : std::atoi(toks[0].c_str());
        if (baud <= 0) baud = 115200;
        std::string to_send;
        if (toks.size() > 1) to_send = trim(arg.substr(arg.find(toks[1])));
        const std::string dev = platform::UartSession::default_grove_uart();
        platform::UartSession uart;
        std::string err;
        if (!uart.open(dev, baud, err)) {
            log(sfmt("open %s failed: %s", dev.c_str(), err.c_str()));
            return;
        }
        log(sfmt("%s @ %d baud", dev.c_str(), baud));
        status(sfmt("%s %d", dev.c_str(), baud));
        if (!to_send.empty()) {
            const std::string data = to_send + "\r\n";
            if (!uart.write_bytes(reinterpret_cast<const uint8_t*>(data.data()), data.size(), err)) log("write failed: " + err);
            else log("> " + to_send);
        }
        LineSplitter ls;
        while (!cancel) {
            std::string chunk;
            if (!uart.read_available(chunk, err)) {
                log("read error: " + err);
                break;
            }
            if (!chunk.empty()) ls.feed(chunk, log);
            else ls.emit(log);
            sleep_cancel(cancel, 50);
        }
        uart.close();
    };
}

Screen make_uart_shell(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "run") return make_run(vm, assets, "UART Shell", uart_worker(st.arg));
    return ask_then(vm, assets, ToolId::UartShell, "UART Shell", "baud [text to send], default 115200", "115200", "run");
}

Screen make_bus_scan(VM& vm, AssetManager& assets) {
    return make_info_card(vm, assets, "Bus Scanner", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                        std::string& status, view::StatusPillKind& kind) {
        std::string err;
        const auto hits = platform::scan_all_i2c(err);
        if (!err.empty()) fields.push_back({"I2C", err});
        int last_bus = -2;
        for (const auto& h : hits) {
            if (h.bus != last_bus) {
                fields.push_back({"bus", std::to_string(h.bus)});
                last_bus = h.bus;
            }
            fields.push_back({"addr", sfmt("0x%02X", h.address)});
        }
        if (hits.empty() && err.empty()) fields.push_back({"I2C", "no devices found"});
        fields.push_back({"SPI0", "MOSI GPIO10 (19)"});
        fields.push_back({"SPI0", "MISO GPIO9 (21)"});
        fields.push_back({"SPI0", "SCLK GPIO11 (23)"});
        fields.push_back({"SPI0", "CE0 GPIO8 (24)"});
        fields.push_back({"SPI0", "CE1 GPIO7 (26)"});
        fields.push_back({"note", "Verify against your cap/board."});
        status = hits.empty() ? "empty" : (std::to_string(hits.size()) + " hits");
        kind = hits.empty() ? view::StatusPillKind::Missing : view::StatusPillKind::Ok;
    });
}

Screen make_unit_scroll(VM& vm, AssetManager& assets) {
    std::string out;
    platform::run_shell("systemctl is-active scrolld 2>&1", out);
    std::string state = trim(out);
    if (state.empty()) state = "unknown";
    const bool active = state == "active";
    screen::InfoScreen::Fields fields = {
        {"service", "scrolld"},
        {"state", state},
        {"install", "see Unit_Scroll README"},
        {"docs", "modules_scripts/.../README.md"},
    };
    return make_info_card(vm, assets, "Unit Scroll", std::move(fields), state,
                          active ? view::StatusPillKind::Ok : view::StatusPillKind::Missing);
}

// ---------------------------------------------------------------------------------------------
// Everyday apps: Files / Media / IR / Wi-Fi
// ---------------------------------------------------------------------------------------------

bool split_root_rel(const std::string& arg, std::string& root, std::string& rel) {
    root.clear();
    rel.clear();
    if (arg.empty()) return false;
    const auto c = arg.find(':');
    if (c == std::string::npos) {
        root = arg;
        return true;
    }
    root = arg.substr(0, c);
    rel = arg.substr(c + 1);
    return !root.empty();
}

std::string join_root_rel(const std::string& root, const std::string& rel) {
    if (rel.empty()) return root;
    return root + ":" + rel;
}

std::string parent_rel(const std::string& rel) {
    if (rel.empty()) return {};
    const auto slash = rel.find_last_of('/');
    if (slash == std::string::npos) return {};
    return rel.substr(0, slash);
}

std::string fmt_bytes(uintmax_t n) {
    if (n < 1024ull) return sfmt("%llu B", static_cast<unsigned long long>(n));
    if (n < 1024ull * 1024ull) return sfmt("%.1f KB", static_cast<double>(n) / 1024.0);
    if (n < 1024ull * 1024ull * 1024ull) return sfmt("%.1f MB", static_cast<double>(n) / (1024.0 * 1024.0));
    return sfmt("%.1f GB", static_cast<double>(n) / (1024.0 * 1024.0 * 1024.0));
}

std::vector<std::string> list_mount_roots() {
    std::vector<std::string> out;
    const char* home = std::getenv("HOME");
    if (home && *home) out.push_back(home);
    for (const char* base : {"/media", "/mnt"}) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        for (const auto& de : fs::directory_iterator(base, ec)) {
            std::error_code ec2;
            if (de.is_directory(ec2)) out.push_back(de.path().string());
        }
    }
    return out;
}

bool looks_binary(const std::string& data) {
    for (unsigned char c : data) {
        if (c == 0) return true;
    }
    return false;
}

Screen make_files_browser(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::FilesBrowser;

    if (st.name == "preview" || st.name == "file" || st.name == "confirmdel" || st.name == "dodel") {
        std::string root, rel;
        if (!split_root_rel(st.arg, root, rel) || rel.empty()) {
            return make_info(vm, assets, "Files", "Bad path");
        }
        std::string err;
        const std::string path = platform::resolve_under_root(root, rel, err);
        if (path.empty() || !platform::path_is_under_root(root, path)) {
            return make_info(vm, assets, "Files", "Path rejected: " + (err.empty() ? std::string("escapes root") : err));
        }

        if (st.name == "preview") {
            std::ifstream in(path, std::ios::binary);
            if (!in) return make_info(vm, assets, "Preview", "Cannot open");
            std::string data(4096, '\0');
            in.read(data.data(), static_cast<std::streamsize>(data.size()));
            data.resize(static_cast<size_t>(std::max<std::streamsize>(0, in.gcount())));
            if (data.empty() || looks_binary(data)) {
                return make_info(vm, assets, shorten(rel, 28), "(binary/empty)");
            }
            return make_info(vm, assets, shorten(rel, 28), data);
        }

        if (st.name == "file") {
            std::vector<MenuEntry> e;
            e.push_back(stage_entry(vm, id, "Preview", "preview", join_root_rel(root, rel)));
            e.push_back(stage_entry(vm, id, "Delete...", "confirmdel", join_root_rel(root, rel)));
            e.push_back(stage_entry(vm, id, "Back", "browse", join_root_rel(root, parent_rel(rel))));
            return make_options(vm, assets, shorten(rel, 28), std::move(e));
        }

        if (st.name == "confirmdel") {
            std::vector<MenuEntry> e;
            e.push_back(stage_entry(vm, id, "Yes, delete", "dodel", join_root_rel(root, rel)));
            e.push_back(stage_entry(vm, id, "No", "browse", join_root_rel(root, parent_rel(rel))));
            return make_options(vm, assets, "Delete?", std::move(e));
        }

        // dodel
        {
            std::error_code ec;
            fs::remove_all(path, ec);
            if (ec) return make_info(vm, assets, "Delete", "Failed: " + ec.message());
            return make_info(vm, assets, "Delete", "Deleted " + rel);
        }
    }

    if (st.name == "browse") {
        std::string root, rel;
        if (!split_root_rel(st.arg, root, rel)) return make_info(vm, assets, "Files", "Bad root");
        std::string err;
        const std::string dir = platform::resolve_under_root(root, rel, err);
        if (dir.empty() || !platform::path_is_under_root(root, dir)) {
            return make_info(vm, assets, "Files", "Path rejected: " + (err.empty() ? std::string("escapes root") : err));
        }
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return make_info(vm, assets, "Files", "Not a directory");

        struct Row {
            std::string name;
            bool is_dir{false};
            uintmax_t size{0};
        };
        std::vector<Row> rows;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            const std::string name = de.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code ec2;
            Row r;
            r.name = name;
            r.is_dir = de.is_directory(ec2);
            if (!r.is_dir) {
                std::error_code ec3;
                r.size = fs::file_size(de.path(), ec3);
            }
            rows.push_back(std::move(r));
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return lower(a.name) < lower(b.name);
        });

        std::vector<MenuEntry> e;
        e.push_back({"Root: " + shorten(root, 40), [&vm]() { vm.go_back(); }});
        if (!rel.empty()) {
            e.push_back(stage_entry(vm, id, "../", "browse", join_root_rel(root, parent_rel(rel))));
        } else {
            e.push_back(stage_entry(vm, id, "Change root...", "roots"));
        }
        for (const auto& r : rows) {
            const std::string child = rel.empty() ? r.name : (rel + "/" + r.name);
            if (r.is_dir) {
                e.push_back(stage_entry(vm, id, r.name + "/", "browse", join_root_rel(root, child)));
            } else {
                e.push_back(stage_entry(vm, id, r.name + "  " + fmt_bytes(r.size), "file", join_root_rel(root, child)));
            }
        }
        return make_options(vm, assets, "Browse", std::move(e));
    }

    // roots (default)
    std::vector<MenuEntry> e;
    for (const auto& root : list_mount_roots()) {
        e.push_back(stage_entry(vm, id, root, "browse", root));
    }
    if (e.empty()) e.push_back({"(no roots)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Files", std::move(e));
}

bool is_audio_ext(const std::string& ext) {
    const std::string e = lower(ext);
    return e == ".wav" || e == ".mp3" || e == ".flac" || e == ".opus";
}

Worker media_play_worker(std::string path) {
    return [path](Cancel&, const Log& log, const Status& status) {
        status("playing");
        log(path);
        const auto ext = lower(fs::path(path).extension().string());
        if (ext != ".wav") log("non-wav: play_wav may fail");
        std::string err;
        if (!platform::play_wav(path, err)) log("play failed: " + err);
        else log("done");
    };
}

Screen make_media_player(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::MediaPlayer;
    const std::string media = ensure_subdir("media");

    if (st.name == "play") {
        if (st.arg.empty()) return make_info(vm, assets, "Media", "No file");
        if (!platform::path_is_under_root(media, st.arg) && !platform::path_is_under_root(home_dir(), st.arg)) {
            return make_info(vm, assets, "Media", "Path not under media/home");
        }
        return make_run(vm, assets, "Play", media_play_worker(st.arg));
    }

    auto list_audio = [&](const std::string& root, const std::string& title) -> Screen {
        std::vector<MenuEntry> e;
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            e.push_back({"(missing " + root + ")", [&vm]() { vm.go_back(); }});
            return make_options(vm, assets, title, std::move(e));
        }
        std::vector<fs::path> files;
        for (const auto& de : fs::directory_iterator(root, ec)) {
            std::error_code ec2;
            if (!de.is_regular_file(ec2)) continue;
            if (!is_audio_ext(de.path().extension().string())) continue;
            files.push_back(de.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& p : files) {
            const std::string path = p.string();
            const std::string label = p.filename().string();
            e.push_back(stage_entry(vm, id, label, "play", path));
        }
        if (e.empty()) e.push_back({"(no audio files)", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, title, std::move(e));
    };

    if (st.name == "media") return list_audio(media, "Media folder");
    if (st.name == "home") return list_audio(home_dir(), "Home audio");

    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Browse media folder", "media"));
    e.push_back(stage_entry(vm, id, "Browse home", "home"));
    return make_options(vm, assets, "Media Player", std::move(e));
}

struct IrLearnPending {
    std::mutex mu;
    bool valid{false};
    uint16_t address{0};
    uint16_t command{0};
};

IrLearnPending& ir_learn_pending() {
    static IrLearnPending p;
    return p;
}

std::string ir_entry_label(const tools::ir::Entry& e) {
    return sfmt("%s  0x%X/0x%X", shorten(e.name, 18).c_str(), static_cast<unsigned>(e.address),
                static_cast<unsigned>(e.command));
}

void ir_send_entry(VM& vm, const tools::ir::Entry& e) {
    const auto r = platform::send_nec_packet(e.address, e.command);
    nav_info(vm, r.ok ? sfmt("Sent %s", e.name.c_str()) : ("Send failed: " + r.error));
}

Screen make_ir_entry_list(VM& vm, AssetManager& assets, const std::string& title,
                          const std::vector<tools::ir::Entry>& entries, const std::string& /*note*/ = {}) {
    std::vector<MenuEntry> e;
    for (const auto& ent : entries) {
        e.push_back({ir_entry_label(ent), [&vm, ent]() { ir_send_entry(vm, ent); }});
    }
    if (entries.empty()) e.push_back({"(no remotes)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, title, std::move(e));
}

Screen make_ir_use(VM& vm, AssetManager& assets) {
    const auto tx = platform::read_sender_info();
    if (!tx.present) {
        return make_info_card(vm, assets, "IR Use",
                              {{"sender", tx.error.empty() ? "not present" : tx.error},
                               {"hint", "Attach an IR TX accessory"}},
                              "missing", view::StatusPillKind::Missing);
    }
    std::vector<tools::ir::Entry> entries;
    std::string err;
    if (!tools::ir::list_all_entries(entries, err) && entries.empty()) {
        return make_info(vm, assets, "IR Use", err.empty() ? "Cannot load remotes" : err);
    }
    return make_ir_entry_list(vm, assets, "IR Use", entries);
}

Worker ir_learn_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        const auto rx = platform::read_receiver_info();
        if (!rx.present) {
            log(rx.error.empty() ? "No IR receiver" : rx.error);
            return;
        }
        platform::IrReceiverSession session;
        if (!session.start()) {
            log("start failed: " + session.last_error());
            return;
        }
        status("waiting for packet...");
        log("point remote and press a button");
        while (!cancel) {
            const auto pkt = session.poll();
            if (pkt.valid) {
                {
                    auto& p = ir_learn_pending();
                    std::lock_guard<std::mutex> lock(p.mu);
                    p.valid = true;
                    p.address = pkt.address;
                    p.command = pkt.command;
                }
                log(sfmt("got addr=0x%X cmd=0x%X", static_cast<unsigned>(pkt.address),
                         static_cast<unsigned>(pkt.command)));
                log("Esc, reopen Learn to name it");
                status("captured");
                session.stop();
                return;
            }
            sleep_cancel(cancel, 40);
        }
        session.stop();
        log("cancelled");
    };
}

Screen make_ir_learn(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::IrLearn;
    if (st.name == "listen") return make_run(vm, assets, "IR Learn", ir_learn_worker());
    if (st.name == "save") {
        auto& p = ir_learn_pending();
        uint16_t addr = 0, cmd = 0;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(p.mu);
            ok = p.valid;
            addr = p.address;
            cmd = p.command;
        }
        if (!ok) return make_info(vm, assets, "IR Learn", "No pending packet");
        std::string name = trim(st.arg);
        for (char& c : name) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '\\') c = '_';
        }
        if (name.empty()) return make_info(vm, assets, "IR Learn", "Name required");
        tools::ir::Entry ent;
        ent.name = name;
        ent.address = addr;
        ent.command = cmd;
        std::string err;
        if (!tools::ir::append_remote(ent, err)) return make_info(vm, assets, "IR Learn", "Save failed: " + err);
        {
            std::lock_guard<std::mutex> lock(p.mu);
            p.valid = false;
        }
        return make_info(vm, assets, "IR Learn", "Saved " + name);
    }

    bool pending = false;
    uint16_t addr = 0, cmd = 0;
    {
        auto& p = ir_learn_pending();
        std::lock_guard<std::mutex> lock(p.mu);
        pending = p.valid;
        addr = p.address;
        cmd = p.command;
    }
    if (pending || st.name == "name") {
        return make_prompt(vm, assets, "Remote name",
                           sfmt("name for 0x%X/0x%X (no spaces)", static_cast<unsigned>(addr), static_cast<unsigned>(cmd)), "",
                           [&vm, id](const std::string& v) { nav(vm, id, "save", trim(v)); });
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Listen for packet", "listen"));
    return make_options(vm, assets, "IR Learn", std::move(e));
}

Screen make_ir_browse(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::IrBrowse;
    if (st.name == "file") {
        std::vector<tools::ir::Entry> entries;
        std::string err;
        if (!tools::ir::load_remote_file(st.arg, entries, err)) {
            return make_info(vm, assets, "IR Browse", err.empty() ? "Cannot load" : err);
        }
        return make_ir_entry_list(vm, assets, st.arg, entries, "NEC only");
    }
    if (st.name == "remotes") {
        std::vector<tools::ir::Entry> remotes;
        std::string e2;
        tools::ir::load_remotes(remotes, e2);
        return make_ir_entry_list(vm, assets, "remotes.txt", remotes, "NEC only");
    }
    std::string err;
    const auto files = tools::ir::list_remote_files(err);
    std::vector<MenuEntry> e;
    e.push_back({"NEC remotes in config/ir", [&vm]() { vm.go_back(); }});
    for (const auto& f : files) e.push_back(stage_entry(vm, id, f, "file", f));
    std::vector<tools::ir::Entry> all;
    if (tools::ir::load_remotes(all, err) && !all.empty()) {
        e.push_back(stage_entry(vm, id, "remotes.txt", "remotes"));
    }
    if (e.size() <= 1) e.push_back({"(no .ir/.txt files)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "IR Browse", std::move(e));
}

Worker ir_read_worker() {
    return [](Cancel& cancel, const Log& log, const Status& status) {
        const auto rx = platform::read_receiver_info();
        if (!rx.present) {
            log(rx.error.empty() ? "No IR receiver" : rx.error);
            return;
        }
        platform::IrReceiverSession session;
        if (!session.start()) {
            log("start failed: " + session.last_error());
            return;
        }
        status("listening...");
        log("Esc to stop");
        while (!cancel) {
            const auto pkt = session.poll();
            if (pkt.valid) {
                log(sfmt("addr=0x%X cmd=0x%X", static_cast<unsigned>(pkt.address),
                         static_cast<unsigned>(pkt.command)));
            }
            sleep_cancel(cancel, 40);
        }
        session.stop();
    };
}

Screen make_ir_favorites(VM& vm, AssetManager& assets) {
    std::vector<tools::ir::Entry> entries;
    std::string err;
    if (!tools::ir::load_remotes(entries, err) || entries.empty()) {
        tools::ir::list_all_entries(entries, err);
    }
    if (entries.size() > 8) entries.resize(8);
    return make_ir_entry_list(vm, assets, "Favorites", entries);
}

struct WifiPskPending {
    std::mutex mu;
    std::string ssid;
    std::string psk;
};

WifiPskPending& wifi_psk_pending() {
    static WifiPskPending p;
    return p;
}

Worker wifi_connect_psk_worker(std::string ssid, std::string psk) {
    return [ssid, psk](Cancel&, const Log& log, const Status& status) {
        status("connecting to " + ssid);
        std::string err;
        if (platform::wifi_connect_psk(ssid, psk, err)) log("connected to " + ssid);
        else log("connect failed: " + err);
    };
}

Worker wifi_disconnect_worker(bool radio_off) {
    return [radio_off](Cancel&, const Log& log, const Status& status) {
        status(radio_off ? "turning radio off..." : "disconnecting...");
        std::string err;
        const bool ok = radio_off ? platform::wifi_radio_off(err) : platform::wifi_disconnect(err);
        if (ok) log(radio_off ? "radio off" : "disconnected");
        else log("failed: " + err);
    };
}

Screen make_wifi_connect(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::WifiConnect;
    if (st.name == "scan") return make_run(vm, assets, "Wi-Fi scan", wifi_scan_worker(false));
    if (st.name == "open") return make_run(vm, assets, "Connect", wifi_connect_worker(st.arg));
    if (st.name == "psk") {
        const std::string ssid = st.arg;
        return make_prompt(vm, assets, "Password", "PSK for " + ssid, "", [&vm, id, ssid](const std::string& v) {
            {
                auto& p = wifi_psk_pending();
                std::lock_guard<std::mutex> lock(p.mu);
                p.ssid = ssid;
                p.psk = v;
            }
            nav(vm, id, "dopsk");
        });
    }
    if (st.name == "dopsk") {
        std::string ssid, psk;
        {
            auto& p = wifi_psk_pending();
            std::lock_guard<std::mutex> lock(p.mu);
            ssid = p.ssid;
            psk = p.psk;
        }
        return make_run(vm, assets, "Connect", wifi_connect_psk_worker(std::move(ssid), std::move(psk)));
    }

    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Scan networks", "scan"));
    for (const auto& ap : cached_aps(false)) {
        if (ap.ssid.empty()) continue;
        if (ap.open) {
            e.push_back(stage_entry(vm, id, "Join " + ap_summary(ap), "open", ap.ssid));
        } else {
            e.push_back(stage_entry(vm, id, "Join " + ap_summary(ap), "psk", ap.ssid));
        }
    }
    return make_options(vm, assets, "Wi-Fi Connect", std::move(e));
}

Screen make_wifi_disconnect(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::WifiDisconnect;
    if (st.name == "now") return make_run(vm, assets, "Disconnect", wifi_disconnect_worker(false));
    if (st.name == "radio") return make_run(vm, assets, "Radio off", wifi_disconnect_worker(true));
    if (st.name == "confirmradio") {
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "Yes, turn radio off", "radio"));
        e.push_back({ "No", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, "Radio off?", std::move(e));
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Disconnect active", "now"));
    e.push_back(stage_entry(vm, id, "Turn radio off...", "confirmradio"));
    return make_options(vm, assets, "Wi-Fi Disconnect", std::move(e));
}

std::string wifi_active_ssid() {
    std::string out;
    platform::run_shell("nmcli -t -f ACTIVE,SSID dev wifi 2>/dev/null | awk -F: '$1==\"yes\"{print $2; exit}'", out);
    return trim(out);
}

Screen make_wifi_ap_info(VM& vm, AssetManager& assets) {
    return make_info_card(vm, assets, "AP info", [](Cancel&, screen::InfoScreen::Fields& fields,
                                                    std::string& status, view::StatusPillKind& kind) {
        std::string err;
        const auto iface = platform::default_route_iface(err);
        if (!err.empty() && iface.name.empty()) {
            fields.push_back({"error", err});
            status = "error";
            kind = view::StatusPillKind::Error;
            return;
        }
        const std::string ssid = wifi_active_ssid();
        if (!ssid.empty()) fields.push_back({"SSID", ssid});
        fields.push_back({"iface", iface.name.empty() ? "(none)" : iface.name});
        fields.push_back({"kind", iface.kind});
        fields.push_back({"state", iface.state});
        fields.push_back({"ipv4", iface.ipv4.empty() ? "-" : iface.ipv4});
        if (iface.prefix >= 0) fields.push_back({"prefix", std::to_string(iface.prefix)});
        fields.push_back({"gateway", iface.gateway.empty() ? "-" : iface.gateway});
        fields.push_back({"dns", iface.dns.empty() ? "-" : iface.dns});
        fields.push_back({"hw", iface.hw_address.empty() ? "-" : iface.hw_address});
        std::string list_err;
        const auto all = platform::list_net_ifaces(list_err);
        for (const auto& i : all) {
            fields.push_back({i.name, (i.ipv4.empty() ? "-" : i.ipv4) + "  " + i.kind});
        }
        status = iface.state.empty() ? "ok" : iface.state;
        kind = view::StatusPillKind::Ok;
    });
}

// ---------------------------------------------------------------------------------------------
// Phase gap tools: loot / handshakes / NTLM / IR extras / STA probes / accessory
// ---------------------------------------------------------------------------------------------

std::string loot_dir() {
    return ensure_subdir("loot");
}

std::string redact_line(std::string s) {
    const auto lower_s = lower(s);
    auto p = lower_s.find("password");
    if (p != std::string::npos) {
        auto colon = s.find(':', p);
        if (colon != std::string::npos) {
            auto end = s.find(',', colon);
            if (end == std::string::npos) end = s.size();
            for (size_t i = colon + 1; i < end; ++i) {
                if (s[i] != ' ') s[i] = '*';
            }
        }
    }
    if (s.find("::") != std::string::npos && s.size() > 40) return s.substr(0, 24) + "...[redacted]";
    return s;
}

std::string ntlm_mask_line(const std::string& line) {
    // Show text before the second colon, then ***.
    size_t first = line.find(':');
    if (first == std::string::npos) return "***";
    size_t second = line.find(':', first + 1);
    if (second == std::string::npos) return line.substr(0, first + 1) + "***";
    return line.substr(0, second + 1) + "***";
}

Screen make_loot_console(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::LootConsole;
    const std::string dir = loot_dir();
    if (st.name == "view") {
        if (!safe_file_name(st.arg)) return make_info(vm, assets, "Loot", "Bad file name");
        std::string err;
        const std::string path = platform::resolve_under_root(dir, st.arg, err);
        if (path.empty()) return make_info(vm, assets, "Loot", "Path rejected");
        std::ifstream in(path);
        if (!in) return make_info(vm, assets, "Loot", "Cannot open");
        std::vector<std::string> lines;
        std::string l;
        while (std::getline(in, l) && lines.size() < 100) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(redact_line(l));
        }
        return make_results(vm, assets, st.arg, std::move(lines));
    }
    if (st.name == "delconfirm") {
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "Yes, delete", "delete", st.arg));
        e.push_back({ "No", [&vm]() { vm.go_back(); }});
        return make_options(vm, assets, "Delete?", std::move(e));
    }
    if (st.name == "delete") {
        if (!safe_file_name(st.arg)) return make_info(vm, assets, "Loot", "Bad file name");
        std::string err;
        const std::string path = platform::resolve_under_root(dir, st.arg, err);
        std::error_code ec;
        if (!path.empty()) fs::remove(path, ec);
        return make_info(vm, assets, "Loot", ec ? "Delete failed" : "Deleted");
    }
    if (st.name == "export") {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            std::error_code ec2;
            if (!de.is_regular_file(ec2)) continue;
            const auto ext = de.path().extension().string();
            if (ext != ".txt" && ext != ".csv") continue;
            names.push_back(de.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        const std::string out_path = (fs::path(dir) / ("merged_" + timestamp_name() + ".csv")).string();
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out) return make_info(vm, assets, "Loot", "Export failed");
        out << "name,preview\n";
        for (const auto& n : names) {
            const std::string path = (fs::path(dir) / n).string();
            std::ifstream in(path);
            std::string first;
            if (in) std::getline(in, first);
            if (!first.empty() && first.back() == '\r') first.pop_back();
            const std::string prev = redact_line(first.substr(0, 48));
            out << '"' << n << "\",\"" << prev << "\"\n";
        }
        return make_info(vm, assets, "Loot", "Exported " + out_path);
    }
    if (st.name == "entry") {
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "View", "view", st.arg));
        e.push_back(stage_entry(vm, id, "Delete...", "delconfirm", st.arg));
        return make_options(vm, assets, st.arg, std::move(e));
    }
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const auto ext = de.path().extension().string();
        if (ext != ".txt" && ext != ".csv") continue;
        if (safe_file_name(de.path().filename().string())) names.push_back(de.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Export merged CSV", "export"));
    for (const auto& n : names) {
        std::ifstream in((fs::path(dir) / n).string());
        std::string first;
        if (in) std::getline(in, first);
        if (!first.empty() && first.back() == '\r') first.pop_back();
        const std::string label = n + ": " + redact_line(first).substr(0, 16);
        e.push_back(stage_entry(vm, id, label, "entry", n));
    }
    if (names.empty()) e.push_back({"No .txt/.csv in " + dir, [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Loot Console", std::move(e));
}

int count_eapol_markers(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;
    std::vector<char> buf(256 * 1024);
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto n = static_cast<size_t>(in.gcount());
    int hits = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (static_cast<uint8_t>(buf[i]) == 0x88 && static_cast<uint8_t>(buf[i + 1]) == 0x8e) ++hits;
    }
    return hits;
}

Screen make_check_handshakes(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::CheckHandshakes;
    const std::string dir = captures_dir();
    if (st.name == "check") {
        if (!safe_file_name(st.arg)) return make_info(vm, assets, "Handshake", "Bad file name");
        std::string err;
        const std::string path = platform::resolve_under_root(dir, st.arg, err);
        if (path.empty()) return make_info(vm, assets, "Handshake", "Path rejected");
        const int hits = count_eapol_markers(path);
        if (hits < 0) return make_info(vm, assets, "Handshake", "Cannot open");
        return make_info(vm, assets, st.arg, sfmt("EAPOL markers: %d", hits));
    }
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!de.is_regular_file(ec2)) continue;
        const auto ext = lower(de.path().extension().string());
        if (ext != ".pcap" && ext != ".cap") continue;
        if (safe_file_name(de.path().filename().string())) names.push_back(de.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    std::vector<MenuEntry> e;
    for (const auto& n : names) e.push_back(stage_entry(vm, id, n, "check", n));
    if (e.empty()) e.push_back({"No .pcap files", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Check Handshakes", std::move(e));
}

Screen make_ntlm_clean(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::NtlmClean;
    const std::string path = (fs::path(loot_dir()) / "ntlm.txt").string();
    if (st.name == "clean") {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string l;
        while (in && std::getline(in, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            l = trim(l);
            if (l.empty() || l[0] == '#') continue;
            lines.push_back(l);
        }
        std::set<std::string> uniq;
        std::vector<std::string> out;
        for (const auto& h : lines) {
            if (uniq.insert(h).second) out.push_back(h);
        }
        std::ofstream o(path, std::ios::binary | std::ios::trunc);
        if (!o) return make_info(vm, assets, "NTLM Clean", "Write failed");
        for (const auto& h : out) o << h << '\n';
        return make_info(vm, assets,
                         "NTLM Clean",
                         sfmt("Kept %d unique\nDropped %d dups", static_cast<int>(out.size()),
                              static_cast<int>(lines.size()) - static_cast<int>(out.size())));
    }
    if (st.name == "list") {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string l;
        while (in && std::getline(in, l) && lines.size() < 100) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            l = trim(l);
            if (l.empty() || l[0] == '#') continue;
            lines.push_back(ntlm_mask_line(l));
        }
        if (lines.empty()) lines.push_back("No hashes in loot/ntlm.txt");
        return make_results(vm, assets, "NTLM hashes", std::move(lines));
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "List (masked)", "list"));
    e.push_back(stage_entry(vm, id, "Clean duplicates", "clean"));
    return make_options(vm, assets, "NTLM Clean", std::move(e));
}

Screen make_tv_b_gone(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::TvBGone;
    if (st.name == "run") {
        const bool eu = st.arg == "EU";
        return make_run(vm, assets, eu ? "TV-B-Gone EU" : "TV-B-Gone NA",
                        [eu](Cancel& cancel, const Log& log, const Status& status) {
                            const auto sender = platform::read_sender_info();
                            if (!sender.present) {
                                log(sender.error.empty() ? "IR unavailable" : sender.error);
                                return;
                            }
                            std::string err;
                            tools::tvbgone::run_blast(
                                eu ? tools::tvbgone::Region::EU : tools::tvbgone::Region::NA,
                                [&cancel]() { return cancel.load(); },
                                [&](size_t i, size_t total, const std::string& label) {
                                    status(sfmt("%s %zu/%zu", label.c_str(), i, total));
                                    if (i == 1 || (i % 5) == 0) log(sfmt("%s %zu/%zu", label.c_str(), i, total));
                                },
                                [&](const tools::tvbgone::PulseCode& code, std::string& e) {
                                    const auto r =
                                        platform::send_raw(code.pulses_us.data(), code.pulses_us.size(), code.carrier_hz);
                                    if (!r.ok) {
                                        e = r.error.empty() ? "IR unavailable" : r.error;
                                        return false;
                                    }
                                    return true;
                                },
                                err);
                            if (cancel) log("Stopped");
                            else if (!err.empty()) log(err);
                            else log("All codes sent");
                        });
    }
    std::vector<MenuEntry> e;
    e.push_back(stage_entry(vm, id, "Region NA", "run", "NA"));
    e.push_back(stage_entry(vm, id, "Region EU", "run", "EU"));
    return make_options(vm, assets, "TV-B-Gone", std::move(e));
}

Screen make_tagtinker(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::TagTinker;
    auto plid_from_arg = [](const std::string& arg, uint8_t plid[4]) {
        std::memset(plid, 0, 4);
        if (arg.empty() || arg == "broadcast") return;
        tools::tagtinker::barcode_to_plid(arg.c_str(), plid);
    };
    if (st.name == "ask") {
        return make_prompt(vm, assets, "TagTinker", "17-digit barcode (empty=broadcast)", "",
                           [&vm, id](const std::string& v) {
                               const std::string t = trim(v);
                               nav(vm, id, "menu", t.empty() ? "broadcast" : t);
                           });
    }
    if (st.name == "send") {
        // arg: action|plidkey
        const auto bar = st.arg.find('|');
        const std::string action = bar == std::string::npos ? st.arg : st.arg.substr(0, bar);
        const std::string key = bar == std::string::npos ? "broadcast" : st.arg.substr(bar + 1);
        return make_run(vm, assets, "TagTinker", [action, key, plid_from_arg](Cancel& cancel, const Log& log, const Status&) {
            (void)cancel;
            const auto sender = platform::read_sender_info();
            if (!sender.present) {
                log(sender.error.empty() ? "IR unavailable" : sender.error);
                return;
            }
            uint8_t plid[4]{};
            plid_from_arg(key, plid);
            uint8_t frame[tools::tagtinker::kMaxFrame];
            size_t n = 0;
            if (action == "ledon") {
                uint8_t ping[tools::tagtinker::kMaxFrame];
                const size_t pn = tools::tagtinker::make_ping(ping, sizeof(ping), plid);
                tools::tagtinker::PulseBurst pb;
                if (pn && tools::tagtinker::encode_pp4(ping, pn, pb)) {
                    (void)platform::send_raw(pb.pulses_us.data(), pb.pulses_us.size(), pb.carrier_hz);
                }
                n = tools::tagtinker::make_led(frame, sizeof(frame), plid, 0xC9, 5);
            } else if (action == "ledoff") {
                n = tools::tagtinker::make_led(frame, sizeof(frame), plid, 0x49, 1);
            } else if (action == "ping") {
                n = tools::tagtinker::make_ping(frame, sizeof(frame), plid);
            } else if (action == "refresh") {
                n = tools::tagtinker::make_refresh(frame, sizeof(frame), plid);
            } else if (action == "page") {
                n = tools::tagtinker::make_broadcast_page(frame, sizeof(frame), 0, false, 10);
            } else {
                log("unknown action");
                return;
            }
            if (n == 0) {
                log("frame build failed");
                return;
            }
            tools::tagtinker::PulseBurst pb;
            if (!tools::tagtinker::encode_pp4(frame, n, pb)) {
                log("PP4 encode failed");
                return;
            }
            const auto r = platform::send_raw(pb.pulses_us.data(), pb.pulses_us.size(), pb.carrier_hz);
            log(r.ok ? sfmt("Sent %s (%zu B)", action.c_str(), n) : ("Send failed: " + r.error));
        });
    }
    if (st.name == "menu") {
        const std::string key = st.arg.empty() ? "broadcast" : st.arg;
        std::vector<MenuEntry> e;
        e.push_back(stage_entry(vm, id, "LED ON", "send", "ledon|" + key));
        e.push_back(stage_entry(vm, id, "LED OFF", "send", "ledoff|" + key));
        e.push_back(stage_entry(vm, id, "Ping", "send", "ping|" + key));
        e.push_back(stage_entry(vm, id, "Refresh", "send", "refresh|" + key));
        e.push_back(stage_entry(vm, id, "Page flip (broadcast)", "send", "page|" + key));
        e.push_back(stage_entry(vm, id, "Set barcode...", "ask"));
        return make_options(vm, assets, "TagTinker ESL", std::move(e));
    }
    return make_tagtinker(vm, assets, Stage{"menu", "broadcast"});
}

Screen make_ir_carrier(VM& vm, AssetManager& assets) {
    return make_run(vm, assets, "IR Carrier", [](Cancel& cancel, const Log& log, const Status& status) {
        status("38 kHz up to 8s");
        const auto r = platform::send_carrier(38000, 8000, cancel_ptr(cancel));
        if (cancel) log("Stopped");
        else if (!r.ok) log(r.error.empty() ? "IR unavailable" : r.error);
        else log("Done");
    });
}

std::string default_gateway_hint() {
    std::string err;
    const auto iface = platform::default_route_iface(err);
    return iface.gateway;
}

Screen make_smb_status(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::SmbStatus;
    if (st.name == "run") {
        const std::string host = trim(st.arg);
        return make_run(vm, assets, "SMB Status", [host](Cancel& cancel, const Log& log, const Status&) {
            if (host.empty() || !valid_host(host)) {
                log("bad host");
                return;
            }
            if (cancel) return;
            const auto p139 = platform::tcp_probe(host, 139, 1000, false);
            const auto p445 = platform::tcp_probe(host, 445, 1000, false);
            log("Target " + host);
            log(sfmt("139/tcp %s", p139.open ? "open" : "closed"));
            log(sfmt("445/tcp %s", p445.open ? "open" : "closed"));
        });
    }
    return ask_then(vm, assets, id, "SMB Status", "host IP", default_gateway_hint(), "run");
}

Screen make_sip_options(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::SipOptions;
    if (st.name == "run") {
        const std::string host = trim(st.arg);
        return make_run(vm, assets, "SIP OPTIONS", [host](Cancel& cancel, const Log& log, const Status&) {
            if (host.empty() || !valid_host(host)) {
                log("bad host");
                return;
            }
            const auto r = platform::sip_options(host, 2000, cancel_ptr(cancel));
            if (!r.ok) log(r.error.empty() ? "SIP failed" : r.error);
            else log(r.status_line.empty() ? "no reply" : r.status_line);
        });
    }
    return ask_then(vm, assets, id, "SIP OPTIONS", "host IP", default_gateway_hint(), "run");
}

Screen make_ldap_read(VM& vm, AssetManager& assets, const Stage& st) {
    const ToolId id = ToolId::LdapRead;
    // Stages: ask_host -> ask_base -> ask_bind -> ask_pw -> run with host|base|bind|pw
    if (st.name == "ask_base") {
        return make_prompt(vm, assets, "LDAP base DN", "base DN", "dc=local,dc=lan",
                           [&vm, id, host = st.arg](const std::string& v) {
                               nav(vm, id, "ask_bind", host + "|" + trim(v));
                           });
    }
    if (st.name == "ask_bind") {
        return make_prompt(vm, assets, "LDAP bind DN", "empty = anonymous", "",
                           [&vm, id, prev = st.arg](const std::string& v) {
                               const std::string bind = trim(v);
                               if (bind.empty()) nav(vm, id, "run", prev + "||");
                               else nav(vm, id, "ask_pw", prev + "|" + bind);
                           });
    }
    if (st.name == "ask_pw") {
        return make_prompt(vm, assets, "LDAP password", "not saved", "",
                           [&vm, id, prev = st.arg](const std::string& v) {
                               nav(vm, id, "run", prev + "|" + v);
                           });
    }
    if (st.name == "run") {
        // host|base|bind|pw  (pw may contain '|' — take remainder after 3rd '|')
        std::string host, base, bind, pw;
        size_t p0 = st.arg.find('|');
        if (p0 == std::string::npos) host = st.arg;
        else {
            host = st.arg.substr(0, p0);
            size_t p1 = st.arg.find('|', p0 + 1);
            if (p1 == std::string::npos) base = st.arg.substr(p0 + 1);
            else {
                base = st.arg.substr(p0 + 1, p1 - p0 - 1);
                size_t p2 = st.arg.find('|', p1 + 1);
                if (p2 == std::string::npos) bind = st.arg.substr(p1 + 1);
                else {
                    bind = st.arg.substr(p1 + 1, p2 - p1 - 1);
                    pw = st.arg.substr(p2 + 1);
                }
            }
        }
        return make_run(vm, assets, "LDAP Read", [host, base, bind, pw](Cancel& cancel, const Log& log, const Status&) {
            if (host.empty() || !valid_host(host)) {
                log("bad host");
                return;
            }
            const auto r = platform::ldap_read(host, base, bind, pw, 3000, cancel_ptr(cancel));
            if (!r.ok) {
                log(r.error.empty() ? "LDAP failed" : r.error);
                return;
            }
            for (const auto& line : r.lines) log(line);
        });
    }
    return ask_then(vm, assets, id, "LDAP host", "host IP", default_gateway_hint(), "ask_base");
}

screen::InfoScreen::WorkerFn ble_status_info_worker(const char* name_sub, const char* svc_sub) {
    return [name_sub, svc_sub](Cancel& cancel, screen::InfoScreen::Fields& fields, std::string& status,
                               view::StatusPillKind& kind) {
        const auto r = platform::ble_status_probe(name_sub, svc_sub, 5, cancel_ptr(cancel));
        if (!r.ok) {
            fields.push_back({"error", r.error.empty() ? "BlueZ unavailable" : r.error});
            status = "error";
            kind = view::StatusPillKind::Error;
            return;
        }
        if (r.lines.empty()) {
            fields.push_back({"result", "no devices"});
            status = "missing";
            kind = view::StatusPillKind::Missing;
            return;
        }
        int i = 0;
        for (const auto& line : r.lines) {
            fields.push_back({i == 0 ? "device" : ("#" + std::to_string(i)), line});
            ++i;
        }
        status = "ok";
        kind = view::StatusPillKind::Ok;
    };
}

Screen make_modules_accessory_status(VM& vm, AssetManager& assets, const char* title, uint8_t addr) {
    std::string err;
    const auto hits = platform::scan_all_i2c(err);
    bool present = false;
    for (const auto& h : hits) {
        if (h.address == addr) {
            present = true;
            break;
        }
    }
    std::vector<std::string> lines;
    if (!err.empty()) lines.push_back("I2C: " + err);
    lines.push_back(present ? sfmt("0x%02X present", addr) : sfmt("0x%02X not found (accessory)", addr));
    return make_results(vm, assets, title, std::move(lines));
}

// ---------------------------------------------------------------------------------------------
// ToolId::None helpers (info / ethernet link / GPS folder / Discovery filter)
// ---------------------------------------------------------------------------------------------

Screen make_none(VM& vm, AssetManager& assets, const Stage& st) {
    if (st.name == "ethlink") {
        screen::InfoScreen::Fields fields;
        for (const auto& line : ethernet_link_lines()) {
            const auto sp = line.find("  ");
            if (sp != std::string::npos) fields.push_back({line.substr(0, sp), line.substr(sp + 2)});
            else fields.push_back({"link", line});
        }
        const bool ok = !fields.empty() && fields[0].value.find("No ethernet") == std::string::npos;
        return make_info_card(vm, assets, "Link status", std::move(fields), ok ? "ok" : "none",
                              ok ? view::StatusPillKind::Ok : view::StatusPillKind::Missing);
    }
    if (st.name == "gpsfolder") {
        return make_prompt(vm, assets, "Captures folder", "directory containing .csv captures", captures_dir(),
                           [&vm](const std::string& v) {
                               const std::string p = trim(v);
                               if (!p.empty()) write_text_file(tools::config_file("captures_path.txt"), p + "\n");
                               vm.go_back();
                           });
    }
    if (st.name == "joystatus") return make_modules_accessory_status(vm, assets, "Unit Joystick", 0x63);
    if (st.name == "pahubstatus") return make_modules_accessory_status(vm, assets, "PaHub", 0x70);
    if (st.name == "filter") {
        return make_prompt(vm, assets, "Filter tools", "substring (empty = all)", vm.discovery_filter(),
                           [&vm](const std::string& v) {
                               vm.set_discovery_filter(trim(v));
                               vm.open_discovery();
                           });
    }
    if (st.name == "info") return make_info(vm, assets, "Info", st.arg.empty() ? "(nothing)" : st.arg);
    return make_info(vm, assets, "Tool", "No tool selected");
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

std::unique_ptr<screen::KvxScreen> make_channel_tool_menu(viewmodel::BaseViewModel& vm, AssetManager& assets) {
    const auto& ch = vm.current_channel();
    const std::string cid = ch.id ? ch.id : "";
    const auto tools = model::tools_for_channel(ch.id);
    std::vector<MenuEntry> e;

    auto add_tool = [&](const model::ToolDesc* d) {
        const ToolId tid = d->id;
        e.push_back({d->label, [&vm, tid]() { vm.open_tool(tid); }});
    };
    auto add_by_id = [&](ToolId tid) {
        for (const auto* d : tools) {
            if (d->id == tid) {
                add_tool(d);
                return;
            }
        }
    };

    if (cid == "ethernet") {
        e.push_back({"Link status", [&vm]() { nav(vm, ToolId::None, "ethlink"); }});
        for (const auto* d : tools) add_tool(d);
    } else if (cid == "gps") {
        add_by_id(ToolId::GpsWardrivingMenu);
        add_by_id(ToolId::GpsSiteSurvey);
        add_by_id(ToolId::GpsTracker);
        add_by_id(ToolId::GpsConfigMenu);
    } else if (cid == "modules") {
        add_by_id(ToolId::CapProbe);
        add_by_id(ToolId::BusScan);
        add_by_id(ToolId::UnitScrollStatus);
        add_by_id(ToolId::UartShell);
        add_by_id(ToolId::PahubScan);
        add_by_id(ToolId::IButtonRead);
        add_by_id(ToolId::Pn532KillerUart);
        add_by_id(ToolId::UartLlm);
        e.push_back({"Unit Joystick", [&vm]() { nav(vm, ToolId::None, "joystatus"); }});
        e.push_back({"PaHub presence", [&vm]() { nav(vm, ToolId::None, "pahubstatus"); }});
    } else if (cid == "scripts") {
        add_by_id(ToolId::ScriptsRunner);
        add_by_id(ToolId::PcConnectScripts);
        add_by_id(ToolId::PcConnectHowTo);
    } else if (cid == "pcconnect") {
        add_by_id(ToolId::PcConnectHowTo);
        add_by_id(ToolId::PcConnectNeeds);
        add_by_id(ToolId::PcConnectScripts);
        add_by_id(ToolId::PcConnectExport);
    } else if (cid == "nfc") {
        // Quick actions first, then ADV-style nested menus.
        add_by_id(ToolId::NfcStatus);
        add_by_id(ToolId::NfcRead);
        add_by_id(ToolId::NfcEmulateNdef);
        add_by_id(ToolId::NfcWriteNdef);
        add_by_id(ToolId::NfcScan);
        add_by_id(ToolId::NfcLoad);
        add_by_id(ToolId::NfcErase);
        add_by_id(ToolId::NfcClassicMenu);
        add_by_id(ToolId::NfcTagsMenu);
        add_by_id(ToolId::NfcNdefMenu);
        add_by_id(ToolId::NfcLabMenu);
        add_by_id(ToolId::NfcParsersMenu);
        add_by_id(ToolId::NfcCompanionsMenu);
        add_by_id(ToolId::NfcConfigMenu);
    } else {
        for (const auto* d : tools) add_tool(d);
    }
    return make_options(vm, assets, ch.label ? ch.label : "Tools", std::move(e));
}

std::unique_ptr<screen::KvxScreen> make_discovery_screen(viewmodel::BaseViewModel& vm, AssetManager& assets) {
    const std::string filter = vm.discovery_filter();
    std::vector<MenuEntry> e;
    e.push_back({filter.empty() ? std::string("Filter...") : "Filter: " + filter, [&vm]() { nav(vm, ToolId::None, "filter"); }});
    for (const auto* d : model::all_unique_tools()) {
        if (!filter.empty() && !contains_ci(d->label, filter) && !contains_ci(d->key, filter)) continue;
        const ToolId tid = d->id;
        e.push_back({d->label, [&vm, tid]() { vm.open_tool(tid); }});
    }
    if (e.size() == 1) e.push_back({"(no matches)", [&vm]() { vm.go_back(); }});
    return make_options(vm, assets, "Discovery", std::move(e));
}

std::unique_ptr<screen::KvxScreen> make_tool_run_screen(viewmodel::BaseViewModel& vm, AssetManager& assets) {
    const ToolId id = vm.active_tool_id();
    const std::string param = vm.tool_param();
    const Stage st = parse_stage(param);

    switch (id) {
    // --- Net -----------------------------------------------------------------------------
    case ToolId::NetToolkit: return make_net_toolkit(vm, assets, st);
    case ToolId::ScanHosts: return make_run(vm, assets, "Scan Hosts", scan_hosts_worker(param));
    case ToolId::ListenTcp:
    case ToolId::ClientTcp:
    case ToolId::Telnet: return make_tcp_tool(vm, assets, id, st);
    case ToolId::Socks4: return make_run(vm, assets, "SOCKS4 Proxy", socks4_worker(param));
    case ToolId::LanWatch: return make_run(vm, assets, "LAN Watch", lan_watch_worker());
    case ToolId::PineapHunter: return make_run(vm, assets, "PineAP Hunter", pineap_worker());
    case ToolId::OpenNetworks: return make_open_networks(vm, assets, st);
    case ToolId::ApDetail: return make_ap_detail(vm, assets, st);
    case ToolId::FileShare:
        return make_run(vm, assets, "File Share", http_share_worker(home_dir(), false, "File Share"));
    case ToolId::DeadDrop:
        return make_run(vm, assets, "WiFi Dead Drop", http_share_worker(ensure_subdir("dead-drop"), true, "Dead Drop"));
    case ToolId::Roku: return make_roku(vm, assets, st);
    case ToolId::WireGuard: return make_wireguard(vm, assets, st);
    case ToolId::Printer: return make_printer(vm, assets, st);
    case ToolId::UpnpList: return make_run(vm, assets, "UPnP list", upnp_worker());
    case ToolId::HoneyPot: return make_run(vm, assets, "HoneyPot", honeypot_worker());
    case ToolId::SshHint: return make_info(vm, assets, "SSH", "Use the launcher SSH client");
    case ToolId::LootConsole: return make_loot_console(vm, assets, st);
    case ToolId::CheckHandshakes: return make_check_handshakes(vm, assets, st);
    case ToolId::NtlmClean: return make_ntlm_clean(vm, assets, st);
    case ToolId::SmbStatus: return make_smb_status(vm, assets, st);
    case ToolId::SipOptions: return make_sip_options(vm, assets, st);
    case ToolId::LdapRead: return make_ldap_read(vm, assets, st);

    // --- BLE -----------------------------------------------------------------------------
    case ToolId::BleScan: return make_run(vm, assets, "BLE Scan", ble_worker(BleMode::Scan, ""));
    case ToolId::BleHunter: return make_run(vm, assets, "BLE Hunter", ble_worker(BleMode::Hunter, param));
    case ToolId::BleSniffer: return make_run(vm, assets, "BLE Sniffer", ble_worker(BleMode::Sniffer, ""));
    case ToolId::WallOfFlipper: return make_run(vm, assets, "Wall Of Flipper", ble_worker(BleMode::Flipper, ""));
    case ToolId::WallOfAirtag: return make_run(vm, assets, "Wall Of Airtag", ble_worker(BleMode::Airtag, ""));
    case ToolId::SkimmerDetector: return make_run(vm, assets, "Skimmer Detector", ble_worker(BleMode::Skimmer, ""));
    case ToolId::DroneId: return make_run(vm, assets, "Drone ID (BLE)", ble_worker(BleMode::DroneId, ""));
    case ToolId::BleDisconnect: return make_ble_disconnect(vm, assets);
    case ToolId::NinebotStatus:
        return make_info_card(vm, assets, "Ninebot Status",
                              ble_status_info_worker("ninebot", "6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
    case ToolId::AmiiboStatus:
        return make_info_card(vm, assets, "Amiibolink Status", ble_status_info_worker("amiibo", ""));
    case ToolId::Pn532Status:
        return make_info_card(vm, assets, "PN532-BLE Status", ble_status_info_worker("pn532", ""));
    case ToolId::ChameleonStatus:
        return make_info_card(vm, assets, "Chameleon Status", ble_status_info_worker("chameleon", ""));

    // --- Tools leftovers -----------------------------------------------------------------
    case ToolId::Dtmf: return make_dtmf(vm, assets, st);
    case ToolId::Morse: return make_morse(vm, assets, st);
    case ToolId::Microphone: return make_microphone(vm, assets, st);
    case ToolId::Mqtt: return make_mqtt(vm, assets, st);
    case ToolId::LlmChat: return make_llm(vm, assets, st);

    // --- Everyday apps -------------------------------------------------------------------
    case ToolId::Calculator:
        return make_info(vm, assets, "Calculator", "Open Tools → Calculator");
    case ToolId::MediaPlayer: return make_media_player(vm, assets, st);
    case ToolId::IButtonHint: return make_info(vm, assets, "iButton", "Needs a 1-Wire accessory.");
    case ToolId::FilesBrowser: return make_files_browser(vm, assets, st);
    case ToolId::FilesShareHint:
        return make_run(vm, assets, "File Share", http_share_worker(home_dir(), false, "File Share"));
    case ToolId::IrUse: return make_ir_use(vm, assets);
    case ToolId::IrLearn: return make_ir_learn(vm, assets, st);
    case ToolId::IrBrowse: return make_ir_browse(vm, assets, st);
    case ToolId::IrRead: return make_run(vm, assets, "IR Read", ir_read_worker());
    case ToolId::IrFavorites: return make_ir_favorites(vm, assets);
    case ToolId::TvBGone: return make_tv_b_gone(vm, assets, st);
    case ToolId::TagTinker: return make_tagtinker(vm, assets, st);
    case ToolId::IrCarrier: return make_ir_carrier(vm, assets);
    case ToolId::WifiConnect: return make_wifi_connect(vm, assets, st);
    case ToolId::WifiDisconnect: return make_wifi_disconnect(vm, assets, st);
    case ToolId::WifiApInfo: return make_wifi_ap_info(vm, assets);

    // --- Scripts / PC Connect / GPS / Modules / accessory --------------------------------
    case ToolId::ScriptsRunner: return make_scripts(vm, assets, st);
    case ToolId::PcConnectHowTo: return make_pcconnect_howto(vm, assets);
    case ToolId::PcConnectNeeds: return make_pcconnect_needs(vm, assets);
    case ToolId::PcConnectScripts: return make_pcconnect_scripts(vm, assets, st);
    case ToolId::PcConnectExport: return make_pcconnect_export(vm, assets);
    case ToolId::GpsCaptures:
    case ToolId::GpsLive:
    case ToolId::GpsWardrivingMenu:
    case ToolId::GpsScanWifi:
    case ToolId::GpsScanBle:
    case ToolId::GpsScanBoth:
    case ToolId::GpsWdConfig:
    case ToolId::GpsWdMaster:
    case ToolId::GpsSiteSurvey:
    case ToolId::GpsTracker:
    case ToolId::GpsConfigMenu:
    case ToolId::GpsStatus:
    case ToolId::GpsSource:
    case ToolId::GpsHardware:
    case ToolId::GpsNmeaPath:
    case ToolId::GpsBaud:
    case ToolId::GpsFolder:
        if (Screen s = make_gps_tool_screen(vm, assets, id, param)) return s;
        break;
    case ToolId::UartShell: return make_uart_shell(vm, assets, st);
    case ToolId::BusScan: return make_bus_scan(vm, assets);
    case ToolId::UnitScrollStatus: return make_unit_scroll(vm, assets);
    case ToolId::None:
    case ToolId::Count: break;
    default: {
        if (auto cap = make_cap_tool_screen(vm, assets, id, param)) return cap;
        break;
    }
    }
    if (auto cap = make_cap_tool_screen(vm, assets, id, param)) return cap;
    return make_none(vm, assets, st);
}

} // namespace app

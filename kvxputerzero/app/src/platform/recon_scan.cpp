#include "recon_scan.h"

#include "http_get.h"

#include <arpa/inet.h>

#include <cstdio>
#include <fstream>

namespace platform {

namespace {

std::string trim_copy(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string normalize_base(std::string base) {
    base = trim_copy(base);
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) base = "http://" + base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

std::string normalize_path(std::string p) {
    p = trim_copy(p);
    if (p.empty()) return {};
    if (p.front() != '/') p = "/" + p;
    return p;
}

} // namespace

const std::vector<std::string>& default_crawler_paths() {
    static const std::vector<std::string> list = {
        "/",      "/robots.txt",  "/sitemap.xml", "/admin",   "/login",    "/index.html",
        "/api",   "/.git/HEAD",   "/.env",        "/backup",  "/config",   "/server-status",
        "/wp-login.php", "/phpmyadmin", "/status",  "/health", "/favicon.ico",
    };
    return list;
}

const std::vector<std::string>& default_cctv_paths() {
    static const std::vector<std::string> list = {
        "/", "/login", "/web/", "/cgi-bin/", "/ISAPI/System/deviceInfo", "/onvif/device_service",
        "/doc/page/login.asp", "/view/viewer_index.shtml",
    };
    return list;
}

std::vector<std::string> read_path_list(const std::string& file) {
    std::vector<std::string> out;
    std::ifstream f(file);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line);
    }
    return out;
}

ReconStats web_crawl(const std::string& base_in, const std::vector<std::string>& paths, const ReconEmit& emit,
                     const ReconCancel& cancel) {
    ReconStats st;
    const std::string base = normalize_base(base_in);
    if (base == "http://" || base.size() <= 7) {
        if (emit) emit("usage: base host or URL");
        return st;
    }
    if (emit) emit("base " + base);
    for (const auto& raw : paths) {
        if (cancel && cancel()) break;
        const std::string path = normalize_path(raw);
        if (path.empty()) continue;
        ++st.tried;
        const auto r = http_request("GET", base + path, {}, 4, 256);
        if (r.status >= 200 && r.status < 400) {
            ++st.hits;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d ", r.status);
            if (emit) emit(std::string(buf) + path);
        }
    }
    return st;
}

ReconStats http_sweep(const std::string& seed_ip, const std::vector<std::string>& cctv_paths,
                      const ReconEmit& emit, const ReconCancel& cancel) {
    ReconStats st;
    in_addr ia{};
    const std::string seed = trim_copy(seed_ip);
    if (inet_pton(AF_INET, seed.c_str(), &ia) != 1) {
        if (emit) emit("need an IPv4 address to seed the sweep");
        return st;
    }
    const uint32_t host = ntohl(ia.s_addr);
    const uint32_t net = host & 0xFFFFFF00u; // /24
    for (uint32_t h = 1; h <= 254; ++h) {
        if (cancel && cancel()) break;
        in_addr t{};
        t.s_addr = htonl(net + h);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &t, ip, sizeof(ip));
        ++st.tried;
        const auto r = http_request("GET", std::string("http://") + ip + "/", {}, 1, 128);
        if (r.status <= 0 || r.status >= 500) continue;
        ++st.hits;
        // Probe the camera-ish paths to flag likely DVR/NVR/IP-camera web UIs.
        bool camera = false;
        for (const auto& p : cctv_paths) {
            if (cancel && cancel()) break;
            const std::string path = normalize_path(p);
            if (path.empty() || path == "/") continue;
            const auto pr = http_request("GET", std::string("http://") + ip + path, {}, 1, 64);
            if (pr.status > 0 && pr.status < 400) {
                camera = true;
                break;
            }
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  HTTP %d%s", ip, r.status, camera ? "  [camera?]" : "");
        if (emit) emit(buf);
    }
    return st;
}

} // namespace platform

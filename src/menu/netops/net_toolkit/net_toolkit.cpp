#ifndef LITE_VERSION
#include "net_toolkit.h"

#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/app/utils.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <esp_netif.h>
#include <globals.h>
#include <lwip/dns.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>
#include <vector>

namespace {

bool ensureSta() {
    if (WiFi.isConnected()) return true;
    return wifiConnectMenu(WIFI_STA);
}

FS *toolkitFs() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return nullptr;
    return fs;
}

void ensureToolkitDirs(FS &fs) {
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_TOOLKIT);
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_TOOLKIT_SCANS);
}

std::vector<uint16_t> builtinTop20() {
    return {21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443, 445, 993, 995, 1723, 3306, 3389, 5900, 8080};
}

std::vector<uint16_t> loadPortsFromFile(FS &fs, const char *path, size_t maxPorts) {
    std::vector<uint16_t> ports;
    if (!fs.exists(path)) return ports;
    File f = fs.open(path, FILE_READ);
    if (!f) return ports;
    while (f.available() && ports.size() < maxPorts) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int p = line.toInt();
        if (p > 0 && p <= 65535) ports.push_back((uint16_t)p);
    }
    f.close();
    return ports;
}

String sanitizeBanner(const String &raw) {
    String out;
    out.reserve(raw.length());
    for (size_t i = 0; i < raw.length() && out.length() < 120; i++) {
        char c = raw[i];
        if (c == '\r' || c == '\n') {
            if (out.length() && out[out.length() - 1] != ' ') out += ' ';
            continue;
        }
        if (c >= 32 && c < 127) out += c;
    }
    out.trim();
    return out;
}

String grabBanner(const String &host, uint16_t port, uint32_t timeoutMs = 800) {
    WiFiClient client;
    client.setTimeout(timeoutMs / 1000);
    if (!client.connect(host.c_str(), port, timeoutMs)) return "";
    if (port == 80 || port == 8080 || port == 8000 || port == 8888) {
        client.print("HEAD / HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
    } else if (port == 443) {
        // TLS not probed here; close after connect
        client.stop();
        return "tcp-open (tls)";
    }
    delay(50);
    String raw;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && raw.length() < 256) {
        while (client.available() && raw.length() < 256) raw += (char)client.read();
        if (!client.connected() && !client.available()) break;
        delay(5);
    }
    client.stop();
    return sanitizeBanner(raw);
}

bool tcpProbe(const String &host, uint16_t port, uint32_t timeoutMs, String *bannerOut) {
    WiFiClient client;
    uint32_t t0 = millis();
    bool ok = client.connect(host.c_str(), port, timeoutMs);
    uint32_t ms = millis() - t0;
    if (!ok) {
        if (bannerOut) *bannerOut = "";
        return false;
    }
    client.stop();
    if (bannerOut) {
        *bannerOut = grabBanner(host, port, 600);
        if (bannerOut->isEmpty()) *bannerOut = "open " + String(ms) + "ms";
    }
    return true;
}

String askTargetHost() {
    String def = WiFi.gatewayIP().toString();
    String host = keyboard(def, 64, "Host / IP");
    host.trim();
    if (host.length() == 0 || host == "\x1B") return "";
    return host;
}

void showLines(const String &title, const std::vector<String> &lines) {
    ScrollableTextArea area(title);
    for (const auto &l : lines) area.addLine(l);
    if (lines.empty()) area.addLine("(no results)");
    area.show(true);
}

void portScanMenu() {
    if (!ensureSta()) return;
    String host = askTargetHost();
    if (host.isEmpty()) return;

    FS *fs = toolkitFs();
    std::vector<uint16_t> ports;
    options = {
        {"Top 20",
         [&]() {
             if (fs) ports = loadPortsFromFile(*fs, kvx::paths::NETOPS_TOOLKIT_TOP20, 64);
             if (ports.empty()) ports = builtinTop20();
         }},
        {"Top 100",
         [&]() {
             if (fs) ports = loadPortsFromFile(*fs, kvx::paths::NETOPS_TOOLKIT_TOP100, 128);
             if (ports.empty()) {
                 ports = builtinTop20();
                 for (uint16_t p : {81, 88, 113, 119, 123, 161, 389, 465, 514, 587, 631, 636, 873,
                                    1080, 1433, 1521, 2049, 2121, 2222, 2375, 3000, 3128, 5000, 5432,
                                    5672, 5901, 6379, 8008, 8081, 8443, 8888, 9000, 9090, 9200, 11211,
                                    27017})
                     ports.push_back(p);
             }
         }},
        {"Custom list (SD)",
         [&]() {
             if (!fs) {
                 displayError("No storage", true);
                 return;
             }
             String path = loopSD(*fs, true, "TXT", kvx::paths::NETOPS_TOOLKIT);
             if (path.length()) ports = loadPortsFromFile(*fs, path.c_str(), 256);
         }},
        {"Single port",
         [&]() {
             String s = num_keyboard("80", 5, "Port");
             int p = s.toInt();
             if (p > 0 && p <= 65535) ports.push_back((uint16_t)p);
         }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Port preset");
    options.clear();
    if (ports.empty()) {
        displayInfo("No ports selected", true);
        return;
    }

    drawMainBorderWithTitle("Port Scan");
    tft.setTextSize(FP);
    padprintln(host);
    padprintln("ESC abort | open ports:");
    std::vector<String> results;
    results.push_back("ip,port,state,banner,ms");
    EscPress = false;
    int openCount = 0;
    for (size_t i = 0; i < ports.size() && !check(EscPress) && !returnToMenu; i++) {
        uint16_t port = ports[i];
        tft.fillRect(10, uiStatusY(1), tftWidth - 20, LH * FP + 2, kvxConfig.bgColor);
        tft.drawString(String(i + 1) + "/" + String(ports.size()) + " :" + String(port), 10, uiStatusY(1));
        String banner;
        uint32_t t0 = millis();
        bool open = tcpProbe(host, port, 400, &banner);
        uint32_t ms = millis() - t0;
        if (open) {
            openCount++;
            String line = host + "," + String(port) + ",open,\"" + banner + "\"," + String(ms);
            results.push_back(line);
            padprintln(String(port) + " open " + banner.substring(0, 28));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (fs) {
        ensureToolkitDirs(*fs);
        String fname = String(kvx::paths::NETOPS_TOOLKIT_SCANS) + "/scan_" + String((uint32_t)millis()) + ".csv";
        File out = fs->open(fname, FILE_WRITE);
        if (out) {
            for (const auto &r : results) out.println(r);
            out.close();
            displayInfo("Saved " + String(openCount) + " open -> scans/", true);
        }
    } else {
        displayInfo(String(openCount) + " open ports", true);
    }
    showLines("Scan results", results);
}

void bannerGrabMenu() {
    if (!ensureSta()) return;
    String host = askTargetHost();
    if (host.isEmpty()) return;
    String portStr = num_keyboard("80", 5, "Port");
    int port = portStr.toInt();
    if (port <= 0 || port > 65535) {
        displayError("Bad port", true);
        return;
    }
    drawMainBorderWithTitle("Banner Grab");
    padprintln(host + ":" + String(port));
    String banner = grabBanner(host, (uint16_t)port, 1200);
    if (banner.isEmpty()) {
        // still try connect-only
        WiFiClient c;
        if (c.connect(host.c_str(), port, 800)) {
            banner = "(open, no banner)";
            c.stop();
        } else {
            displayError("Closed / filtered", true);
            return;
        }
    }
    showLines("Banner", {host + ":" + String(port), banner});
}

struct PingStats {
    volatile int ok = 0;
    volatile int fail = 0;
    volatile uint32_t lastRtt = 0;
    volatile bool done = false;
};

static void onPingOk(esp_ping_handle_t hdl, void *args) {
    auto *st = (PingStats *)args;
    uint32_t ttl = 0, time_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &time_ms, sizeof(time_ms));
    st->ok++;
    st->lastRtt = time_ms;
}

static void onPingTimeout(esp_ping_handle_t hdl, void *args) {
    auto *st = (PingStats *)args;
    st->fail++;
}

static void onPingEnd(esp_ping_handle_t hdl, void *args) {
    auto *st = (PingStats *)args;
    st->done = true;
    esp_ping_delete_session(hdl);
}

void pingMenu() {
    if (!ensureSta()) return;
    String host = askTargetHost();
    if (host.isEmpty()) return;

    IPAddress ip;
    if (!WiFi.hostByName(host.c_str(), ip)) {
        displayError("DNS failed", true);
        return;
    }

    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = (uint32_t)ip;

    PingStats stats;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = 4;
    cfg.timeout_ms = 1000;
    cfg.interval_ms = 500;

    esp_ping_callbacks_t cbs = {
        .cb_args = &stats,
        .on_ping_success = onPingOk,
        .on_ping_timeout = onPingTimeout,
        .on_ping_end = onPingEnd,
    };
    esp_ping_handle_t hdl = nullptr;
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &hdl);
    if (err != ESP_OK) {
        displayError("ICMP unavailable", true);
        return;
    }

    drawMainBorderWithTitle("Ping");
    padprintln(host + " (" + ip.toString() + ")");
    esp_ping_start(hdl);
    EscPress = false;
    uint32_t paintedOk = UINT32_MAX, paintedFail = UINT32_MAX, paintedRtt = UINT32_MAX;
    while (!stats.done && !check(EscPress) && !returnToMenu) {
        if (stats.ok != paintedOk || stats.fail != paintedFail || stats.lastRtt != paintedRtt) {
            tft.fillRect(10, uiStatusY(0), tftWidth - 20, LH * FP * 2 + 4, kvxConfig.bgColor);
            tft.drawString(
                "ok=" + String(stats.ok) + " fail=" + String(stats.fail) + " rtt=" + String(stats.lastRtt) +
                    "ms",
                10,
                uiStatusY(0)
            );
            paintedOk = stats.ok;
            paintedFail = stats.fail;
            paintedRtt = stats.lastRtt;
        }
        delay(100);
    }
    if (!stats.done) {
        esp_ping_stop(hdl);
        esp_ping_delete_session(hdl);
    }
    displayInfo(
        "ok " + String(stats.ok) + "/" + String(stats.ok + stats.fail) + " last " + String(stats.lastRtt) + "ms",
        true
    );
}

// Minimal DNS query (A/AAAA/CNAME/TXT/MX)
bool dnsQuery(
    const IPAddress &resolver, const String &name, uint16_t qtype, std::vector<String> &answers, String &err
) {
    answers.clear();
    WiFiUDP udp;
    if (!udp.begin(0)) {
        err = "UDP fail";
        return false;
    }

    uint8_t pkt[512];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x12;
    pkt[1] = 0x34; // id
    pkt[2] = 0x01;
    pkt[3] = 0x00; // recursion desired
    pkt[4] = 0x00;
    pkt[5] = 0x01; // QDCOUNT=1

    size_t pos = 12;
    int start = 0;
    String n = name;
    if (n.endsWith(".")) n.remove(n.length() - 1);
    while (start <= (int)n.length()) {
        int dot = n.indexOf('.', start);
        if (dot < 0) dot = n.length();
        int labLen = dot - start;
        if (labLen <= 0 || labLen > 63 || pos + 1 + labLen >= sizeof(pkt) - 5) {
            err = "bad name";
            udp.stop();
            return false;
        }
        pkt[pos++] = (uint8_t)labLen;
        for (int i = 0; i < labLen; i++) pkt[pos++] = (uint8_t)n[start + i];
        start = dot + 1;
        if (dot >= (int)n.length()) break;
    }
    pkt[pos++] = 0;
    pkt[pos++] = (qtype >> 8) & 0xff;
    pkt[pos++] = qtype & 0xff;
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x01; // IN

    if (!udp.beginPacket(resolver, 53)) {
        err = "send fail";
        udp.stop();
        return false;
    }
    udp.write(pkt, pos);
    udp.endPacket();

    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
        int len = udp.parsePacket();
        if (len > 0) {
            int nread = udp.read(pkt, sizeof(pkt));
            udp.stop();
            if (nread < 12) {
                err = "short reply";
                return false;
            }
            uint16_t ancount = (pkt[6] << 8) | pkt[7];
            size_t i = 12;
            // skip question
            while (i < (size_t)nread && pkt[i] != 0) {
                i += pkt[i] + 1;
            }
            i += 5; // 0 + type + class
            for (uint16_t a = 0; a < ancount && i + 12 <= (size_t)nread; a++) {
                if ((pkt[i] & 0xC0) == 0xC0) i += 2;
                else {
                    while (i < (size_t)nread && pkt[i] != 0) i += pkt[i] + 1;
                    i++;
                }
                if (i + 10 > (size_t)nread) break;
                uint16_t type = (pkt[i] << 8) | pkt[i + 1];
                uint16_t rdlen = (pkt[i + 8] << 8) | pkt[i + 9];
                i += 10;
                if (i + rdlen > (size_t)nread) break;
                if (type == 1 && rdlen == 4) {
                    answers.push_back(
                        String(pkt[i]) + "." + String(pkt[i + 1]) + "." + String(pkt[i + 2]) + "." +
                        String(pkt[i + 3])
                    );
                } else if (type == 28 && rdlen == 16) {
                    char buf[48];
                    snprintf(
                        buf,
                        sizeof(buf),
                        "%02x%02x:%02x%02x:%02x%02x:%02x%02x:...",
                        pkt[i],
                        pkt[i + 1],
                        pkt[i + 2],
                        pkt[i + 3],
                        pkt[i + 4],
                        pkt[i + 5],
                        pkt[i + 6],
                        pkt[i + 7]
                    );
                    answers.push_back(String(buf));
                } else if (type == 16) { // TXT
                    String txt;
                    size_t j = i;
                    while (j < i + rdlen) {
                        uint8_t l = pkt[j++];
                        for (uint8_t k = 0; k < l && j < i + rdlen; k++) txt += (char)pkt[j++];
                    }
                    answers.push_back("TXT " + txt);
                } else if (type == 15 && rdlen >= 3) { // MX
                    uint16_t pref = (pkt[i] << 8) | pkt[i + 1];
                    answers.push_back("MX " + String(pref));
                } else if (type == 5) {
                    answers.push_back("CNAME");
                } else {
                    answers.push_back("type=" + String(type) + " len=" + String(rdlen));
                }
                i += rdlen;
            }
            if (answers.empty()) answers.push_back("(no answers)");
            return true;
        }
        delay(10);
    }
    udp.stop();
    err = "timeout";
    return false;
}

void digMenu() {
    if (!ensureSta()) return;
    String name = keyboard("example.com", 64, "Name");
    name.trim();
    if (name.isEmpty() || name == "\x1B") return;

    uint16_t qtype = 1;
    options = {
        {"A",     [&]() { qtype = 1; } },
        {"AAAA",  [&]() { qtype = 28; }},
        {"CNAME", [&]() { qtype = 5; } },
        {"TXT",   [&]() { qtype = 16; }},
        {"MX",    [&]() { qtype = 15; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Query type");
    options.clear();

    IPAddress resolver = WiFi.dnsIP(0);
    if ((uint32_t)resolver == 0) resolver = IPAddress(8, 8, 8, 8);
    options = {
        {"DHCP DNS (" + resolver.toString() + ")", []() {}},
        {"8.8.8.8", [&]() { resolver = IPAddress(8, 8, 8, 8); }},
        {"1.1.1.1", [&]() { resolver = IPAddress(1, 1, 1, 1); }},
        {"Custom",
         [&]() {
             String s = keyboard(resolver.toString(), 15, "DNS IP");
             IPAddress tmp;
             if (tmp.fromString(s)) resolver = tmp;
         }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Resolver");
    options.clear();

    std::vector<String> answers;
    String err;
    drawMainBorderWithTitle("Dig");
    padprintln(name);
    if (!dnsQuery(resolver, name, qtype, answers, err)) {
        displayError(err, true);
        return;
    }
    std::vector<String> lines;
    lines.push_back("@ " + resolver.toString());
    lines.insert(lines.end(), answers.begin(), answers.end());
    showLines("Dig", lines);
}

void curlMenu() {
    if (!ensureSta()) return;
    String url = keyboard("http://example.com", 120, "URL");
    url.trim();
    if (url.isEmpty() || url == "\x1B") return;

    String method = "GET";
    options = {
        {"GET",  [&]() { method = "GET"; } },
        {"HEAD", [&]() { method = "HEAD"; }},
        {"POST", [&]() { method = "POST"; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Method");
    options.clear();

    String body;
    if (method == "POST") {
        FS *fs = toolkitFs();
        if (fs) {
            String path = loopSD(*fs, true, "*", "/");
            if (path.length()) {
                File f = fs->open(path, FILE_READ);
                if (f) {
                    body = f.readString();
                    f.close();
                }
            }
        }
        if (body.isEmpty()) body = keyboard("", 200, "POST body");
    }

    HTTPClient http;
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(url)) {
        displayError("begin failed", true);
        return;
    }

    drawMainBorderWithTitle("Curl-lite");
    padprintln(method + " " + url.substring(0, 36));
    int code = -1;
    String resp;
    if (method == "GET") code = http.GET();
    else if (method == "HEAD") code = http.sendRequest("HEAD");
    else code = http.POST(body);

    std::vector<String> lines;
    lines.push_back("status: " + String(code));
    lines.push_back("size: " + String(http.getSize()));
    if (code > 0) {
        resp = http.getString();
        if (resp.length() > 1500) resp = resp.substring(0, 1500) + "\n...(truncated)";
        int start = 0;
        while (start < (int)resp.length()) {
            int nl = resp.indexOf('\n', start);
            if (nl < 0) nl = resp.length();
            lines.push_back(resp.substring(start, nl));
            start = nl + 1;
            if (lines.size() > 80) break;
        }
    }
    http.end();

    FS *fs = toolkitFs();
    if (fs && code > 0) {
        ensureToolkitDirs(*fs);
        String fname = String(kvx::paths::NETOPS_TOOLKIT_SCANS) + "/curl_" + String((uint32_t)millis()) + ".txt";
        File out = fs->open(fname, FILE_WRITE);
        if (out) {
            out.println("URL: " + url);
            out.println("Status: " + String(code));
            out.println(resp);
            out.close();
            lines.insert(lines.begin() + 1, "saved: " + fname);
        }
    }
    showLines("Curl-lite", lines);
}

} // namespace

void netToolkitHelp() {
    showAppHelp(
        {
            "Everyday STA network tools.",
            "Join WiFi first (prompted).",
            "",
            "Port Scan:",
            "  Pick host + port preset.",
            "  Sequential TCP connect.",
            "  Open ports + banners.",
            "  CSV -> netops/toolkit/scans/",
            "  ESC aborts mid-scan.",
            "",
            "Banner Grab:",
            "  One host:port; short read",
            "  or HTTP HEAD / SSH id.",
            "",
            "Ping:",
            "  ICMP via esp_ping.",
            "  Shows ok/fail + last RTT.",
            "",
            "Dig:",
            "  DNS A/AAAA/CNAME/TXT/MX",
            "  via DHCP DNS or custom.",
            "",
            "Curl-lite:",
            "  HTTP(S) GET/HEAD/POST.",
            "  Preview + save response.",
            "",
            "Not full nmap. Lab use only.",
        }
    );
}

void netToolkitMenu() {
    options = {
        {"Help",        netToolkitHelp},
        {"Port Scan",   portScanMenu  },
        {"Banner Grab", bannerGrabMenu},
        {"Ping",        pingMenu      },
        {"Dig",         digMenu       },
        {"Curl-lite",   curlMenu      },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Net Toolkit");
    options.clear();
}

#endif // LITE_VERSION

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

// Build QNAME into pkt starting at pos; returns new pos or 0 on error.
size_t dnsEncodeName(uint8_t *pkt, size_t pktCap, size_t pos, const String &name, String &err) {
    String n = name;
    if (n.endsWith(".")) n.remove(n.length() - 1);
    int start = 0;
    while (start <= (int)n.length()) {
        int dot = n.indexOf('.', start);
        if (dot < 0) dot = n.length();
        int labLen = dot - start;
        if (labLen <= 0 || labLen > 63 || pos + 1 + labLen >= pktCap - 5) {
            err = "bad name";
            return 0;
        }
        pkt[pos++] = (uint8_t)labLen;
        for (int i = 0; i < labLen; i++) pkt[pos++] = (uint8_t)n[start + i];
        start = dot + 1;
        if (dot >= (int)n.length()) break;
    }
    pkt[pos++] = 0;
    return pos;
}

void dnsParseAnswers(const uint8_t *pkt, int nread, std::vector<String> &answers) {
    if (nread < 12) return;
    uint16_t ancount = (pkt[6] << 8) | pkt[7];
    size_t i = 12;
    while (i < (size_t)nread && pkt[i] != 0) {
        if ((pkt[i] & 0xC0) == 0xC0) {
            i += 2;
            break;
        }
        i += pkt[i] + 1;
    }
    if (i < (size_t)nread && pkt[i] == 0) i++;
    i += 4; // type + class
    for (uint16_t a = 0; a < ancount && i + 10 <= (size_t)nread; a++) {
        if ((pkt[i] & 0xC0) == 0xC0) i += 2;
        else {
            while (i < (size_t)nread && pkt[i] != 0) {
                if ((pkt[i] & 0xC0) == 0xC0) {
                    i += 2;
                    break;
                }
                i += pkt[i] + 1;
            }
            if (i < (size_t)nread && pkt[i] == 0) i++;
        }
        if (i + 10 > (size_t)nread) break;
        uint16_t type = (pkt[i] << 8) | pkt[i + 1];
        uint16_t rdlen = (pkt[i + 8] << 8) | pkt[i + 9];
        i += 10;
        if (i + rdlen > (size_t)nread) break;
        if (type == 1 && rdlen == 4) {
            answers.push_back(
                String(pkt[i]) + "." + String(pkt[i + 1]) + "." + String(pkt[i + 2]) + "." + String(pkt[i + 3])
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
        } else if (type == 16) {
            String txt;
            size_t j = i;
            while (j < i + rdlen) {
                uint8_t l = pkt[j++];
                for (uint8_t k = 0; k < l && j < i + rdlen; k++) txt += (char)pkt[j++];
            }
            answers.push_back("TXT " + txt);
        } else if (type == 15 && rdlen >= 3) {
            uint16_t pref = (pkt[i] << 8) | pkt[i + 1];
            answers.push_back("MX " + String(pref));
        } else if (type == 5) {
            answers.push_back("CNAME");
        } else if (type == 2) {
            answers.push_back("NS");
        } else if (type == 6) {
            answers.push_back("SOA");
        } else {
            answers.push_back("type=" + String(type) + " len=" + String(rdlen));
        }
        i += rdlen;
    }
}

// Minimal DNS query (A/AAAA/CNAME/TXT/MX). flags default 0x0100 = RD.
bool dnsQuery(
    const IPAddress &resolver, const String &name, uint16_t qtype, std::vector<String> &answers, String &err,
    uint16_t flags = 0x0100
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
    pkt[1] = 0x34;
    pkt[2] = (flags >> 8) & 0xff;
    pkt[3] = flags & 0xff;
    pkt[4] = 0x00;
    pkt[5] = 0x01;

    size_t pos = dnsEncodeName(pkt, sizeof(pkt), 12, name, err);
    if (pos == 0) {
        udp.stop();
        return false;
    }
    pkt[pos++] = (qtype >> 8) & 0xff;
    pkt[pos++] = qtype & 0xff;
    pkt[pos++] = 0x00;
    pkt[pos++] = 0x01;

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
            dnsParseAnswers(pkt, nread, answers);
            if (answers.empty()) answers.push_back("(no answers)");
            return true;
        }
        delay(10);
    }
    udp.stop();
    err = "timeout";
    return false;
}

IPAddress pickResolver() {
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
    return resolver;
}

bool saveScanCsv(const String &prefix, const std::vector<String> &rows) {
    FS *fs = toolkitFs();
    if (!fs) return false;
    ensureToolkitDirs(*fs);
    String fname =
        String(kvx::paths::NETOPS_TOOLKIT_SCANS) + "/" + prefix + "_" + String((uint32_t)millis()) + ".csv";
    File out = fs->open(fname, FILE_WRITE);
    if (!out) return false;
    for (const auto &r : rows) out.println(r);
    out.close();
    return true;
}

// Extract printable DNS labels from a message (AXFR name harvest).
void dnsHarvestNames(const uint8_t *pkt, int nread, std::vector<String> &names, size_t maxNames) {
    for (int i = 12; i < nread && names.size() < maxNames;) {
        if ((pkt[i] & 0xC0) == 0xC0) {
            i += 2;
            continue;
        }
        if (pkt[i] == 0) {
            i++;
            continue;
        }
        if (pkt[i] > 63) {
            i++;
            continue;
        }
        String name;
        int j = i;
        int hops = 0;
        while (j < nread && hops++ < 32) {
            if ((pkt[j] & 0xC0) == 0xC0) {
                if (j + 1 >= nread) break;
                j = ((pkt[j] & 0x3F) << 8) | pkt[j + 1];
                continue;
            }
            uint8_t lab = pkt[j++];
            if (lab == 0) break;
            if (j + lab > nread) break;
            if (name.length()) name += '.';
            for (uint8_t k = 0; k < lab; k++) {
                char c = (char)pkt[j++];
                if (c >= 32 && c < 127) name += c;
            }
        }
        if (name.length() >= 2 && name.indexOf('.') > 0) {
            bool dup = false;
            for (const auto &n : names) {
                if (n == name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) names.push_back(name);
        }
        i++;
    }
}

void axfrMenu() {
    if (!ensureSta()) return;
    String zone = keyboard("example.com", 64, "Zone");
    zone.trim();
    if (zone.isEmpty() || zone == "\x1B") return;
    String nsStr = keyboard(WiFi.dnsIP(0).toString(), 15, "NS IP");
    IPAddress ns;
    if (!ns.fromString(nsStr)) {
        displayError("Bad NS IP", true);
        return;
    }

    WiFiClient client;
    client.setTimeout(5);
    drawMainBorderWithTitle("AXFR");
    padprintln(zone);
    padprintln(ns.toString());
    if (!client.connect(ns, 53, 4000)) {
        displayError("TCP/53 fail", true);
        return;
    }

    // TCP DNS: 2-byte length + message. Message starts at offset 2.
    uint8_t q[300];
    memset(q, 0, sizeof(q));
    const size_t msgStart = 2;
    q[msgStart + 0] = 0x12;
    q[msgStart + 1] = 0x34;
    q[msgStart + 4] = 0x00;
    q[msgStart + 5] = 0x01; // QDCOUNT=1
    String err;
    size_t pos = dnsEncodeName(q, sizeof(q), msgStart + 12, zone, err);
    if (pos == 0) {
        client.stop();
        displayError(err, true);
        return;
    }
    q[pos++] = 0x00;
    q[pos++] = 252; // AXFR
    q[pos++] = 0x00;
    q[pos++] = 0x01;
    uint16_t msgLen = (uint16_t)(pos - msgStart);
    q[0] = (msgLen >> 8) & 0xff;
    q[1] = msgLen & 0xff;
    client.write(q, pos);

    std::vector<String> names;
    std::vector<String> rows;
    rows.push_back("zone,ns,name");
    const size_t kMaxNames = 64;
    const size_t kMaxBytes = 8192;
    size_t total = 0;
    uint8_t buf[512];
    uint32_t t0 = millis();
    EscPress = false;
    while (millis() - t0 < 8000 && total < kMaxBytes && names.size() < kMaxNames &&
           !check(EscPress) && !returnToMenu) {
        if (!client.connected() && !client.available()) break;
        if (client.available() < 2) {
            delay(20);
            continue;
        }
        int hi = client.read();
        int lo = client.read();
        if (hi < 0 || lo < 0) break;
        uint16_t rlen = ((uint16_t)hi << 8) | (uint16_t)lo;
        if (rlen == 0 || rlen > sizeof(buf)) break;
        size_t got = 0;
        uint32_t w0 = millis();
        while (got < rlen && millis() - w0 < 3000) {
            if (client.available()) {
                int n = client.read(buf + got, rlen - got);
                if (n > 0) got += n;
            } else delay(5);
        }
        if (got < rlen) break;
        total += got;
        dnsHarvestNames(buf, (int)got, names, kMaxNames);
        // Stop after second SOA typically ends AXFR — count SOA markers loosely
        if (got >= 12) {
            uint16_t an = (buf[6] << 8) | buf[7];
            if (an == 0 && names.size() > 0) break;
        }
    }
    client.stop();

    for (const auto &n : names) rows.push_back(zone + "," + ns.toString() + "," + n);
    if (names.empty()) rows.push_back(zone + "," + ns.toString() + ",(none/refused)");
    saveScanCsv("axfr", rows);
    displayInfo(String(names.size()) + " names", true);
    showLines("AXFR", names.empty() ? std::vector<String>{"(none — often refused)"} : names);
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

    IPAddress resolver = pickResolver();

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

void cacheSnoopMenu() {
    if (!ensureSta()) return;
    IPAddress resolver = pickResolver();

    std::vector<String> names = {
        "www.google.com",
        "www.cloudflare.com",
        "www.microsoft.com",
        "www.apple.com",
        "www.amazon.com",
        "www.facebook.com",
        "www.youtube.com",
        "www.wikipedia.org",
    };
    FS *fs = toolkitFs();
    if (fs && fs->exists(String(kvx::paths::NETOPS_TOOLKIT) + "/names.txt")) {
        File f = fs->open(String(kvx::paths::NETOPS_TOOLKIT) + "/names.txt", FILE_READ);
        if (f) {
            names.clear();
            while (f.available() && names.size() < 32) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() && !line.startsWith("#")) names.push_back(line);
            }
            f.close();
        }
    }

    drawMainBorderWithTitle("Cache Snoop");
    padprintln(resolver.toString());
    padprintln("RD=0  ESC abort");
    std::vector<String> rows;
    rows.push_back("name,hit,answers");
    std::vector<String> lines;
    EscPress = false;
    String painted;
    for (size_t i = 0; i < names.size() && !check(EscPress) && !returnToMenu; i++) {
        String hud = String(i + 1) + "/" + String(names.size()) + " " + names[i].substring(0, 20);
        if (hud != painted) {
            tft.fillRect(10, uiStatusY(0), tftWidth - 20, LH * FP + 2, kvxConfig.bgColor);
            tft.drawString(hud, 10, uiStatusY(0));
            painted = hud;
        }
        std::vector<String> answers;
        String err;
        bool ok = dnsQuery(resolver, names[i], 1, answers, err, 0x0000);
        bool hit = ok && !answers.empty() && answers[0] != "(no answers)";
        String ans = hit ? answers[0] : (ok ? "empty" : err);
        ans.replace(",", ";");
        rows.push_back(names[i] + "," + String(hit ? "1" : "0") + "," + ans);
        lines.push_back(String(hit ? "HIT " : "miss ") + names[i].substring(0, 22));
        delay(30);
    }
    saveScanCsv("snoop", rows);
    displayInfo(String(lines.size()) + " queries", true);
    showLines("Cache Snoop", lines);
}

struct TraceHop {
    volatile bool done = false;
    volatile bool ok = false;
    volatile uint32_t rtt = 0;
    volatile uint32_t replyTtl = 0;
    char ipStr[24] = {0};
};

static void onTraceOk(esp_ping_handle_t hdl, void *args) {
    auto *st = (TraceHop *)args;
    uint32_t ttl = 0, time_ms = 0;
    ip_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &time_ms, sizeof(time_ms));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &addr, sizeof(addr));
    st->ok = true;
    st->rtt = time_ms;
    st->replyTtl = ttl;
    if (addr.type == IPADDR_TYPE_V4) {
        IPAddress ip(addr.u_addr.ip4.addr);
        snprintf(st->ipStr, sizeof(st->ipStr), "%s", ip.toString().c_str());
    }
}

static void onTraceTimeout(esp_ping_handle_t hdl, void *args) {
    auto *st = (TraceHop *)args;
    st->ok = false;
}

static void onTraceEnd(esp_ping_handle_t hdl, void *args) {
    auto *st = (TraceHop *)args;
    st->done = true;
    esp_ping_delete_session(hdl);
}

void tracerouteMenu() {
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

    drawMainBorderWithTitle("Trace");
    padprintln(host);
    padprintln(ip.toString());
    std::vector<String> rows;
    rows.push_back("hop,ttl,ip,rtt_ms");
    std::vector<String> lines;
    EscPress = false;
    String painted;
    bool reached = false;
    for (int ttl = 1; ttl <= 8 && !reached && !check(EscPress) && !returnToMenu; ttl++) {
        TraceHop hop;
        esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
        cfg.target_addr = target;
        cfg.count = 1;
        cfg.timeout_ms = 1200;
        cfg.interval_ms = 200;
        cfg.ttl = ttl;
        esp_ping_callbacks_t cbs = {
            .cb_args = &hop,
            .on_ping_success = onTraceOk,
            .on_ping_timeout = onTraceTimeout,
            .on_ping_end = onTraceEnd,
        };
        esp_ping_handle_t hdl = nullptr;
        if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) {
            lines.push_back(String(ttl) + " err");
            break;
        }
        esp_ping_start(hdl);
        while (!hop.done && !check(EscPress)) delay(20);
        if (!hop.done) {
            esp_ping_stop(hdl);
            esp_ping_delete_session(hdl);
        }
        String ipOut = hop.ok ? String(hop.ipStr) : "*";
        String line = String(ttl) + " " + ipOut + (hop.ok ? (" " + String(hop.rtt) + "ms") : " timeout");
        lines.push_back(line);
        rows.push_back(
            String(ttl) + "," + String(ttl) + "," + ipOut + "," + (hop.ok ? String(hop.rtt) : "")
        );
        if (line != painted) {
            tft.fillRect(10, uiStatusY(0), tftWidth - 20, LH * FP + 2, kvxConfig.bgColor);
            tft.drawString(line.substring(0, 28), 10, uiStatusY(0));
            painted = line;
        }
        if (hop.ok && strcmp(hop.ipStr, ip.toString().c_str()) == 0) reached = true;
    }
    saveScanCsv("trace", rows);
    displayInfo(reached ? "reached" : "done", true);
    showLines("Trace", lines);
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
            "CSV -> netops/toolkit/scans/",
            "",
            "Port Scan / Banner / Ping",
            "Dig: A/AAAA/CNAME/TXT/MX",
            "Curl-lite: GET/HEAD/POST",
            "",
            "AXFR:",
            "  TCP/53 zone transfer try.",
            "  Often refused — lab only.",
            "",
            "Cache Snoop:",
            "  RD=0 queries; HIT if cached.",
            "  names.txt or built-in list.",
            "",
            "Trace:",
            "  ICMP TTL 1..8 hop probe.",
            "",
            "Not full nmap/dig. Lab use.",
        }
    );
}

void netToolkitMenu() {
    options = {
        {"Help",         netToolkitHelp },
        {"Port Scan",    portScanMenu   },
        {"Banner Grab",  bannerGrabMenu },
        {"Ping",         pingMenu       },
        {"Dig",          digMenu        },
        {"AXFR",         axfrMenu       },
        {"Cache Snoop",  cacheSnoopMenu },
        {"Trace",        tracerouteMenu },
        {"Curl-lite",    curlMenu       },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Net Toolkit");
    options.clear();
}

#endif // LITE_VERSION

#include "roku_ecp.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <globals.h>

void RokuEcp::setBase(const String &ip) {
    _ip = ip;
    _ip.trim();
    // Strip accidental scheme/port/path if a LOCATION URL was saved
    if (_ip.startsWith("http://")) _ip.remove(0, 7);
    else if (_ip.startsWith("https://")) _ip.remove(0, 8);
    int slash = _ip.indexOf('/');
    if (slash >= 0) _ip = _ip.substring(0, slash);
    int colon = _ip.indexOf(':');
    if (colon >= 0) _ip = _ip.substring(0, colon);
    _base = "http://" + _ip + ":8060";
}

String RokuEcp::statusMessage(RokuEcpStatus st) {
    switch (st) {
        case ROKU_ECP_OK: return "OK";
        case ROKU_ECP_FORBIDDEN: return "Enable Control by mobile apps";
        case ROKU_ECP_WIFI_DOWN: return "WiFi down";
        case ROKU_ECP_CANCELLED: return "Cancelled";
        case ROKU_ECP_UNREACHABLE:
        default: return "Roku unreachable";
    }
}

/*
 * Raw HTTP/1.1 over WiFiClient.
 *
 * Arduino HTTPClient's POST("") omits Content-Length when the body is empty.
 * Roku ECP often answers that with 403 Forbidden (same code as "Control by
 * mobile apps" disabled), so keypress looked broken while GET device-info
 * still worked. Also Host must be the numeric IP — never a hostname.
 */
RokuEcpStatus RokuEcp::request(const char *method, const String &path, String *bodyOut, int timeoutMs) {
    if (!WiFi.isConnected()) return ROKU_ECP_WIFI_DOWN;
    if (_ip.isEmpty()) return ROKU_ECP_UNREACHABLE;

    WiFiClient client;
    client.setTimeout(timeoutMs / 1000 + 1);
    if (!client.connect(_ip.c_str(), 8060)) return ROKU_ECP_UNREACHABLE;

    const bool isPost = (strcmp(method, "POST") == 0);

    // Request line + headers. Host is IP only (Roku 403s on hostname Host).
    client.print(method);
    client.print(' ');
    client.print(path);
    client.print(" HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(_ip);
    client.print("\r\n");
    client.print("User-Agent: kvxputer\r\n");
    client.print("Connection: close\r\n");
    if (isPost) client.print("Content-Length: 0\r\n");
    client.print("\r\n");
    client.flush();

    // Status line: HTTP/1.x NNN ...
    unsigned long deadline = millis() + (unsigned long)timeoutMs;
    String statusLine;
    while (millis() < deadline) {
        if (!client.connected() && !client.available()) break;
        if (!client.available()) {
            delay(1);
            continue;
        }
        statusLine = client.readStringUntil('\n');
        statusLine.trim();
        break;
    }
    if (statusLine.isEmpty()) {
        client.stop();
        return ROKU_ECP_UNREACHABLE;
    }

    int code = 0;
    // "HTTP/1.1 200 OK" or "HTTP/1.0 403 Forbidden"
    int sp1 = statusLine.indexOf(' ');
    if (sp1 >= 0) {
        int sp2 = statusLine.indexOf(' ', sp1 + 1);
        String codeStr = (sp2 > sp1) ? statusLine.substring(sp1 + 1, sp2) : statusLine.substring(sp1 + 1);
        code = codeStr.toInt();
    }

    // Drain headers (and optionally capture a small body)
    bool headersDone = false;
    String body;
    while (millis() < deadline) {
        if (!client.connected() && !client.available()) break;
        if (!client.available()) {
            delay(1);
            continue;
        }
        if (!headersDone) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.isEmpty()) headersDone = true;
            continue;
        }
        if (bodyOut) {
            while (client.available() && (int)body.length() < 4096) {
                char c = (char)client.read();
                body += c;
            }
        } else {
            while (client.available()) (void)client.read();
        }
    }
    client.stop();

    if (bodyOut) *bodyOut = body;

    if (code == 403) return ROKU_ECP_FORBIDDEN;
    if (code >= 200 && code < 300) return ROKU_ECP_OK;
    return ROKU_ECP_UNREACHABLE;
}

RokuEcpStatus RokuEcp::postKey(const char *key) {
    if (!key || !key[0]) return ROKU_ECP_UNREACHABLE;
    String path = String("/keypress/") + key;
    return request("POST", path, nullptr, 1500);
}

RokuEcpStatus RokuEcp::launch(const String &appId) {
    if (appId.isEmpty()) return ROKU_ECP_UNREACHABLE;
    return request("POST", "/launch/" + appId, nullptr, 2000);
}

String RokuEcp::scrapeTag(const String &xml, const char *tag) {
    String open = String("<") + tag + ">";
    String close = String("</") + tag + ">";
    int a = xml.indexOf(open);
    if (a < 0) return "";
    a += open.length();
    int b = xml.indexOf(close, a);
    if (b < 0) return "";
    String v = xml.substring(a, b);
    v.trim();
    return v;
}

void RokuEcp::scrapeApps(const String &xml, std::vector<RokuApp> &out) {
    out.clear();
    int pos = 0;
    while (pos < (int)xml.length() && out.size() < 200) {
        int a = xml.indexOf("<app", pos);
        if (a < 0) break;
        int idAttr = xml.indexOf("id=\"", a);
        int tagEnd = xml.indexOf('>', a);
        if (idAttr < 0 || tagEnd < 0 || idAttr > tagEnd) {
            pos = a + 4;
            continue;
        }
        idAttr += 4;
        int idEnd = xml.indexOf('"', idAttr);
        if (idEnd < 0) break;
        String id = xml.substring(idAttr, idEnd);
        int nameStart = tagEnd + 1;
        int nameEnd = xml.indexOf("</app>", nameStart);
        if (nameEnd < 0) break;
        String name = xml.substring(nameStart, nameEnd);
        name.trim();
        if (id.length() && name.length()) out.push_back({id, name});
        pos = nameEnd + 6;
    }
}

RokuEcpStatus RokuEcp::queryDeviceInfo(RokuDevice &out, int timeoutMs) {
    String body;
    RokuEcpStatus st = request("GET", "/query/device-info", &body, timeoutMs);
    if (st != ROKU_ECP_OK) return st;

    out.ip = _ip;
    out.name = scrapeTag(body, "user-device-name");
    if (out.name.isEmpty()) out.name = scrapeTag(body, "friendly-device-name");
    if (out.name.isEmpty()) out.name = _ip;
    out.model = scrapeTag(body, "model-name");
    out.serial = scrapeTag(body, "serial-number");
    return ROKU_ECP_OK;
}

RokuEcpStatus RokuEcp::queryApps(std::vector<RokuApp> &out, int timeoutMs) {
    String body;
    RokuEcpStatus st = request("GET", "/query/apps", &body, timeoutMs);
    if (st != ROKU_ECP_OK) return st;
    scrapeApps(body, out);
    return ROKU_ECP_OK;
}

String RokuEcp::headerValue(const String &msg, const char *name) {
    String lower = msg;
    lower.toLowerCase();
    String key = String(name);
    key.toLowerCase();
    key += ":";
    int i = lower.indexOf(key);
    if (i < 0) return "";
    i = msg.indexOf(':', i) + 1;
    while (i < (int)msg.length() && (msg[i] == ' ' || msg[i] == '\t')) i++;
    int e = msg.indexOf('\r', i);
    if (e < 0) e = msg.indexOf('\n', i);
    if (e < 0) e = msg.length();
    String v = msg.substring(i, e);
    v.trim();
    return v;
}

String RokuEcp::extractIpFromLocation(const String &loc) {
    int start = loc.indexOf("://");
    if (start < 0) return "";
    start += 3;
    int end = loc.indexOf(':', start);
    int slash = loc.indexOf('/', start);
    if (end < 0 || (slash >= 0 && slash < end)) end = slash;
    if (end < 0) end = loc.length();
    String ip = loc.substring(start, end);
    ip.trim();
    return ip;
}

std::vector<RokuDevice> RokuEcp::ssdpDiscover(unsigned long timeoutMs) {
    std::vector<RokuDevice> devices;
    if (!WiFi.isConnected()) return devices;

    WiFiUDP udp;
    // Bind 1901 — do not steal 1900 from the poisoner / other tools
    if (!udp.begin(1901)) return devices;

    const char *msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: roku:ecp\r\n"
        "\r\n";

    udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    udp.write((const uint8_t *)msearch, strlen(msearch));
    udp.endPacket();

    unsigned long until = millis() + timeoutMs;
    char buf[512];
    while (millis() < until) {
        if (returnToMenu || forceHome || EscPress) break;
        int n = udp.parsePacket();
        if (n > 0) {
            int got = udp.read((uint8_t *)buf, sizeof(buf) - 1);
            if (got > 0) {
                buf[got] = 0;
                String s(buf);
                String loc = headerValue(s, "LOCATION");
                if (loc.isEmpty()) loc = headerValue(s, "Location");
                String usn = headerValue(s, "USN");
                String ip = extractIpFromLocation(loc);
                if (ip.length()) {
                    bool dup = false;
                    for (auto &d : devices) {
                        if (d.ip == ip) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        RokuDevice d;
                        d.ip = ip;
                        d.serial = usn;
                        d.name = ip;
                        devices.push_back(d);
                    }
                }
            }
        }
        delay(20);
    }
    udp.stop();
    return devices;
}

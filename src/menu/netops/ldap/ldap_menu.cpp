/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * LDAP anonymous/simple bind + subtree search dump to storage.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ldap.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>

namespace {

void berLen(uint8_t *out, size_t *n, size_t len) {
    if (len < 128) {
        out[(*n)++] = (uint8_t)len;
        return;
    }
    out[(*n)++] = 0x81;
    out[(*n)++] = (uint8_t)len;
}

size_t bindSimple(uint8_t *buf, const String &dn, const String &pw) {
    uint8_t inner[160];
    size_t i = 0;
    inner[i++] = 0x02;
    inner[i++] = 0x01;
    inner[i++] = 0x03;
    inner[i++] = 0x04;
    berLen(inner, &i, dn.length());
    memcpy(inner + i, dn.c_str(), dn.length());
    i += dn.length();
    inner[i++] = 0x80;
    berLen(inner, &i, pw.length());
    memcpy(inner + i, pw.c_str(), pw.length());
    i += pw.length();
    uint8_t bind[180];
    size_t b = 0;
    bind[b++] = 0x60;
    berLen(bind, &b, i);
    memcpy(bind + b, inner, i);
    b += i;
    size_t p = 0;
    buf[p++] = 0x30;
    size_t seqLen = 3 + b;
    berLen(buf, &p, seqLen);
    buf[p++] = 0x02;
    buf[p++] = 0x01;
    buf[p++] = 0x01;
    memcpy(buf + p, bind, b);
    p += b;
    return p;
}

size_t searchReq(uint8_t *buf, const String &base) {
    // SearchRequest msgid=2, base, wholeSubtree, filter (objectClass=*), attrs empty, sizeLimit 50
    static const uint8_t tail[] = {
        0x0a, 0x01, 0x02, 0x0a, 0x01, 0x00, 0x02, 0x01, 0x32, 0x02, 0x01, 0x00,
        0x01, 0x01, 0x00, 0x87, 0x0b, 'o', 'b', 'j', 'e', 'c', 't', 'C', 'l', 'a', 's', 's', 0x30, 0x00
    };
    uint8_t inner[200];
    size_t i = 0;
    inner[i++] = 0x04;
    berLen(inner, &i, base.length());
    memcpy(inner + i, base.c_str(), base.length());
    i += base.length();
    memcpy(inner + i, tail, sizeof(tail));
    i += sizeof(tail);
    size_t p = 0;
    buf[p++] = 0x30;
    size_t seq = 3 + 1 + (i < 128 ? 1 : 2) + i;
    (void)seq;
    uint8_t search[220];
    size_t s = 0;
    search[s++] = 0x63;
    berLen(search, &s, i);
    memcpy(search + s, inner, i);
    s += i;
    buf[p++] = (uint8_t)(3 + s);
    buf[p++] = 0x02;
    buf[p++] = 0x01;
    buf[p++] = 0x02;
    memcpy(buf + p, search, s);
    p += s;
    // fix outer length at buf[1] if short form
    buf[1] = (uint8_t)(p - 2);
    return p;
}

} // namespace

void ldapMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String host = keyboard(WiFi.gatewayIP().toString(), 48, "LDAP host");
    if (host.length() == 0 || host == "\x1B") return;
    String base = keyboard("dc=local,dc=lan", 80, "Base DN");
    if (base == "\x1B") return;
    String user = keyboard("", 64, "Bind DN (empty=anon)");
    if (user == "\x1B") return;
    String pw = user.length() ? keyboard("", 32, "Password") : "";
    WiFiClient c;
    drawMainBorderWithTitle("LDAP Dump");
    if (!c.connect(host.c_str(), 389)) {
        displayError("LDAP connect failed", true);
        return;
    }
    uint8_t pkt[256];
    size_t n = bindSimple(pkt, user, pw);
    c.write(pkt, n);
    delay(200);
    String resp;
    unsigned long t = millis();
    while (millis() - t < 1500) {
        while (c.available()) resp += (char)c.read();
        delay(20);
    }
    n = searchReq(pkt, base);
    c.write(pkt, n);
    t = millis();
    while (millis() - t < 4000) {
        while (c.available()) resp += (char)c.read();
        delay(20);
    }
    c.stop();

    FS *fs = nullptr;
    String saved;
    if (getFsStorage(fs) && fs) {
        kvx::paths::ensureDir(*fs, kvx::paths::NETOPS_CRAWLER);
        String path = String(kvx::paths::NETOPS_CRAWLER) + "/ldap_dump.bin";
        File f = fs->open(path, FILE_WRITE);
        if (f) {
            f.write((const uint8_t *)resp.c_str(), resp.length());
            f.close();
            saved = path;
        }
    }
    displayInfo("LDAP " + String(resp.length()) + " bytes\n" + (saved.length() ? saved : "(not saved)"), true);
}
#endif

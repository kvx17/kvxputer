/**
 * @file ndef_parse.cpp
 * Inspired by wakdev/nfctools-fz; reimplemented for kvxputer (no Flipper source).
 */
#include "ndef_parse.h"

static String bytesToHex(const uint8_t *p, size_t n) {
    String s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < 0x10) s += "0";
        s += String(p[i], HEX);
        if (i + 1 < n) s += " ";
    }
    s.toUpperCase();
    return s;
}

static String uriFromPayload(const uint8_t *payload, size_t len) {
    if (len < 1) return "";
    static const char *prefixes[] = {
        "",
        "http://www.",
        "https://www.",
        "http://",
        "https://",
        "tel:",
        "mailto:",
        "ftp://anonymous:anonymous@",
        "ftp://ftp.",
        "ftps://",
        "sftp://",
        "smb://",
        "nfs://",
        "ftp://",
        "dav://",
        "news:",
        "telnet://",
        "imap:",
        "rtsp://",
        "urn:",
        "pop:",
        "sip:",
        "sips:",
        "tftp:",
        "btspp://",
        "btl2cap://",
        "btgoep://",
        "tcpobex://",
        "irdaobex://",
        "file://",
        "urn:epc:id:",
        "urn:epc:tag:",
        "urn:epc:pat:",
        "urn:epc:raw:",
        "urn:epc:",
        "urn:nfc:",
    };
    uint8_t code = payload[0];
    String prefix = (code < sizeof(prefixes) / sizeof(prefixes[0])) ? prefixes[code] : "";
    String rest;
    for (size_t i = 1; i < len; i++) rest += (char)payload[i];
    return prefix + rest;
}

static void parseRecord(const uint8_t *rec, size_t len, std::vector<NdefParsedRecord> &out) {
    if (len < 3) return;
    uint8_t flags = rec[0];
    uint8_t tnf = flags & 0x07;
    bool sr = flags & 0x10;
    bool il = flags & 0x08;
    uint8_t typeLen = rec[1];
    size_t idx = 2;
    uint32_t payloadLen = 0;
    if (sr) {
        if (idx >= len) return;
        payloadLen = rec[idx++];
    } else {
        if (idx + 4 > len) return;
        payloadLen = ((uint32_t)rec[idx] << 24) | ((uint32_t)rec[idx + 1] << 16) |
                     ((uint32_t)rec[idx + 2] << 8) | rec[idx + 3];
        idx += 4;
    }
    uint8_t idLen = 0;
    if (il) {
        if (idx >= len) return;
        idLen = rec[idx++];
    }
    if (idx + typeLen + idLen + payloadLen > len) return;
    String typeStr;
    for (uint8_t i = 0; i < typeLen; i++) typeStr += (char)rec[idx + i];
    idx += typeLen;
    idx += idLen;
    const uint8_t *payload = rec + idx;

    NdefParsedRecord r;
    if (tnf == 0x01 && typeStr == "U") {
        r.typeLabel = "URI";
        r.summary = uriFromPayload(payload, payloadLen);
        r.detail = r.summary;
        r.qrPayload = r.summary;
    } else if (tnf == 0x01 && typeStr == "T") {
        r.typeLabel = "Text";
        size_t langLen = (payloadLen > 0) ? (payload[0] & 0x3F) : 0;
        String text;
        for (size_t i = 1 + langLen; i < payloadLen; i++) text += (char)payload[i];
        r.summary = text;
        r.detail = text;
        if (text.length() <= 100) r.qrPayload = text;
    } else if (tnf == 0x02) {
        r.typeLabel = typeStr.length() ? typeStr : "MIME";
        r.summary = String(payloadLen) + " bytes";
        r.detail = "MIME: " + typeStr + "\n" + bytesToHex(payload, payloadLen > 64 ? 64 : payloadLen);
        if (typeStr == "text/vcard") {
            String v;
            for (size_t i = 0; i < payloadLen; i++) v += (char)payload[i];
            r.summary = "vCard";
            r.detail = v;
        }
    } else if (tnf == 0x00) {
        r.typeLabel = "Empty";
        r.summary = "(empty)";
        r.detail = "(empty record)";
    } else {
        r.typeLabel = "TNF " + String(tnf);
        r.summary = typeStr.length() ? typeStr : bytesToHex(payload, payloadLen > 16 ? 16 : payloadLen);
        r.detail = bytesToHex(payload, payloadLen > 64 ? 64 : payloadLen);
    }
    out.push_back(r);
}

static void parseMessage(const uint8_t *msg, size_t len, std::vector<NdefParsedRecord> &out) {
    size_t off = 0;
    while (off + 3 <= len) {
        uint8_t flags = msg[off];
        bool me = flags & 0x40;
        size_t start = off;
        uint8_t typeLen = msg[off + 1];
        bool sr = flags & 0x10;
        bool il = flags & 0x08;
        size_t idx = off + 2;
        uint32_t payloadLen = 0;
        if (sr) {
            if (idx >= len) break;
            payloadLen = msg[idx++];
        } else {
            if (idx + 4 > len) break;
            payloadLen = ((uint32_t)msg[idx] << 24) | ((uint32_t)msg[idx + 1] << 16) |
                         ((uint32_t)msg[idx + 2] << 8) | msg[idx + 3];
            idx += 4;
        }
        uint8_t idLen = 0;
        if (il) {
            if (idx >= len) break;
            idLen = msg[idx++];
        }
        size_t recLen = (idx - start) + typeLen + idLen + payloadLen;
        if (start + recLen > len) break;
        parseRecord(msg + start, recLen, out);
        off = start + recLen;
        if (me) break;
    }
}

std::vector<NdefParsedRecord> ndefParseFromBytes(const uint8_t *data, size_t len) {
    std::vector<NdefParsedRecord> out;
    if (!data || len < 2) return out;

    // TLV wrap?
    size_t off = 0;
    while (off < len) {
        uint8_t t = data[off++];
        if (t == 0x00) continue; // NULL
        if (t == 0xFE) break;    // Terminator
        if (off >= len) break;
        size_t l = data[off++];
        if (l == 0xFF) {
            if (off + 2 > len) break;
            l = ((size_t)data[off] << 8) | data[off + 1];
            off += 2;
        }
        if (off + l > len) break;
        if (t == 0x03) parseMessage(data + off, l, out);
        off += l;
    }
    if (out.empty() && len >= 3) {
        // Bare NDEF message (T4T)
        parseMessage(data, len, out);
    }
    return out;
}

std::vector<NdefParsedRecord> ndefParseFromPages(const String &strAllPages) {
    std::vector<uint8_t> mem;
    // Collect page bytes starting from page 4 (user memory) when present;
    // also accept Block lines.
    int lineStart = 0;
    while (lineStart < (int)strAllPages.length()) {
        int nl = strAllPages.indexOf('\n', lineStart);
        if (nl < 0) nl = strAllPages.length();
        String line = strAllPages.substring(lineStart, nl);
        line.trim();
        lineStart = nl + 1;
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        String head = line.substring(0, colon);
        String hex = line.substring(colon + 1);
        hex.trim();
        bool take = false;
        if (head.startsWith("Page ")) {
            int page = head.substring(5).toInt();
            if (page >= 4) take = true;
        } else if (head.startsWith("Block ")) {
            take = true;
        }
        if (!take) continue;
        hex.replace(" ", "");
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            mem.push_back((uint8_t)strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16));
        }
    }
    return ndefParseFromBytes(mem.data(), mem.size());
}

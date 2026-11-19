#include "nfc_ndef.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace tools {
namespace {

std::vector<uint8_t> pages_to_bytes(const std::string& pages_text) {
    std::vector<uint8_t> out;
    std::istringstream in(pages_text);
    std::string line;
    while (std::getline(in, line)) {
        auto c = line.find(':');
        if (c == std::string::npos) continue;
        std::string hex;
        for (char ch : line.substr(c + 1)) {
            if (std::isxdigit(static_cast<unsigned char>(ch))) hex.push_back(ch);
        }
        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
            out.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
        }
    }
    return out;
}

std::string uri_from_payload(const uint8_t* payload, size_t len) {
    if (len < 1) return {};
    static const char* prefixes[] = {
        "",          "http://www.", "https://www.", "http://", "https://", "tel:",
        "mailto:",   "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://",
        "smb://",    "nfs://",      "ftp://",       "dav://",  "news:",    "telnet://",
        "imap:",     "rtsp://",     "urn:",         "pop:",    "sip:",     "sips:",
        "tftp:",     "btspp://",    "btl2cap://",   "btgoep://","tcpobex://","irdaobex://",
        "file://",   "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:",
        "urn:epc:",  "urn:nfc:",
    };
    const uint8_t code = payload[0];
    std::string prefix =
        (code < sizeof(prefixes) / sizeof(prefixes[0])) ? prefixes[code] : "";
    std::string rest(reinterpret_cast<const char*>(payload + 1),
                     reinterpret_cast<const char*>(payload + len));
    return prefix + rest;
}

void parse_record(const uint8_t* rec, size_t len, std::vector<NdefRecord>& out) {
    if (len < 3) return;
    const uint8_t flags = rec[0];
    const uint8_t tnf = flags & 0x07;
    const bool sr = flags & 0x10;
    const bool il = flags & 0x08;
    const uint8_t type_len = rec[1];
    size_t idx = 2;
    uint32_t payload_len = 0;
    if (sr) {
        if (idx >= len) return;
        payload_len = rec[idx++];
    } else {
        if (idx + 4 > len) return;
        payload_len = (static_cast<uint32_t>(rec[idx]) << 24) |
                      (static_cast<uint32_t>(rec[idx + 1]) << 16) |
                      (static_cast<uint32_t>(rec[idx + 2]) << 8) | rec[idx + 3];
        idx += 4;
    }
    uint8_t id_len = 0;
    if (il) {
        if (idx >= len) return;
        id_len = rec[idx++];
    }
    if (idx + type_len + id_len + payload_len > len) return;
    const uint8_t* type = rec + idx;
    idx += type_len;
    idx += id_len;
    const uint8_t* payload = rec + idx;

    NdefRecord r;
    if (tnf == 0x01 && type_len == 1 && type[0] == 'U') {
        r.type_label = "URI";
        r.summary = uri_from_payload(payload, payload_len);
        r.qr_payload = r.summary;
        r.detail = r.summary;
    } else if (tnf == 0x01 && type_len == 1 && type[0] == 'T') {
        r.type_label = "Text";
        size_t start = 0;
        if (payload_len > 0) start = 1 + (payload[0] & 0x3F);
        if (start > payload_len) start = payload_len;
        r.summary.assign(reinterpret_cast<const char*>(payload + start),
                         reinterpret_cast<const char*>(payload + payload_len));
        r.qr_payload = r.summary;
        r.detail = r.summary;
    } else if (tnf == 0x04 && type_len == 15 &&
               std::string(reinterpret_cast<const char*>(type), 15) == "android.com:pkg") {
        r.type_label = "Android App";
        r.summary.assign(reinterpret_cast<const char*>(payload),
                         reinterpret_cast<const char*>(payload + payload_len));
        r.detail = r.summary;
    } else {
        r.type_label = "Record";
        char buf[48];
        std::snprintf(buf, sizeof(buf), "TNF=%u typeLen=%u payload=%u", tnf, type_len, payload_len);
        r.summary = buf;
        r.detail = buf;
    }
    out.push_back(std::move(r));
}

std::vector<uint8_t> wrap_tlv(const std::vector<uint8_t>& message) {
    std::vector<uint8_t> tlv;
    tlv.push_back(0x03);
    if (message.size() < 0xFF) {
        tlv.push_back(static_cast<uint8_t>(message.size()));
    } else {
        tlv.push_back(0xFF);
        tlv.push_back(static_cast<uint8_t>((message.size() >> 8) & 0xFF));
        tlv.push_back(static_cast<uint8_t>(message.size() & 0xFF));
    }
    tlv.insert(tlv.end(), message.begin(), message.end());
    tlv.push_back(0xFE);
    return tlv;
}

std::vector<uint8_t> make_uri_record(const std::string& url) {
    static const struct {
        const char* prefix;
        uint8_t code;
    } kAbbrev[] = {
        {"https://www.", 2}, {"http://www.", 1}, {"https://", 4}, {"http://", 3},
        {"tel:", 5},         {"mailto:", 6},
    };
    uint8_t code = 0;
    std::string data = url;
    for (const auto& e : kAbbrev) {
        const size_t n = std::char_traits<char>::length(e.prefix);
        if (data.rfind(e.prefix, 0) == 0) {
            code = e.code;
            data = data.substr(n);
            break;
        }
    }
    std::vector<uint8_t> payload;
    payload.push_back(code);
    payload.insert(payload.end(), data.begin(), data.end());
    std::vector<uint8_t> rec;
    rec.push_back(0xD1);
    rec.push_back(0x01);
    rec.push_back(static_cast<uint8_t>(payload.size()));
    rec.push_back('U');
    rec.insert(rec.end(), payload.begin(), payload.end());
    return wrap_tlv(rec);
}

} // namespace

std::vector<NdefRecord> ndef_parse_from_pages(const std::string& pages_text) {
    std::vector<NdefRecord> out;
    const auto mem = pages_to_bytes(pages_text);
    // Skip CC (pages 0-3 = 16 bytes) and find TLV 0x03
    size_t i = 16;
    while (i < mem.size()) {
        const uint8_t t = mem[i++];
        if (t == 0x00) continue;
        if (t == 0xFE) break;
        if (i >= mem.size()) break;
        size_t len = mem[i++];
        if (len == 0xFF) {
            if (i + 1 >= mem.size()) break;
            len = (static_cast<size_t>(mem[i]) << 8) | mem[i + 1];
            i += 2;
        }
        if (i + len > mem.size()) break;
        if (t == 0x03) parse_record(mem.data() + i, len, out);
        i += len;
    }
    return out;
}

std::vector<uint8_t> ndef_tlv_uri(const std::string& url) { return make_uri_record(url); }

std::vector<uint8_t> ndef_tlv_text(const std::string& text) {
    std::vector<uint8_t> payload;
    payload.push_back(0x02);
    payload.push_back('e');
    payload.push_back('n');
    payload.insert(payload.end(), text.begin(), text.end());
    std::vector<uint8_t> rec;
    rec.push_back(0xD1);
    rec.push_back(0x01);
    rec.push_back(static_cast<uint8_t>(payload.size()));
    rec.push_back('T');
    rec.insert(rec.end(), payload.begin(), payload.end());
    return wrap_tlv(rec);
}

std::vector<uint8_t> ndef_tlv_android_app(const std::string& package) {
    const std::string type = "android.com:pkg";
    std::vector<uint8_t> rec;
    rec.push_back(0xD4); // MB ME SR TNF=external
    rec.push_back(static_cast<uint8_t>(type.size()));
    rec.push_back(static_cast<uint8_t>(package.size()));
    rec.insert(rec.end(), type.begin(), type.end());
    rec.insert(rec.end(), package.begin(), package.end());
    return wrap_tlv(rec);
}

std::vector<uint8_t> ndef_tlv_wifi(const std::string& ssid, const std::string& password) {
    // Simplified: URI-style wifi string as text for dump generation.
    return ndef_tlv_text("WIFI:S:" + ssid + ";T:WPA;P:" + password + ";;");
}

std::vector<uint8_t> ndef_message_from_tlv(const std::vector<uint8_t>& tlv_or_message) {
    if (tlv_or_message.empty()) return {};
    // Already a bare NDEF record (MB set, typical TNF well-known/external).
    if (tlv_or_message[0] != 0x03) return tlv_or_message;
    size_t i = 1;
    if (i >= tlv_or_message.size()) return {};
    size_t len = tlv_or_message[i++];
    if (len == 0xFF) {
        if (i + 1 >= tlv_or_message.size()) return {};
        len = (static_cast<size_t>(tlv_or_message[i]) << 8) | tlv_or_message[i + 1];
        i += 2;
    }
    if (i + len > tlv_or_message.size()) return {};
    return {tlv_or_message.begin() + static_cast<std::ptrdiff_t>(i),
            tlv_or_message.begin() + static_cast<std::ptrdiff_t>(i + len)};
}

std::vector<uint8_t> ndef_t4t_file_from_tlv(const std::vector<uint8_t>& tlv_or_message, size_t max_len) {
    if (max_len < 2) return {};
    const auto msg = ndef_message_from_tlv(tlv_or_message);
    if (msg.empty() || msg.size() > max_len - 2) return {};
    std::vector<uint8_t> file(max_len, 0x00);
    file[0] = static_cast<uint8_t>((msg.size() >> 8) & 0xFF);
    file[1] = static_cast<uint8_t>(msg.size() & 0xFF);
    std::memcpy(file.data() + 2, msg.data(), msg.size());
    return file;
}

} // namespace tools

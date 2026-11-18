#include "qr_lib.h"

#include "third_party/qrcodegen.hpp"
#include "user_paths.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace tools::qr {
namespace {

uint16_t crc_ccitt_update(uint16_t crc, uint8_t data) {
    crc = static_cast<uint8_t>(crc >> 8) | static_cast<uint16_t>(crc << 8);
    crc ^= data;
    crc ^= static_cast<uint8_t>(crc & 0xff) >> 4;
    crc ^= static_cast<uint16_t>(crc << 12);
    crc ^= static_cast<uint16_t>((crc & 0x00ff) << 5);
    return crc;
}

std::string calculate_crc(const std::string& input) {
    uint16_t crc = 0xffff;
    for (unsigned char c : input) crc = crc_ccitt_update(crc, c);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", crc);
    return buf;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool json_unescape(const std::string& in, size_t& i, std::string& out) {
    out.clear();
    if (i >= in.size() || in[i] != '"') return false;
    ++i;
    while (i < in.size()) {
        char c = in[i++];
        if (c == '"') return true;
        if (c == '\\' && i < in.size()) {
            char e = in[i++];
            out.push_back(e == 'n' ? '\n' : e);
        } else {
            out.push_back(c);
        }
    }
    return false;
}

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

std::string len2(const std::string& s) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02zu", s.size());
    return buf;
}

} // namespace

bool encode_text(const std::string& text, Matrix& out, std::string& error) {
    out = {};
    if (text.empty()) {
        error = "empty text";
        return false;
    }
    try {
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::LOW);
        out.size = qr.getSize();
        out.modules.assign(static_cast<size_t>(out.size * out.size), 0);
        for (int y = 0; y < out.size; ++y) {
            for (int x = 0; x < out.size; ++x) {
                if (qr.getModule(x, y)) {
                    out.modules[static_cast<size_t>(y * out.size + x)] = 1;
                }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

std::string build_pix(const std::string& key, const std::string& amount, std::string& error) {
    if (key.empty()) {
        error = "empty PIX key";
        return {};
    }
    const std::string key_length = len2(key);
    const std::string amt = amount.empty() ? "0" : amount;
    const std::string amount_length = len2(amt);
    const std::string data0 = "0014BR.GOV.BCB.PIX01" + key_length + key;
    std::string pix_code = "00020126" + len2(data0) + data0 + "52040000530398654" + amount_length + amt +
                           "5802BR5909Bruce PIX6014Rio de Janeiro62070503***6304";
    return pix_code + calculate_crc(pix_code);
}

std::string storage_path() {
    return tools::config_file("qrcodes.json");
}

bool load_saved(std::vector<SavedEntry>& entries, std::string& error) {
    entries.clear();
    std::ifstream in(storage_path());
    if (!in) return true;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '[') {
        error = "bad qrcodes.json";
        return false;
    }
    ++i;
    while (true) {
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ']') return true;
        if (i >= raw.size() || raw[i] != '{') {
            error = "bad qrcodes.json object";
            return false;
        }
        ++i;
        SavedEntry e;
        while (true) {
            skip_ws(raw, i);
            if (i < raw.size() && raw[i] == '}') {
                ++i;
                break;
            }
            std::string key;
            if (!json_unescape(raw, i, key)) {
                error = "bad key";
                return false;
            }
            skip_ws(raw, i);
            if (i >= raw.size() || raw[i] != ':') {
                error = "expected :";
                return false;
            }
            ++i;
            skip_ws(raw, i);
            std::string val;
            if (!json_unescape(raw, i, val)) {
                error = "bad value";
                return false;
            }
            if (key == "name") e.name = val;
            else if (key == "content") e.content = val;
            skip_ws(raw, i);
            if (i < raw.size() && raw[i] == ',') ++i;
        }
        if (!e.name.empty()) entries.push_back(std::move(e));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') {
            ++i;
            continue;
        }
        if (i < raw.size() && raw[i] == ']') return true;
        error = "bad qrcodes.json trailing";
        return false;
    }
}

bool save_saved(const std::vector<SavedEntry>& entries, std::string& error) {
    std::ofstream out(storage_path(), std::ios::trunc);
    if (!out) {
        error = "cannot write qrcodes.json";
        return false;
    }
    out << "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "  {\"name\":\"" << json_escape(entries[i].name) << "\",\"content\":\""
            << json_escape(entries[i].content) << "\"}";
        if (i + 1 < entries.size()) out << ',';
        out << '\n';
    }
    out << "]\n";
    out.close();
#if !defined(_WIN32)
    ::chmod(storage_path().c_str(), 0600);
#endif
    return true;
}

bool add_saved(const std::string& name, const std::string& content, std::string& error) {
    std::vector<SavedEntry> entries;
    if (!load_saved(entries, error)) return false;
    for (const auto& e : entries) {
        if (e.name == name) {
            error = "name already exists";
            return false;
        }
    }
    entries.push_back({name, content});
    return save_saved(entries, error);
}

bool remove_saved(const std::string& name, std::string& error) {
    std::vector<SavedEntry> entries;
    if (!load_saved(entries, error)) return false;
    std::vector<SavedEntry> kept;
    for (auto& e : entries) {
        if (e.name != name) kept.push_back(std::move(e));
    }
    return save_saved(kept, error);
}

} // namespace tools::qr

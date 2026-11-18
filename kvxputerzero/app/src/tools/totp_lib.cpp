#include "totp_lib.h"

#include "codec_lib.h"
#include "user_paths.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace tools::totp {
namespace {

int b32_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

void hmac_sha1(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[20]) {
    constexpr size_t kBlock = 64;
    uint8_t k0[kBlock] = {};
    if (key_len > kBlock) {
        tools::codec::sha1(key, key_len, k0);
    } else {
        for (size_t i = 0; i < key_len; ++i) k0[i] = key[i];
    }

    uint8_t ipad[kBlock];
    uint8_t opad[kBlock];
    for (size_t i = 0; i < kBlock; ++i) {
        ipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k0[i] ^ 0x5c);
    }

    std::vector<uint8_t> inner;
    inner.reserve(kBlock + msg_len);
    inner.insert(inner.end(), ipad, ipad + kBlock);
    inner.insert(inner.end(), msg, msg + msg_len);
    uint8_t inner_hash[20];
    tools::codec::sha1(inner.data(), inner.size(), inner_hash);

    std::vector<uint8_t> outer;
    outer.reserve(kBlock + 20);
    outer.insert(outer.end(), opad, opad + kBlock);
    outer.insert(outer.end(), inner_hash, inner_hash + 20);
    tools::codec::sha1(outer.data(), outer.size(), out);
}

// Minimal JSON array of {"name":"...","secret":"..."} — no nested escapes beyond \" \\.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
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
            if (e == 'n') out.push_back('\n');
            else out.push_back(e);
        } else {
            out.push_back(c);
        }
    }
    return false;
}

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
}

} // namespace

bool base32_decode(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == ' ' || c == '-') continue;
        int v = b32_val(c);
        if (v < 0) return false;
        buffer = (buffer << 5) | static_cast<uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return !out.empty();
}

uint32_t hotp(const uint8_t* key, size_t key_len, uint64_t counter) {
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = static_cast<uint8_t>(counter & 0xff);
        counter >>= 8;
    }
    uint8_t hash[20];
    hmac_sha1(key, key_len, msg, 8, hash);
    int offset = hash[19] & 0x0f;
    uint32_t bin = (static_cast<uint32_t>(hash[offset] & 0x7f) << 24) |
                   (static_cast<uint32_t>(hash[offset + 1] & 0xff) << 16) |
                   (static_cast<uint32_t>(hash[offset + 2] & 0xff) << 8) |
                   static_cast<uint32_t>(hash[offset + 3] & 0xff);
    return bin % 1000000u;
}

std::string format_code(uint32_t code) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", code);
    return buf;
}

bool code_at(const Account& account, time_t now, std::string& code, int& remaining, std::string& error) {
    std::vector<uint8_t> key;
    if (!base32_decode(account.secret_b32, key)) {
        error = "bad base32 secret";
        return false;
    }
    const uint64_t counter = static_cast<uint64_t>(now) / 30u;
    remaining = 30 - static_cast<int>(static_cast<uint64_t>(now) % 30u);
    code = format_code(hotp(key.data(), key.size(), counter));
    return true;
}

std::string storage_path() {
    return tools::config_file("totp.json");
}

bool load_accounts(std::vector<Account>& accounts, std::string& error) {
    accounts.clear();
    const std::string path = storage_path();
    std::ifstream in(path);
    if (!in) return true; // missing file = empty list
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '[') {
        error = "bad totp.json";
        return false;
    }
    ++i;
    while (true) {
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ']') return true;
        if (i >= raw.size() || raw[i] != '{') {
            error = "bad totp.json object";
            return false;
        }
        ++i;
        Account acc;
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
            if (key == "name") acc.name = val;
            else if (key == "secret") acc.secret_b32 = val;
            skip_ws(raw, i);
            if (i < raw.size() && raw[i] == ',') ++i;
        }
        if (!acc.name.empty() && !acc.secret_b32.empty()) accounts.push_back(std::move(acc));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') {
            ++i;
            continue;
        }
        if (i < raw.size() && raw[i] == ']') return true;
        error = "bad totp.json trailing";
        return false;
    }
}

bool save_accounts(const std::vector<Account>& accounts, std::string& error) {
    const std::string path = storage_path();
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "cannot write totp.json";
        return false;
    }
    out << "[\n";
    for (size_t i = 0; i < accounts.size(); ++i) {
        out << "  {\"name\":\"" << json_escape(accounts[i].name) << "\",\"secret\":\""
            << json_escape(accounts[i].secret_b32) << "\"}";
        if (i + 1 < accounts.size()) out << ',';
        out << '\n';
    }
    out << "]\n";
    out.close();
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
    return true;
}

} // namespace tools::totp

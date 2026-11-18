#include "codec_lib.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tools::codec {
namespace {

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

std::string digest_to_hex(const uint8_t* d, size_t n) {
    return to_hex(d, n);
}

// Minimal MD5 (RFC 1321)
void md5(const uint8_t* msg, size_t len, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint32_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    std::vector<uint8_t> data(msg, msg + len);
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 0; i < 8; ++i) data.push_back(static_cast<uint8_t>((bit_len >> (8 * i)) & 0xff));

    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = static_cast<uint32_t>(data[offset + i * 4]) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 3]) << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t F, g;
            if (i < 16) {
                F = (B & C) | ((~B) & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | ((~D) & C);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) % 16;
            } else {
                F = C ^ (B | (~D));
                g = (7 * i) % 16;
            }
            F = F + A + K[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + rotl32(F, S[i]);
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }
    uint32_t vals[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        out[i * 4] = static_cast<uint8_t>(vals[i] & 0xff);
        out[i * 4 + 1] = static_cast<uint8_t>((vals[i] >> 8) & 0xff);
        out[i * 4 + 2] = static_cast<uint8_t>((vals[i] >> 16) & 0xff);
        out[i * 4 + 3] = static_cast<uint8_t>((vals[i] >> 24) & 0xff);
    }
}

// Minimal SHA-256
void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    std::vector<uint8_t> data(msg, msg + len);
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<uint8_t>((bit_len >> (8 * i)) & 0xff));

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); };

    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xff);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xff);
    }
}

} // namespace (helpers above)

void sha1(const uint8_t* msg, size_t len, uint8_t out[20]) {
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    std::vector<uint8_t> data(msg, msg + len);
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<uint8_t>((bit_len >> (8 * i)) & 0xff));

    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    auto rotl = [](uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); };

    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[offset + i * 4]) << 24) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<uint8_t>((hs[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xff);
        out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xff);
    }
}

std::string to_hex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(hex[(data[i] >> 4) & 0xf]);
        s.push_back(hex[data[i] & 0xf]);
    }
    return s;
}

std::string from_hex(const std::string& hex, bool& ok) {
    ok = true;
    std::string h;
    h.reserve(hex.size());
    for (char c : hex) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        h.push_back(c);
    }
    if (h.size() % 2) {
        ok = false;
        return {};
    }
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(h.size() / 2);
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        int a = nib(h[i]), b = nib(h[i + 1]);
        if (a < 0 || b < 0) {
            ok = false;
            return {};
        }
        out.push_back(static_cast<char>((a << 4) | b));
    }
    return out;
}

std::string url_encode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xf]);
            out.push_back(hex[c & 0xf]);
        }
    }
    return out;
}

std::string url_decode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char buf[3] = {in[i + 1], in[i + 2], 0};
            out.push_back(static_cast<char>(std::strtol(buf, nullptr, 16)));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string b64_encode(const std::string& in) {
    std::string out;
    size_t i = 0;
    while (i < in.size()) {
        uint32_t octet_a = static_cast<uint8_t>(in[i++]);
        uint32_t octet_b = i < in.size() ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t octet_c = i < in.size() ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out.push_back(kB64[(triple >> 18) & 63]);
        out.push_back(kB64[(triple >> 12) & 63]);
        out.push_back(i > in.size() + 1 ? '=' : kB64[(triple >> 6) & 63]);
        out.push_back(i > in.size() ? '=' : kB64[triple & 63]);
    }
    // Fix padding using remaining count
    const size_t mod = in.size() % 3;
    if (mod) {
        out[out.size() - 1] = '=';
        if (mod == 1) out[out.size() - 2] = '=';
    }
    return out;
}

std::string b64_decode(const std::string& in, bool& ok) {
    ok = true;
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        if (c == '=') return -2;
        return -1;
    };
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v == -1) {
            ok = false;
            return {};
        }
        if (v == -2) break;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xff));
        }
    }
    return out;
}

std::string md5_hex(const uint8_t* data, size_t len) {
    uint8_t out[16];
    md5(data, len, out);
    return digest_to_hex(out, 16);
}

std::string sha1_hex(const uint8_t* data, size_t len) {
    uint8_t out[20];
    sha1(data, len, out);
    return digest_to_hex(out, 20);
}

std::string sha256_hex(const uint8_t* data, size_t len) {
    uint8_t out[32];
    sha256(data, len, out);
    return digest_to_hex(out, 32);
}

const char* op_label(Op op) {
    switch (op) {
    case Op::B64Encode: return "Base64 encode";
    case Op::B64Decode: return "Base64 decode";
    case Op::HexEncode: return "Hex encode";
    case Op::HexDecode: return "Hex decode";
    case Op::UrlEncode: return "URL encode";
    case Op::UrlDecode: return "URL decode";
    case Op::Md5: return "MD5";
    case Op::Sha1: return "SHA1";
    case Op::Sha256: return "SHA256";
    }
    return "Codec";
}

std::string run(Op op, const std::string& input, bool& ok) {
    ok = true;
    const auto* data = reinterpret_cast<const uint8_t*>(input.data());
    switch (op) {
    case Op::B64Encode: return b64_encode(input);
    case Op::B64Decode: return b64_decode(input, ok);
    case Op::HexEncode: return to_hex(data, input.size());
    case Op::HexDecode: return from_hex(input, ok);
    case Op::UrlEncode: return url_encode(input);
    case Op::UrlDecode: return url_decode(input);
    case Op::Md5: return md5_hex(data, input.size());
    case Op::Sha1: return sha1_hex(data, input.size());
    case Op::Sha256: return sha256_hex(data, input.size());
    }
    ok = false;
    return {};
}

} // namespace tools::codec

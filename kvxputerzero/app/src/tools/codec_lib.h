#pragma once

#include <cstdint>
#include <string>

namespace tools::codec {

std::string to_hex(const uint8_t* data, size_t len);
std::string from_hex(const std::string& hex, bool& ok);
std::string url_encode(const std::string& in);
std::string url_decode(const std::string& in);
std::string b64_encode(const std::string& in);
std::string b64_decode(const std::string& in, bool& ok);
std::string md5_hex(const uint8_t* data, size_t len);
void sha1(const uint8_t* data, size_t len, uint8_t out[20]);
std::string sha1_hex(const uint8_t* data, size_t len);
std::string sha256_hex(const uint8_t* data, size_t len);

enum class Op {
    B64Encode = 0,
    B64Decode,
    HexEncode,
    HexDecode,
    UrlEncode,
    UrlDecode,
    Md5,
    Sha1,
    Sha256,
};

const char* op_label(Op op);
std::string run(Op op, const std::string& input, bool& ok);

} // namespace tools::codec

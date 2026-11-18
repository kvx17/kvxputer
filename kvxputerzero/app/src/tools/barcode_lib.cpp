#include "barcode_lib.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tools::barcode {
namespace {

// EAN/UPC digit encodings (from upstream barcode_gen encodings.c)
const char EAN_13_STRUCTURE[10][7] = {
    "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG", "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"
};
const char UPC_EAN_L[10][8] = {
    "0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011"
};
const char EAN_G[10][8] = {
    "0100111", "0110011", "0011011", "0100001", "0011101", "0111001", "0000101", "0010001", "0001001", "0010111"
};
const char UPC_EAN_R[10][8] = {
    "1110010", "1100110", "1101100", "1000010", "1011100", "1001110", "1010000", "1000100", "1001000", "1110100"
};

// Code 39: 9 bits, narrow/wide; pattern is bar-space-bar-space... ending space between chars
struct Code39Entry {
    char ch;
    const char *pat; // 9 chars of 0/1 (narrow/wide)
};
const Code39Entry kCode39[] = {
    {'0', "000110100"}, {'1', "100100001"}, {'2', "001100001"}, {'3', "101100000"},
    {'4', "000110001"}, {'5', "100110000"}, {'6', "001110000"}, {'7', "000100101"},
    {'8', "100100100"}, {'9', "001100100"}, {'A', "100001001"}, {'B', "001001001"},
    {'C', "101001000"}, {'D', "000011001"}, {'E', "100011000"}, {'F', "001011000"},
    {'G', "000001101"}, {'H', "100001100"}, {'I', "001001100"}, {'J', "000011100"},
    {'K', "100000011"}, {'L', "001000011"}, {'M', "101000010"}, {'N', "000010011"},
    {'O', "100010010"}, {'P', "001010010"}, {'Q', "000000111"}, {'R', "100000110"},
    {'S', "001000110"}, {'T', "000010110"}, {'U', "110000001"}, {'V', "011000001"},
    {'W', "111000000"}, {'X', "010010001"}, {'Y', "110010000"}, {'Z', "011010000"},
    {'-', "010000101"}, {'.', "110000100"}, {' ', "011000100"}, {'*', "010010100"},
    {'$', "010101000"}, {'/', "010100010"}, {'+', "010001010"}, {'%', "000101010"},
};

struct CodabarEntry {
    char ch;
    const char *pat; // 7 bits narrow/wide, starts with bar
};
const CodabarEntry kCodabar[] = {
    {'0', "0000011"}, {'1', "0000110"}, {'2', "0001001"}, {'3', "1100000"},
    {'4', "0010010"}, {'5', "1000010"}, {'6', "0100001"}, {'7', "0100100"},
    {'8', "0110000"}, {'9', "1001000"}, {'-', "0001100"}, {'$', "0011000"},
    {':', "1000101"}, {'/', "1010001"}, {'.', "1010100"}, {'+', "0010101"},
    {'A', "0011010"}, {'B', "0101001"}, {'C', "0001011"}, {'D', "0001110"},
};

// Code 128 patterns: 11 modules as binary string (1=bar, 0=space). Index = code value.
const char *const kCode128[107] = {
    "11011001100", "11001101100", "11001100110", "10010011000", "10010001100", "10001001100",
    "10011001000", "10011000100", "10001100100", "11001001000", "11001000100", "11000100100",
    "10110011100", "10011011100", "10011001110", "10111001100", "10011101100", "10011100110",
    "11001110010", "11001011100", "11001001110", "11011100100", "11001110100", "11101101110",
    "11101001100", "11100101100", "11100100110", "11101100100", "11100110100", "11100110010",
    "11011011000", "11011000110", "11000110110", "10100011000", "10001011000", "10001000110",
    "10110001000", "10001101000", "10001100010", "11010001000", "11000101000", "11000100010",
    "10110111000", "10110001110", "10001101110", "10111011000", "10111000110", "10001110110",
    "11101110110", "11010001110", "11000101110", "11011101000", "11011100010", "11011101110",
    "11101011000", "11101000110", "11100010110", "11101101000", "11101100010", "11100011010",
    "11101111010", "11001000010", "11110001010", "10100110000", "10100001100", "10010110000",
    "10010000110", "10000101100", "10000100110", "10110010000", "10110000100", "10011010000",
    "10011000010", "10000110100", "10000110010", "11000010010", "11001010000", "11110111010",
    "11000010100", "10001111010", "10100111100", "10010111100", "10010011110", "10111100100",
    "10011110100", "10011110010", "11110100100", "11110010100", "11110010010", "11011011110",
    "11011110110", "11110110110", "10101111000", "10100011110", "10001011110", "10111101000",
    "10111100010", "11110101000", "11110100010", "10111011110", "10111101110", "11101011110",
    "11110101110", "11010000100", "11010010000", "11010011100", "11000111010" // STOP is 13 modules handled separately
};
const char *const kCode128Stop = "1100011101011";

char toUpperAscii(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
    return c;
}

std::string toUpperAscii(const std::string &s) {
    std::string out = s;
    for (char &c : out) c = toUpperAscii(c);
    return out;
}

const char *code39Pat(char c) {
    c = toUpperAscii(c);
    for (const auto &e : kCode39) {
        if (e.ch == c) return e.pat;
    }
    return nullptr;
}

const char *codabarPat(char c) {
    if (c >= 'a' && c <= 'd') c = static_cast<char>(c - 'a' + 'A');
    for (const auto &e : kCodabar) {
        if (e.ch == c) return e.pat;
    }
    return nullptr;
}

bool isDigits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

int eanCheckDigit(const std::string &digitsWithoutCheck) {
    int sum = 0;
    const int n = static_cast<int>(digitsWithoutCheck.size());
    for (int i = 0; i < n; i++) {
        int d = digitsWithoutCheck[static_cast<size_t>(i)] - '0';
        // from the right: odd positions weight 3
        int fromRight = n - i;
        sum += (fromRight % 2 == 1) ? d * 3 : d;
    }
    return (10 - (sum % 10)) % 10;
}

// Append narrow/wide pattern as modules into bits (1=bar). For Code39/Codabar: each bit is N or W modules.
void appendNwPattern(std::string &bits, const char *pat, bool startBar, int narrow = 1, int wide = 3) {
    bool bar = startBar;
    for (const char *p = pat; *p; p++) {
        int w = (*p == '1') ? wide : narrow;
        for (int i = 0; i < w; i++) bits += bar ? '1' : '0';
        bar = !bar;
    }
}

bool encodeCode39(const std::string &data, std::string &bits, std::string &label, std::string &error) {
    if (data.empty()) {
        error = "Empty data";
        return false;
    }
    std::string body = toUpperAscii(data);
    for (char c : body) {
        if (!code39Pat(c)) {
            error = "Invalid Code 39 character";
            return false;
        }
    }
    std::string full = "*" + body + "*";
    bits.clear();
    for (size_t i = 0; i < full.size(); i++) {
        if (i) bits += '0'; // inter-char gap (narrow space)
        appendNwPattern(bits, code39Pat(full[i]), true);
    }
    label = full;
    return true;
}

bool isCodabarStopStart(char c) {
    return c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'a' || c == 'b' || c == 'c' || c == 'd';
}

bool encodeCodabar(const std::string &data, std::string &bits, std::string &label, std::string &error) {
    if (data.size() < 3) {
        error = "Codabar needs start, data, stop";
        return false;
    }
    const char start = data.front();
    const char stop = data.back();
    if (!codabarPat(start) || !codabarPat(stop) || !isCodabarStopStart(start) || !isCodabarStopStart(stop)) {
        error = "Codabar must start/end with A-D";
        return false;
    }
    for (char c : data) {
        if (!codabarPat(c)) {
            error = "Invalid Codabar character";
            return false;
        }
    }
    bits.clear();
    for (size_t i = 0; i < data.size(); i++) {
        if (i) bits += '0';
        appendNwPattern(bits, codabarPat(data[i]), true);
    }
    label = toUpperAscii(data);
    return true;
}

bool encodeEanFamily(BarcodeType type, const std::string &raw, std::string &bits, std::string &label,
                     std::string &error) {
    const int need = (type == BarcodeType::Ean13) ? 12 : (type == BarcodeType::Ean8) ? 7 : 11; // without check
    std::string digits = raw;
    if (!isDigits(digits)) {
        error = "Digits only";
        return false;
    }
    const int len = static_cast<int>(digits.size());
    if (len == need + 1) {
        // user supplied check digit - verify
        int chk = eanCheckDigit(digits.substr(0, static_cast<size_t>(need)));
        if (chk != digits[static_cast<size_t>(need)] - '0') {
            error = "Bad check digit";
            return false;
        }
    } else if (len == need) {
        digits += static_cast<char>('0' + eanCheckDigit(digits));
    } else {
        error = "Need " + std::to_string(need) + " or " + std::to_string(need + 1) + " digits";
        return false;
    }

    bits = "101"; // start guard
    if (type == BarcodeType::Ean13) {
        int first = digits[0] - '0';
        const char *structr = EAN_13_STRUCTURE[first];
        for (int i = 0; i < 6; i++) {
            int d = digits[static_cast<size_t>(i + 1)] - '0';
            bits += (structr[i] == 'G') ? EAN_G[d] : UPC_EAN_L[d];
        }
        bits += "01010";
        for (int i = 7; i < 13; i++) bits += UPC_EAN_R[digits[static_cast<size_t>(i)] - '0'];
    } else if (type == BarcodeType::Ean8) {
        for (int i = 0; i < 4; i++) bits += UPC_EAN_L[digits[static_cast<size_t>(i)] - '0'];
        bits += "01010";
        for (int i = 4; i < 8; i++) bits += UPC_EAN_R[digits[static_cast<size_t>(i)] - '0'];
    } else { // UPC-A
        for (int i = 0; i < 6; i++) bits += UPC_EAN_L[digits[static_cast<size_t>(i)] - '0'];
        bits += "01010";
        for (int i = 6; i < 12; i++) bits += UPC_EAN_R[digits[static_cast<size_t>(i)] - '0'];
    }
    bits += "101";
    label = digits;
    return true;
}

bool encodeCode128(const std::string &data, std::string &bits, std::string &label, std::string &error) {
    if (data.empty()) {
        error = "Empty data";
        return false;
    }
    const bool useC = (data.size() % 2 == 0) && isDigits(data);
    std::vector<int> codes;
    if (useC) {
        codes.push_back(105); // Start C
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            codes.push_back((data[i] - '0') * 10 + (data[i + 1] - '0'));
        }
    } else {
        codes.push_back(104); // Start B
        for (size_t i = 0; i < data.size(); i++) {
            int c = static_cast<uint8_t>(data[i]);
            if (c < 32 || c > 126) {
                error = "Code 128 supports ASCII 32-126";
                return false;
            }
            codes.push_back(c - 32);
        }
    }
    int checksum = codes[0];
    for (size_t i = 1; i < codes.size(); i++) checksum += static_cast<int>(i) * codes[i];
    checksum %= 103;
    codes.push_back(checksum);

    bits.clear();
    for (int code : codes) {
        if (code < 0 || code > 105) {
            error = "Invalid Code 128 value";
            return false;
        }
        bits += kCode128[code];
    }
    bits += kCode128Stop;
    label = data;
    return true;
}

} // namespace

const char* type_label(BarcodeType type) {
    switch (type) {
    case BarcodeType::Code128: return "Code 128";
    case BarcodeType::Code39: return "Code 39";
    case BarcodeType::Codabar: return "Codabar";
    case BarcodeType::Ean13: return "EAN-13";
    case BarcodeType::Ean8: return "EAN-8";
    case BarcodeType::UpcA: return "UPC-A";
    }
    return "Unknown";
}

bool encode(BarcodeType type,
            const std::string& data,
            std::string& bits,
            std::string& label,
            std::string& error) {
    bits.clear();
    label.clear();
    error.clear();

    bool ok = false;
    switch (type) {
    case BarcodeType::Code128: ok = encodeCode128(data, bits, label, error); break;
    case BarcodeType::Code39: ok = encodeCode39(data, bits, label, error); break;
    case BarcodeType::Codabar: ok = encodeCodabar(data, bits, label, error); break;
    case BarcodeType::Ean13:
    case BarcodeType::Ean8:
    case BarcodeType::UpcA: ok = encodeEanFamily(type, data, bits, label, error); break;
    }
    if (!ok) {
        bits.clear();
        label.clear();
        if (error.empty()) error = "Invalid data";
    }
    return ok;
}

} // namespace tools::barcode


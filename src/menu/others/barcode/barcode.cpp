#include "barcode.h"

#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"

#include <Arduino.h>
#include <globals.h>
#include <vector>

#if defined(HAS_KEYBOARD)

namespace {

enum BarcodeType { BT_CODE128, BT_CODE39, BT_CODABAR, BT_EAN13, BT_EAN8, BT_UPCA };

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
const char *kCode128Stop = "1100011101011";

const char *code39Pat(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    for (const auto &e : kCode39) {
        if (e.ch == c) return e.pat;
    }
    return nullptr;
}

const char *codabarPat(char c) {
    if (c >= 'a' && c <= 'd') c = c - 'a' + 'A';
    for (const auto &e : kCodabar) {
        if (e.ch == c) return e.pat;
    }
    return nullptr;
}

bool isDigits(const String &s) {
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return s.length() > 0;
}

int eanCheckDigit(const String &digitsWithoutCheck) {
    int sum = 0;
    int n = digitsWithoutCheck.length();
    for (int i = 0; i < n; i++) {
        int d = digitsWithoutCheck[i] - '0';
        // from the right: odd positions weight 3
        int fromRight = n - i;
        sum += (fromRight % 2 == 1) ? d * 3 : d;
    }
    return (10 - (sum % 10)) % 10;
}

// Append narrow/wide pattern as modules into bits (1=bar). For Code39/Codabar: each bit is N or W modules.
void appendNwPattern(String &bits, const char *pat, bool startBar, int narrow = 1, int wide = 3) {
    bool bar = startBar;
    for (const char *p = pat; *p; p++) {
        int w = (*p == '1') ? wide : narrow;
        for (int i = 0; i < w; i++) bits += bar ? '1' : '0';
        bar = !bar;
    }
}

bool encodeCode39(const String &data, String &bits, String &label) {
    String body = data;
    body.toUpperCase();
    for (size_t i = 0; i < body.length(); i++) {
        if (!code39Pat(body[i])) return false;
    }
    String full = "*" + body + "*";
    bits = "";
    for (size_t i = 0; i < full.length(); i++) {
        if (i) bits += '0'; // inter-char gap (narrow space)
        appendNwPattern(bits, code39Pat(full[i]), true);
    }
    label = full;
    return true;
}

bool encodeCodabar(const String &data, String &bits, String &label) {
    if (data.length() < 3) return false;
    char start = data[0];
    char stop = data[data.length() - 1];
    if (!codabarPat(start) || !codabarPat(stop)) return false;
    if (strchr("ABCDabcd", start) == nullptr || strchr("ABCDabcd", stop) == nullptr) return false;
    for (size_t i = 0; i < data.length(); i++) {
        if (!codabarPat(data[i])) return false;
    }
    bits = "";
    for (size_t i = 0; i < data.length(); i++) {
        if (i) bits += '0';
        appendNwPattern(bits, codabarPat(data[i]), true);
    }
    label = data;
    label.toUpperCase();
    return true;
}

bool encodeEanFamily(BarcodeType type, const String &raw, String &bits, String &label) {
    int need = (type == BT_EAN13) ? 12 : (type == BT_EAN8) ? 7 : 11; // without check
    String digits = raw;
    if (!isDigits(digits)) return false;
    if ((int)digits.length() == need + 1) {
        // user supplied check digit — verify
        int chk = eanCheckDigit(digits.substring(0, need));
        if (chk != digits[need] - '0') return false;
    } else if ((int)digits.length() == need) {
        digits += char('0' + eanCheckDigit(digits));
    } else {
        return false;
    }

    bits = "101"; // start guard
    if (type == BT_EAN13) {
        int first = digits[0] - '0';
        const char *structr = EAN_13_STRUCTURE[first];
        for (int i = 0; i < 6; i++) {
            int d = digits[i + 1] - '0';
            bits += (structr[i] == 'G') ? EAN_G[d] : UPC_EAN_L[d];
        }
        bits += "01010";
        for (int i = 7; i < 13; i++) bits += UPC_EAN_R[digits[i] - '0'];
    } else if (type == BT_EAN8) {
        for (int i = 0; i < 4; i++) bits += UPC_EAN_L[digits[i] - '0'];
        bits += "01010";
        for (int i = 4; i < 8; i++) bits += UPC_EAN_R[digits[i] - '0'];
    } else { // UPCA
        for (int i = 0; i < 6; i++) bits += UPC_EAN_L[digits[i] - '0'];
        bits += "01010";
        for (int i = 6; i < 12; i++) bits += UPC_EAN_R[digits[i] - '0'];
    }
    bits += "101";
    label = digits;
    return true;
}

bool encodeCode128(const String &data, String &bits, String &label) {
    if (data.length() == 0) return false;
    bool useC = (data.length() % 2 == 0) && isDigits(data);
    std::vector<int> codes;
    if (useC) {
        codes.push_back(105); // Start C
        for (size_t i = 0; i + 1 < data.length(); i += 2) {
            codes.push_back((data[i] - '0') * 10 + (data[i + 1] - '0'));
        }
    } else {
        codes.push_back(104); // Start B
        for (size_t i = 0; i < data.length(); i++) {
            int c = (uint8_t)data[i];
            if (c < 32 || c > 126) return false;
            codes.push_back(c - 32);
        }
    }
    int checksum = codes[0];
    for (size_t i = 1; i < codes.size(); i++) checksum += (int)i * codes[i];
    checksum %= 103;
    codes.push_back(checksum);

    bits = "";
    for (int code : codes) {
        if (code < 0 || code > 105) return false;
        bits += kCode128[code];
    }
    bits += kCode128Stop;
    label = data;
    return true;
}

bool encode(BarcodeType type, const String &data, String &bits, String &label) {
    switch (type) {
        case BT_CODE128: return encodeCode128(data, bits, label);
        case BT_CODE39: return encodeCode39(data, bits, label);
        case BT_CODABAR: return encodeCodabar(data, bits, label);
        case BT_EAN13:
        case BT_EAN8:
        case BT_UPCA: return encodeEanFamily(type, data, bits, label);
    }
    return false;
}

void drawBarcodeFrame(const String &bits, const String &label, bool inverted) {
    const int n = bits.length();
    // Use full width (distribute remainder across modules) and maximize bar height.
    const int labelH = uiLineH(FP) + 2;
    const int y0 = 2;
    const int labelY = tftHeight - labelH;
    const int barH = labelY - y0 - 1;
    uint16_t bg = inverted ? TFT_BLACK : TFT_WHITE;
    uint16_t fg = inverted ? TFT_WHITE : TFT_BLACK;

    tft.fillScreen(bg);
    for (int i = 0; i < n; i++) {
        if (bits[i] != '1') continue;
        int x0 = (int)((int64_t)i * tftWidth / n);
        int x1 = (int)((int64_t)(i + 1) * tftWidth / n);
        int w = x1 - x0;
        if (w < 1) w = 1;
        tft.fillRect(x0, y0, w, barH, fg);
    }
    tft.setTextColor(fg, bg);
    tft.setTextSize(FP);
    tft.drawCentreString(label, tftWidth / 2, labelY, 1);
}

void displayBarcode(const String &bits, const String &label) {
#ifdef HAS_SCREEN
    const int n = bits.length();
    if (n <= 0) return;
    if (n > tftWidth) {
        displayError("Too wide for screen", true);
        return;
    }

    bool inverted = true; // default black bg / white barcode
    drawBarcodeFrame(bits, label, inverted);
    delay(200);
    SelPress = false;
    EscPress = false;
    KeyStroke.Clear();

    while (!returnToMenu && !forceHome) {
        if (check(EscPress) || check(SelPress)) break;
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            for (char c : key.word) {
                if (c == ' ') {
                    inverted = !inverted;
                    drawBarcodeFrame(bits, label, inverted);
                    break;
                }
            }
            KeyStroke.Clear();
        }
        delay(40);
    }
    tft.fillScreen(kvxConfig.bgColor);
#endif
}

void runType(BarcodeType type, const char *title, bool numeric) {
    String prompt = String(title) + ":";
    String data = numeric ? num_keyboard("", 32, prompt.c_str()) : keyboard("", 48, prompt.c_str());
    if (data == "\x1B" || data.length() == 0) return;
    String bits, label;
    if (!encode(type, data, bits, label)) {
        displayError("Invalid data", true);
        return;
    }
    displayBarcode(bits, label);
}

} // namespace

void barcodeMenu() {
    while (!returnToMenu && !forceHome) {
        std::vector<Option> opts = {
            {"Code 128", []() { runType(BT_CODE128, "Code 128", false); }},
            {"Code 39", []() { runType(BT_CODE39, "Code 39", false); }},
            {"Codabar", []() { runType(BT_CODABAR, "Codabar A..A", false); }},
            {"EAN-13", []() { runType(BT_EAN13, "EAN-13", true); }},
            {"EAN-8", []() { runType(BT_EAN8, "EAN-8", true); }},
            {"UPC-A", []() { runType(BT_UPCA, "UPC-A", true); }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Barcode");
        if (sel < 0 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}

#else

void barcodeMenu() {}

#endif

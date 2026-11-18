// Pure C++17 barcode encoder (ported from Cardputer barcode.cpp).
#pragma once

#include <string>

namespace tools::barcode {

enum class BarcodeType {
    Code128,
    Code39,
    Codabar,
    Ean13,
    Ean8,
    UpcA,
};

const char* type_label(BarcodeType type);

// bits: '0'/'1' modules (1 = bar). label: human-readable caption.
bool encode(BarcodeType type,
            const std::string& data,
            std::string& bits,
            std::string& label,
            std::string& error);

} // namespace tools::barcode


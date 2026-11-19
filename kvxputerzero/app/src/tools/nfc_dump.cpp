#include "nfc_dump.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace tools {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string after_colon(const std::string& line) {
    const auto c = line.find(':');
    if (c == std::string::npos) return {};
    return trim(line.substr(c + 1));
}

} // namespace

std::string nfc_normalize_hex(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string nfc_uid_spaced(const std::string& uid_hex) {
    const std::string h = nfc_normalize_hex(uid_hex);
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        if (!out.empty()) out.push_back(' ');
        out += h.substr(i, 2);
    }
    return out;
}

std::string nfc_dump_summary(const NfcDump& d) {
    std::ostringstream oss;
    oss << "Type: " << (d.device_type.empty() ? "?" : d.device_type) << "\n";
    oss << "UID: " << nfc_uid_spaced(d.uid_hex) << "\n";
    if (!d.atqa_hex.empty()) oss << "ATQA: " << d.atqa_hex << "\n";
    if (!d.sak_hex.empty()) oss << "SAK: " << d.sak_hex << "\n";
    if (d.pages_total > 0 || d.pages_read > 0)
        oss << "Pages: " << d.pages_read << "/" << d.pages_total
            << (d.page_read_ok ? "" : " (partial)") << "\n";
    if (!d.pages_text.empty()) {
        oss << "\n" << d.pages_text;
        if (!d.pages_text.empty() && d.pages_text.back() != '\n') oss << "\n";
    }
    if (!d.ndef_text.empty()) oss << "\n" << d.ndef_text << "\n";
    return oss.str();
}

bool nfc_load_dump_file(const std::string& path, NfcDump& out, std::string& error) {
    out = {};
    std::ifstream in(path);
    if (!in) {
        error = "open failed";
        return false;
    }
    std::string line;
    std::ostringstream pages;
    bool in_pages = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string low = line;
        if (line.rfind("Device type:", 0) == 0) out.device_type = after_colon(line);
        else if (line.rfind("UID:", 0) == 0) out.uid_hex = nfc_normalize_hex(after_colon(line));
        else if (line.rfind("ATQA:", 0) == 0) out.atqa_hex = after_colon(line);
        else if (line.rfind("SAK:", 0) == 0) out.sak_hex = after_colon(line);
        else if (line.rfind("Pages total:", 0) == 0) out.pages_total = std::atoi(after_colon(line).c_str());
        else if (line.rfind("Pages read:", 0) == 0) {
            out.pages_read = std::atoi(after_colon(line).c_str());
            out.page_read_ok = false;
        } else if (line.rfind("Page ", 0) == 0 || line.rfind("Block ", 0) == 0) {
            in_pages = true;
            pages << line << "\n";
            if (out.pages_read == 0) ++out.pages_read; // count if header missing
        } else if (in_pages && !line.empty() && line[0] != '#' && line.rfind("Failed", 0) != 0) {
            pages << line << "\n";
        } else if (line.rfind("Filetype: kvxputerzero", 0) == 0) {
            // ignore
        } else if (line.rfind("Signature:", 0) == 0 || line.rfind("Mifare version:", 0) == 0 ||
                   line.rfind("Counter ", 0) == 0) {
            out.raw_extra += line + "\n";
        }
    }
    out.pages_text = pages.str();
    if (out.pages_total == 0 && out.pages_read > 0) out.pages_total = out.pages_read;
    if (out.uid_hex.empty() && out.pages_text.empty()) {
        error = "no UID or pages in file";
        return false;
    }
    if (out.device_type.empty()) out.device_type = "ISO14443-3A";
    return true;
}

bool nfc_save_bruce(const std::string& path, const NfcDump& dump, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: Bruce RFID File\n";
    out << "Version 1\n";
    out << "Device type: " << (dump.device_type.empty() ? "ISO14443-3A" : dump.device_type) << "\n";
    out << "# UID, ATQA and SAK are common for all formats\n";
    out << "UID: " << nfc_uid_spaced(dump.uid_hex) << "\n";
    if (!dump.sak_hex.empty()) out << "SAK: " << dump.sak_hex << "\n";
    if (!dump.atqa_hex.empty()) out << "ATQA: " << dump.atqa_hex << "\n";
    if (!dump.raw_extra.empty()) out << dump.raw_extra;
    out << "# Memory dump\n";
    out << "Pages total: " << (dump.pages_total > 0 ? dump.pages_total : dump.pages_read) << "\n";
    if (!dump.page_read_ok) out << "Pages read: " << dump.pages_read << "\n";
    out << dump.pages_text;
    if (!dump.pages_text.empty() && dump.pages_text.back() != '\n') out << "\n";
    return true;
}

bool nfc_save_flipper(const std::string& path, const NfcDump& dump, std::string& error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: Flipper NFC device\n";
    out << "Version: 4\n";
    out << "Device type: " << (dump.device_type.empty() ? "ISO14443-3A" : dump.device_type) << "\n";
    out << "UID: " << nfc_uid_spaced(dump.uid_hex) << "\n";
    if (!dump.atqa_hex.empty()) out << "ATQA: " << dump.atqa_hex << "\n";
    if (!dump.sak_hex.empty()) out << "SAK: " << dump.sak_hex << "\n";
    out << "Data format version: 2\n";
    if (!dump.raw_extra.empty()) out << dump.raw_extra;
    out << "Pages total: " << (dump.pages_total > 0 ? dump.pages_total : dump.pages_read) << "\n";
    out << "Pages read: " << dump.pages_read << "\n";
    out << dump.pages_text;
    if (!dump.pages_text.empty() && dump.pages_text.back() != '\n') out << "\n";
    out << "Failed authentication attempts: 0\n";
    return true;
}

NfcDump nfc_synthetic_ntag(int total_pages, const std::string& uid_hex,
                           const std::vector<uint8_t>& ndef_tlv) {
    NfcDump d;
    d.device_type = (total_pages >= 231)   ? "NTAG216"
                   : (total_pages >= 135) ? "NTAG215"
                   : (total_pages >= 45)  ? "NTAG213"
                                          : "NTAG203";
    d.uid_hex = nfc_normalize_hex(uid_hex);
    d.atqa_hex = "44 00";
    d.sak_hex = "00";
    d.pages_total = total_pages;
    d.pages_read = total_pages;
    d.page_read_ok = true;
    std::vector<uint8_t> mem(static_cast<size_t>(total_pages) * 4, 0);
    // UID into pages 0-2 (simplified 7-byte layout when enough hex)
    const std::string u = d.uid_hex;
    for (size_t i = 0; i + 1 < u.size() && i / 2 < 7; i += 2) {
        mem[i / 2] = static_cast<uint8_t>(std::strtoul(u.substr(i, 2).c_str(), nullptr, 16));
    }
    mem[10] = 0xE1;
    mem[11] = 0x10;
    // CC size hint
    if (total_pages >= 231) mem[13] = 0x6D;
    else if (total_pages >= 135) mem[13] = 0x3E;
    else if (total_pages >= 45) mem[13] = 0x12;
    else mem[13] = 0x12;
    size_t off = 16; // page 4
    for (uint8_t b : ndef_tlv) {
        if (off >= mem.size()) break;
        mem[off++] = b;
    }
    std::ostringstream pages;
    for (int p = 0; p < total_pages; ++p) {
        char line[64];
        std::snprintf(line, sizeof(line), "Page %d: %02X %02X %02X %02X\n", p, mem[p * 4],
                      mem[p * 4 + 1], mem[p * 4 + 2], mem[p * 4 + 3]);
        pages << line;
    }
    d.pages_text = pages.str();
    return d;
}

} // namespace tools

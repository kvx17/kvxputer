#ifndef LITE_VERSION
#include "codec.h"

#include "root/app/utils.h"
#include "root/input/mykeyboard.h"
// showAppHelp via utils.h
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"

#include <MD5Builder.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <globals.h>
#include <memory>
#include <vector>

namespace {

String toHex(const uint8_t *data, size_t len) {
    String s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        s += buf;
    }
    return s;
}

String fromHex(const String &hex, bool &ok) {
    ok = true;
    String out;
    String h = hex;
    h.replace(" ", "");
    if (h.length() % 2) {
        ok = false;
        return "";
    }
    for (size_t i = 0; i + 1 < h.length(); i += 2) {
        char c0 = h[i], c1 = h[i + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int a = nibble(c0), b = nibble(c1);
        if (a < 0 || b < 0) {
            ok = false;
            return "";
        }
        out += (char)((a << 4) | b);
    }
    return out;
}

String urlEncode(const String &in) {
    String out;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            out += c;
        else {
            out += '%';
            out += hex[(c >> 4) & 0xf];
            out += hex[c & 0xf];
        }
    }
    return out;
}

String urlDecode(const String &in) {
    String out;
    for (size_t i = 0; i < in.length(); i++) {
        if (in[i] == '%' && i + 2 < in.length()) {
            char buf[3] = {in[i + 1], in[i + 2], 0};
            out += (char)strtol(buf, nullptr, 16);
            i += 2;
        } else if (in[i] == '+') out += ' ';
        else out += in[i];
    }
    return out;
}

String b64Encode(const String &in) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, (const uint8_t *)in.c_str(), in.length());
    std::unique_ptr<uint8_t[]> buf(new uint8_t[olen + 1]);
    if (mbedtls_base64_encode(buf.get(), olen, &olen, (const uint8_t *)in.c_str(), in.length()) != 0)
        return "";
    buf[olen] = 0;
    return String((char *)buf.get());
}

String b64Decode(const String &in) {
    size_t olen = 0;
    mbedtls_base64_decode(nullptr, 0, &olen, (const uint8_t *)in.c_str(), in.length());
    std::unique_ptr<uint8_t[]> buf(new uint8_t[olen + 1]);
    if (mbedtls_base64_decode(buf.get(), olen, &olen, (const uint8_t *)in.c_str(), in.length()) != 0)
        return "";
    buf[olen] = 0;
    return String((char *)buf.get(), olen);
}

String hashInput(mbedtls_md_type_t type, const uint8_t *data, size_t len) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
    if (!info) return "";
    uint8_t out[32];
    size_t sz = mbedtls_md_get_size(info);
    if (mbedtls_md(info, data, len, out) != 0) return "";
    return toHex(out, sz);
}

String md5Of(const uint8_t *data, size_t len) {
    MD5Builder md5;
    md5.begin();
    md5.add(data, len);
    md5.calculate();
    return md5.toString();
}

void showResult(const String &title, const String &body) {
    ScrollableTextArea area(title);
    int start = 0;
    while (start < (int)body.length()) {
        int end = min(start + 40, (int)body.length());
        area.addLine(body.substring(start, end));
        start = end;
    }
    if (body.isEmpty()) area.addLine("(empty)");
    area.show(true);
}

String askTextOrFile(bool &fromFile, std::vector<uint8_t> &fileBytes) {
    fromFile = false;
    fileBytes.clear();
    String mode = "text";
    options = {
        {"Typed text", [&]() { mode = "text"; }},
        {"SD file",    [&]() { mode = "file"; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Input");
    options.clear();
    if (mode == "file") {
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs) {
            displayError("No storage", true);
            return "";
        }
        String path = loopSD(*fs, true, "*", "/");
        if (!path.length()) return "";
        File f = fs->open(path, FILE_READ);
        if (!f) return "";
        size_t n = f.size();
        if (n > 64 * 1024) n = 64 * 1024;
        fileBytes.resize(n);
        f.read(fileBytes.data(), n);
        f.close();
        fromFile = true;
        return path;
    }
    return keyboard("", 200, "Input");
}

void runEncode(const char *kind) {
    bool fromFile = false;
    std::vector<uint8_t> bytes;
    String text = askTextOrFile(fromFile, bytes);
    if (text == "\x1B") return;
    String in = fromFile ? String((char *)bytes.data(), bytes.size()) : text;
    String out;
    if (strcmp(kind, "b64e") == 0) out = b64Encode(in);
    else if (strcmp(kind, "b64d") == 0) out = b64Decode(in);
    else if (strcmp(kind, "hexe") == 0) {
        out = toHex((const uint8_t *)in.c_str(), in.length());
    } else if (strcmp(kind, "hexd") == 0) {
        bool ok = false;
        out = fromHex(in, ok);
        if (!ok) {
            displayError("Bad hex", true);
            return;
        }
    } else if (strcmp(kind, "urle") == 0) out = urlEncode(in);
    else if (strcmp(kind, "urld") == 0) out = urlDecode(in);
    showResult(kind, out);
}

void runHash(mbedtls_md_type_t type, bool useMd5) {
    bool fromFile = false;
    std::vector<uint8_t> bytes;
    String text = askTextOrFile(fromFile, bytes);
    if (text == "\x1B" || (text.isEmpty() && !fromFile)) return;
    const uint8_t *data = fromFile ? bytes.data() : (const uint8_t *)text.c_str();
    size_t len = fromFile ? bytes.size() : text.length();
    String dig = useMd5 ? md5Of(data, len) : hashInput(type, data, len);
    showResult(useMd5 ? "MD5" : "Hash", dig);
}

} // namespace

void codecHashHelp() {
    showAppHelp(
        {
            "Encode, decode, and hash",
            "typed text or an SD file.",
            "",
            "Each tool asks Input:",
            "  Typed text — keyboard.",
            "  SD file — pick a file",
            "  (hashed up to 64 KB).",
            "",
            "Base64 / Hex / URL:",
            "  Encode or decode and show",
            "  the result (scrollable).",
            "",
            "MD5 / SHA1 / SHA256:",
            "  Digest as lowercase hex.",
            "",
            "Esc/Sel closes the result.",
        }
    );
}

void codecHashMenu() {
    options = {
        {"Help",          codecHashHelp},
        {"Base64 encode", []() { runEncode("b64e"); }},
        {"Base64 decode", []() { runEncode("b64d"); }},
        {"Hex encode",    []() { runEncode("hexe"); }},
        {"Hex decode",    []() { runEncode("hexd"); }},
        {"URL encode",    []() { runEncode("urle"); }},
        {"URL decode",    []() { runEncode("urld"); }},
        {"MD5",           []() { runHash(MBEDTLS_MD_MD5, true); }},
        {"SHA1",          []() { runHash(MBEDTLS_MD_SHA1, false); }},
        {"SHA256",        []() { runHash(MBEDTLS_MD_SHA256, false); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Codec / Hash");
    options.clear();
}

#endif

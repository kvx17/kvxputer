#include <Arduino.h>
#include <globals.h>

#if defined(HAS_KEYBOARD) && !defined(LITE_VERSION)
#include "totp.h"

#include "root/app/utils.h"
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <globals.h>
#include <time.h>
#include <vector>

namespace {

struct TotpAccount {
    String name;
    String secretB32;
};

std::vector<TotpAccount> g_accounts;

int b32Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

bool base32Decode(const String &in, std::vector<uint8_t> &out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '=' || c == ' ' || c == '-') continue;
        int v = b32Val(c);
        if (v < 0) return false;
        buffer = (buffer << 5) | (uint32_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buffer >> bits) & 0xff));
        }
    }
    return !out.empty();
}

uint32_t hotp(const uint8_t *key, size_t keyLen, uint64_t counter) {
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) {
        msg[i] = counter & 0xff;
        counter >>= 8;
    }
    uint8_t hash[20];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_hmac(info, key, keyLen, msg, 8, hash);
    int offset = hash[19] & 0x0f;
    uint32_t bin = ((hash[offset] & 0x7f) << 24) | ((hash[offset + 1] & 0xff) << 16) |
                   ((hash[offset + 2] & 0xff) << 8) | (hash[offset + 3] & 0xff);
    return bin % 1000000;
}

String formatCode(uint32_t code) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)code);
    return String(buf);
}

String accountsPath() { return String(kvx::paths::TOTP_DIR) + "/accounts.json"; }

// Lightweight device-bound obfuscation (not strong crypto).
String obfuscate(const String &in) {
    uint64_t mac = ESP.getEfuseMac();
    uint8_t key[8];
    for (int i = 0; i < 8; i++) key[i] = (uint8_t)((mac >> (8 * i)) & 0xff);
    String out = in;
    for (size_t i = 0; i < out.length(); i++) out[i] = out[i] ^ key[i % 8] ^ (uint8_t)(i & 0xff);
    // store as hex so binary-safe
    String hex;
    hex.reserve(out.length() * 2);
    for (size_t i = 0; i < out.length(); i++) {
        char b[3];
        snprintf(b, sizeof(b), "%02x", (uint8_t)out[i]);
        hex += b;
    }
    return hex;
}

String deobfuscate(const String &hex) {
    String bin;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        bin += (char)strtol(b, nullptr, 16);
    }
    uint64_t mac = ESP.getEfuseMac();
    uint8_t key[8];
    for (int i = 0; i < 8; i++) key[i] = (uint8_t)((mac >> (8 * i)) & 0xff);
    for (size_t i = 0; i < bin.length(); i++) bin[i] = bin[i] ^ key[i % 8] ^ (uint8_t)(i & 0xff);
    return bin;
}

bool loadAccounts() {
    g_accounts.clear();
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return false;
    kvx::paths::ensureDir(*fs, kvx::paths::TOTP_DIR);
    if (!fs->exists(accountsPath())) return true;
    File f = fs->open(accountsPath(), FILE_READ);
    if (!f) return false;
    String raw = f.readString();
    f.close();
    raw.trim();
    String plain = raw.startsWith("[") ? raw : deobfuscate(raw);
    JsonDocument doc;
    if (deserializeJson(doc, plain)) return false;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        TotpAccount a;
        a.name = o["name"] | "";
        a.secretB32 = o["secret"] | "";
        if (a.name.length() && a.secretB32.length()) g_accounts.push_back(a);
    }
    return true;
}

bool saveAccounts() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return false;
    }
    kvx::paths::ensureDir(*fs, kvx::paths::TOTP_DIR);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto &a : g_accounts) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = a.name;
        o["secret"] = a.secretB32;
    }
    String plain;
    serializeJson(doc, plain);
    File f = fs->open(accountsPath(), FILE_WRITE);
    if (!f) return false;
    f.print(obfuscate(plain));
    f.close();
    return true;
}

time_t nowEpoch() {
    time_t now = time(nullptr);
    if (now < 100000) {
        // RTC / NTP may be unset — use millis-based fake epoch from compile? Prefer ESP32Time if available.
        now = (time_t)(millis() / 1000 + 1700000000UL);
    }
    return now;
}

void showCodes() {
    if (g_accounts.empty()) {
        displayInfo("No accounts", true);
        return;
    }
    EscPress = false;
    size_t idx = 0;
    size_t paintedIdx = SIZE_MAX;
    String paintedCode;
    int paintedRemain = -1;
    bool chromeDrawn = false;

    auto paint = [&](size_t i, const String &code, int remain) {
        if (!chromeDrawn) {
            drawMainBorderWithTitle("TOTP");
            chromeDrawn = true;
        } else {
            tft.fillRect(6, 26, tftWidth - 12, tftHeight - 32, kvxConfig.bgColor);
        }
        tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y + FM * LH);
        tft.setTextSize(FP);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        padprintln(g_accounts[i].name);
        tft.setTextSize(FG);
        padprintln(code);
        tft.setTextSize(FP);
        padprintln(String(remain) + "s  " + String(i + 1) + "/" + String(g_accounts.size()));
        padprintln(",/. next  ESC exit");
        padprintln("Device-bound store");
        paintedIdx = i;
        paintedCode = code;
        paintedRemain = remain;
    };

    while (!check(EscPress) && !returnToMenu) {
        time_t now = nowEpoch();
        uint64_t step = (uint64_t)now / 30ULL;
        int remain = 30 - (int)(now % 30);
        auto &a = g_accounts[idx];
        std::vector<uint8_t> key;
        String code = "??????";
        if (base32Decode(a.secretB32, key)) code = formatCode(hotp(key.data(), key.size(), step));

        if (idx != paintedIdx || code != paintedCode || remain != paintedRemain) {
            paint(idx, code, remain);
        }

        uint32_t t0 = millis();
        while (millis() - t0 < 250) {
            if (check(EscPress) || returnToMenu) return;
            if (check(NextPress) || check(PrevPress)) {
                if (check(NextPress)) idx = (idx + 1) % g_accounts.size();
                else idx = (idx + g_accounts.size() - 1) % g_accounts.size();
                break;
            }
            delay(20);
        }
    }
}

void addAccount() {
    String name = keyboard("", 24, "Account name");
    if (name.isEmpty() || name == "\x1B") return;
    String secret = keyboard("", 64, "Base32 secret");
    secret.trim();
    secret.replace(" ", "");
    if (secret.isEmpty() || secret == "\x1B") return;
    std::vector<uint8_t> key;
    if (!base32Decode(secret, key)) {
        displayError("Bad Base32", true);
        return;
    }
    g_accounts.push_back({name, secret});
    if (saveAccounts()) displayInfo("Saved", true);
}

void deleteAccount() {
    if (g_accounts.empty()) {
        displayInfo("Empty", true);
        return;
    }
    options.clear();
    for (size_t i = 0; i < g_accounts.size(); i++) {
        size_t idx = i;
        options.push_back({g_accounts[i].name.c_str(), [idx]() {
                               g_accounts.erase(g_accounts.begin() + idx);
                               saveAccounts();
                           }});
    }
    loopOptions(options, MENU_TYPE_SUBMENU, "Delete");
    options.clear();
}

} // namespace

void totpHelp() {
    showAppHelp(
        {
            "Authenticator (RFC 6238).",
            "6-digit codes, 30s step.",
            "",
            "Add account:",
            "  Name + Base32 secret",
            "  (from authenticator setup).",
            "",
            "Show codes:",
            "  ,/.  next/prev account",
            "  Countdown until refresh",
            "  ESC exit",
            "",
            "Storage:",
            "  /kvxputer/totp/accounts.json",
            "  Obfuscated with device MAC",
            "  — not strong encryption.",
            "  Do not rely on this alone",
            "  for high-value accounts.",
            "",
            "Needs correct device time",
            "  (NTP / clock) for matches.",
        }
    );
}

void totpMenu() {
    loadAccounts();
    options = {
        {"Help",        totpHelp     },
        {"Show codes",  showCodes    },
        {"Add account", addAccount   },
        {"Delete",      deleteAccount},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "TOTP");
    options.clear();
}

#endif

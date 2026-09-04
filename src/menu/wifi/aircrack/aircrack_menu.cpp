/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Wordlist vs handshake PCAP: PMK + PTK + EAPOL MIC verify.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "aircrack.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include <globals.h>
#include <vector>

namespace {

bool pmkCompute(const char *ssid, const char *pass, uint8_t pmk[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(
               MBEDTLS_MD_SHA1, (const unsigned char *)pass, strlen(pass), (const unsigned char *)ssid,
               strlen(ssid), 4096, 32, pmk
           ) == 0;
}

bool hmacSha1(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[20]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!info) return false;
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, klen);
    mbedtls_md_hmac_update(&ctx, msg, mlen);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
    return true;
}

void prf512(const uint8_t pmk[32], const uint8_t *data, size_t dlen, uint8_t ptk[64]) {
    const char *label = "Pairwise key expansion";
    uint8_t i = 0, pos = 0;
    while (pos < 64) {
        uint8_t block[20];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
        mbedtls_md_hmac_starts(&ctx, pmk, 32);
        mbedtls_md_hmac_update(&ctx, (const uint8_t *)label, strlen(label) + 1);
        mbedtls_md_hmac_update(&ctx, data, dlen);
        mbedtls_md_hmac_update(&ctx, &i, 1);
        mbedtls_md_hmac_finish(&ctx, block);
        mbedtls_md_free(&ctx);
        size_t cpy = (64 - pos) < 20 ? (64 - pos) : 20;
        memcpy(ptk + pos, block, cpy);
        pos += cpy;
        i++;
    }
}

struct Hs {
    uint8_t ap[6]{};
    uint8_t sta[6]{};
    uint8_t anonce[32]{};
    uint8_t snonce[32]{};
    uint8_t mic[16]{};
    std::vector<uint8_t> eapol;
    bool haveA = false, haveS = false, haveMic = false;
};

int find888e(const uint8_t *b, int n) {
    for (int i = 0; i + 1 < n; i++)
        if (b[i] == 0x88 && b[i + 1] == 0x8e) return i;
    return -1;
}

void ingestEapol(Hs &hs, const uint8_t *frame, int flen, int off888e) {
    if (off888e < 24 || off888e + 99 >= flen) return;
    const uint8_t *fc = frame;
    uint16_t fctl = fc[0] | (fc[1] << 8);
    bool toDs = fctl & 0x0100;
    bool fromDs = fctl & 0x0200;
    const uint8_t *addr1 = frame + 4;
    const uint8_t *addr2 = frame + 10;
    const uint8_t *addr3 = frame + 16;
    const uint8_t *eapol = frame + off888e + 2;
    int elen = flen - (off888e + 2);
    if (elen < 95) return;
    uint8_t desc = eapol[4];
    if (desc != 2 && desc != 254) return;
    uint16_t keyInfo = (eapol[5] << 8) | eapol[6];
    const uint8_t *nonce = eapol + 17;
    const uint8_t *mic = eapol + 81;
    bool micSet = keyInfo & 0x0100;
    bool ack = keyInfo & 0x0080;
    if (fromDs && !toDs) {
        memcpy(hs.ap, addr2, 6);
        memcpy(hs.sta, addr1, 6);
        memcpy(hs.anonce, nonce, 32);
        hs.haveA = true;
    } else {
        memcpy(hs.sta, addr2, 6);
        memcpy(hs.ap, addr1, 6);
        memcpy(hs.snonce, nonce, 32);
        hs.haveS = true;
        if (micSet) {
            memcpy(hs.mic, mic, 16);
            hs.eapol.assign(eapol, eapol + elen);
            if ((int)hs.eapol.size() > 81 + 16) memset(hs.eapol.data() + 81, 0, 16);
            hs.haveMic = true;
        }
    }
    (void)addr3;
    (void)ack;
}

bool loadHandshake(FS &fs, const String &path, Hs &hs) {
    File f = fs.open(path, FILE_READ);
    if (!f) return false;
    std::vector<uint8_t> buf;
    buf.resize(f.size() > 256000 ? 256000 : f.size());
    int n = f.read(buf.data(), buf.size());
    f.close();
    int rec = 24;
    while (rec + 16 <= n) {
        uint32_t incl = buf[rec + 8] | (buf[rec + 9] << 8) | (buf[rec + 10] << 16) | (buf[rec + 11] << 24);
        int data = rec + 16;
        if (data + (int)incl > n) break;
        int off = find888e(buf.data() + data, incl);
        if (off >= 0) ingestEapol(hs, buf.data() + data, incl, off);
        rec = data + incl;
        if (hs.haveA && hs.haveS && hs.haveMic) return true;
    }
    return hs.haveMic && hs.haveA && hs.haveS;
}

bool micOk(const uint8_t pmk[32], const Hs &hs) {
    uint8_t minMac[6], maxMac[6], minN[32], maxN[32];
    if (memcmp(hs.ap, hs.sta, 6) < 0) {
        memcpy(minMac, hs.ap, 6);
        memcpy(maxMac, hs.sta, 6);
    } else {
        memcpy(minMac, hs.sta, 6);
        memcpy(maxMac, hs.ap, 6);
    }
    if (memcmp(hs.anonce, hs.snonce, 32) < 0) {
        memcpy(minN, hs.anonce, 32);
        memcpy(maxN, hs.snonce, 32);
    } else {
        memcpy(minN, hs.snonce, 32);
        memcpy(maxN, hs.anonce, 32);
    }
    uint8_t data[76];
    memcpy(data, minMac, 6);
    memcpy(data + 6, maxMac, 6);
    memcpy(data + 12, minN, 32);
    memcpy(data + 44, maxN, 32);
    uint8_t ptk[64];
    prf512(pmk, data, 76, ptk);
    uint8_t calc[20];
    if (!hmacSha1(ptk, 16, hs.eapol.data(), hs.eapol.size(), calc)) return false;
    return memcmp(calc, hs.mic, 16) == 0;
}

} // namespace

void aircrackMenu() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    std::vector<String> pcaps;
    File root = fs->open(kvx::paths::WIFI_CAPTURES_HANDSHAKES);
    if (root && root.isDirectory()) {
        File f;
        while ((f = root.openNextFile())) {
            String n = f.name();
            f.close();
            if (n.endsWith(".pcap")) pcaps.push_back(n);
        }
    }
    if (root) root.close();
    if (pcaps.empty()) {
        displayError("No handshake pcaps", true);
        return;
    }
    String chosen;
    std::vector<Option> opts;
    for (auto &p : pcaps) {
        String path = p;
        opts.push_back({path.c_str(), [&chosen, path]() { chosen = path; }});
    }
    opts.push_back({"Cancel", []() {}});
    int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Aircrack pcap");
    if (sel < 0 || sel == (int)opts.size() - 1 || chosen.length() == 0) return;
    if (!chosen.startsWith("/"))
        chosen = String(kvx::paths::WIFI_CAPTURES_HANDSHAKES) + "/" +
                 chosen.substring(chosen.lastIndexOf('/') + 1);

    Hs hs;
    bool parsed = loadHandshake(*fs, chosen, hs);
    String ssid = keyboard("", 32, "SSID for PMK");
    if (ssid.length() == 0 || ssid == "\x1B") return;
    File wl = fs->open(String(kvx::paths::WIFI_WORDLISTS) + "/wpa_wordlist.txt", FILE_READ);
    if (!wl) {
        displayError(String("No wordlist in\n") + kvx::paths::WIFI_WORDLISTS, true);
        return;
    }
    drawMainBorderWithTitle("Aircrack");
    tft.drawString(parsed ? "MIC verify ON" : "No full HS, PMK only", 10, 40);
    int tried = 0;
    uint8_t pmk[32];
    String found;
    EscPress = false;
    while (wl.available() && !check(EscPress)) {
        String pass = wl.readStringUntil('\n');
        pass.trim();
        if (pass.length() < 8) continue;
        if (!pmkCompute(ssid.c_str(), pass.c_str(), pmk)) continue;
        tried++;
        if (parsed && micOk(pmk, hs)) {
            found = pass;
            break;
        }
        if (tried % 2 == 0) {
            tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Tried " + String(tried) + " " + pass, 10, 56);
        }
    }
    wl.close();
    if (found.length()) displayInfo("MIC match\n" + found, true);
    else displayInfo("Tried " + String(tried) + (parsed ? "\nNo MIC match" : "\nNeed M1+M2 pcap"), true);
}
#endif

/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * On-device wordlist vs handshake PCAP (PMK via PBKDF2-SHA1).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "aircrack.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "mbedtls/pkcs5.h"
#include <globals.h>
#include <vector>

namespace {

int eapolHits(FS &fs, const String &path) {
    File f = fs.open(path, FILE_READ);
    if (!f) return -1;
    int hits = 0, prev = -1;
    uint8_t b[256];
    while (int n = f.read(b, sizeof(b))) {
        for (int i = 0; i < n; i++) {
            if (prev == 0x88 && b[i] == 0x8e) hits++;
            prev = b[i];
        }
    }
    f.close();
    return hits;
}

bool pmkCompute(const char *ssid, const char *pass, uint8_t pmk[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(
               MBEDTLS_MD_SHA1, (const unsigned char *)pass, strlen(pass), (const unsigned char *)ssid,
               strlen(ssid), 4096, 32, pmk
           ) == 0;
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
        chosen = String(kvx::paths::WIFI_CAPTURES_HANDSHAKES) + "/" + chosen.substring(chosen.lastIndexOf('/') + 1);

    int hits = eapolHits(*fs, chosen);
    String ssid = keyboard("", 32, "SSID for PMK");
    if (ssid.length() == 0 || ssid == "\x1B") return;
    File wl = fs->open(String(kvx::paths::WIFI_WORDLISTS) + "/wpa_wordlist.txt", FILE_READ);
    if (!wl) {
        displayError("No wordlist", true);
        return;
    }
    drawMainBorderWithTitle("Aircrack");
    tft.drawString("EAPOL markers: " + String(hits), 10, 40);
    int tried = 0;
    uint8_t pmk[32];
    EscPress = false;
    while (wl.available() && !check(EscPress)) {
        String pass = wl.readStringUntil('\n');
        pass.trim();
        if (pass.length() < 8) continue;
        pmkCompute(ssid.c_str(), pass.c_str(), pmk);
        tried++;
        if (tried % 3 == 0) {
            tft.fillRect(10, 56, tftWidth - 20, 16, kvxConfig.bgColor);
            tft.drawString("Tried " + String(tried) + " " + pass, 10, 56);
        }
    }
    wl.close();
    displayInfo("Tried " + String(tried) + " PMKs\nMIC verify not in this port\nEAPOL marks: " + String(hits),
                true);
}
#endif

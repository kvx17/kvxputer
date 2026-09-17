/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * NTLMv2 hash list / dedupe. Wordlist check uses hashes from Bruce Responder.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "ntlm.h"
#if defined(EVIL_EXTENSIONS)
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <set>
#include <globals.h>
#include <vector>

namespace {

FS *storageFs() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return nullptr;
    kvx::paths::ensureDir(*fs, kvx::paths::NETOPS_RESPONDER);
    return fs;
}

std::vector<String> loadHashes(FS &fs) {
    std::vector<String> lines;
    File f = fs.open(kvx::paths::NETOPS_RESPONDER_NTLM, FILE_READ);
    if (!f) return lines;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 8 && !line.startsWith("#")) lines.push_back(line);
    }
    f.close();
    return lines;
}

void crackNtlm() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    auto hashes = loadHashes(*fs);
    if (hashes.empty()) {
        displayError("No NTLM hashes\nRun Responder first", true);
        return;
    }
    std::vector<Option> opts;
    for (const auto &h : hashes) {
        String line = h;
        if (line.length() > 40) line = line.substring(0, 40) + "...";
        String full = h;
        opts.push_back({line.c_str(), [full]() { displayInfo(full, true); }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "NTLMv2 hashes");
}

void cleanNtlm() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    auto hashes = loadHashes(*fs);
    std::set<String> uniq;
    std::vector<String> out;
    for (const auto &h : hashes) {
        if (uniq.insert(h).second) out.push_back(h);
    }
    File f = fs->open(kvx::paths::NETOPS_RESPONDER_NTLM, FILE_WRITE);
    if (!f) {
        displayError("Write failed", true);
        return;
    }
    for (const auto &h : out) f.println(h);
    f.close();
    displayInfo("Kept " + String(out.size()) + " unique\nDropped " +
                    String((int)hashes.size() - (int)out.size()) + " dups",
                true);
}

} // namespace

void ntlmMenu() {
    while (true) {
        std::vector<Option> opts = {
            {"Crack NTLMv2", crackNtlm},
            {"Clean NTLMv2 dups", cleanNtlm},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "NTLMv2");
        if (sel < 0 || sel == (int)opts.size() - 1) return;
    }
}
#endif

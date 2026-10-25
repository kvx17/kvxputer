#if !defined(LITE_VERSION)
#include "loot_console.h"

#include "root/app/utils.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include <FS.h>
#include <globals.h>
#include <vector>

namespace {

struct LootEntry {
    String source;
    String path;
    String preview;
};

bool redact = true;

String redactLine(const String &line) {
    if (!redact) return line;
    String s = line;
    int p = s.indexOf("password");
    if (p < 0) p = s.indexOf("Password");
    if (p >= 0) {
        int colon = s.indexOf(':', p);
        if (colon > 0) {
            int end = s.indexOf(',', colon);
            if (end < 0) end = s.length();
            for (int i = colon + 1; i < end; i++) {
                if (s[i] != ' ') s[i] = '*';
            }
        }
    }
    if (s.indexOf("::") > 0 && s.length() > 40) return s.substring(0, 24) + "...[redacted]";
    return s;
}

void addFileEntries(FS &fs, const char *dir, const char *source, const char *ext, std::vector<LootEntry> &out) {
    if (!fs.exists(dir)) return;
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) return;
    File f;
    while ((f = root.openNextFile())) {
        String name = f.name();
        String full = name.startsWith("/") ? name : (String(dir) + "/" + name);
        bool ok = !ext || full.endsWith(ext) || name.endsWith(ext);
        if (ok && !f.isDirectory()) {
            LootEntry e;
            e.source = source;
            e.path = full;
            String line = f.readStringUntil('\n');
            line.trim();
            e.preview = redactLine(line.substring(0, 48));
            out.push_back(e);
        }
        f.close();
    }
}

void addSingleFile(FS &fs, const String &path, const char *source, std::vector<LootEntry> &out) {
    if (!fs.exists(path)) return;
    File f = fs.open(path, FILE_READ);
    if (!f) return;
    LootEntry e;
    e.source = source;
    e.path = path;
    String line = f.readStringUntil('\n');
    while ((line.startsWith("#") || line.length() < 3) && f.available()) line = f.readStringUntil('\n');
    line.trim();
    e.preview = redactLine(line.substring(0, 48));
    out.push_back(e);
    f.close();
}

std::vector<LootEntry> indexLoot(FS &fs) {
    std::vector<LootEntry> out;
    addFileEntries(fs, kvx::paths::WIFI_PORTAL_CREDS, "portal", ".csv", out);
    addSingleFile(fs, kvx::paths::NETOPS_RESPONDER_NTLM, "responder", out);
    addSingleFile(fs, String(kvx::paths::NETOPS_RESPONDER) + "/autodiscover.txt", "autodiscover", out);
    addSingleFile(fs, String(kvx::paths::WIFI_CAPTURES) + "/eap_identity.txt", "eap", out);
    return out;
}

void viewEntry(FS &fs, const LootEntry &e) {
    File f = fs.open(e.path, FILE_READ);
    if (!f) {
        displayError("Open fail", true);
        return;
    }
    ScrollableTextArea area(e.source);
    area.addLine(e.path);
    int n = 0;
    while (f.available() && n < 100) {
        area.addLine(redactLine(f.readStringUntil('\n')));
        n++;
    }
    f.close();
    area.show(true);
}

void exportMerged(FS &fs, const std::vector<LootEntry> &entries) {
    kvx::paths::ensureDir(fs, kvx::paths::NETOPS_LOOT);
    String outPath = String(kvx::paths::NETOPS_LOOT) + "/merged_" + String((uint32_t)millis()) + ".csv";
    File out = fs.open(outPath, FILE_WRITE);
    if (!out) {
        displayError("Export fail", true);
        return;
    }
    out.println("source,path,preview");
    for (const auto &e : entries) {
        out.printf("\"%s\",\"%s\",\"%s\"\n", e.source.c_str(), e.path.c_str(), e.preview.c_str());
    }
    out.close();
    displayInfo("Exported", true);
}

} // namespace

void lootConsoleHelp() {
    showAppHelp(
        {
            "One place for captured creds",
            "and hashes on SD/LittleFS.",
            "",
            "Indexes:",
            "  wifi/portals/creds/*.csv",
            "  netops/responder/ntlm_hashes",
            "  responder/autodiscover.txt",
            "  wifi/captures/eap_identity",
            "",
            "Redact toggle:",
            "  Masks passwords / hash middles",
            "  in previews and View.",
            "",
            "Per entry: View or Delete.",
            "Export merged CSV:",
            "  support_files/netops/loot/",
            "",
            "Refresh by reopening the menu.",
            "No live tail while tools run.",
        }
    );
}

void lootConsoleMenu() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }

    auto entries = indexLoot(*fs);
    options.clear();
    options.push_back({"Help", lootConsoleHelp});
    options.push_back({redact ? "Redact: ON (toggle)" : "Redact: OFF (toggle)", []() {
                           redact = !redact;
                           lootConsoleMenu();
                       }});
    options.push_back({"Export merged CSV", [=]() { exportMerged(*fs, entries); }});
    for (size_t i = 0; i < entries.size(); i++) {
        String label = entries[i].source + ": " + entries[i].preview.substring(0, 16);
        LootEntry e = entries[i];
        options.push_back({label.c_str(), [=]() {
                               options = {
                                   {"View", [=]() { viewEntry(*fs, e); }},
                                   {"Delete",
                                    [=]() {
                                        fs->remove(e.path);
                                        displayInfo("Deleted", true);
                                    }},
                                   {"Back", []() {}},
                               };
                               loopOptions(options, MENU_TYPE_SUBMENU, "Loot");
                               options.clear();
                           }});
    }
    if (entries.empty()) options.push_back({"(no loot files)", []() {}});
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Loot");
    options.clear();
}

#endif

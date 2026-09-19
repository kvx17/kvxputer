#include "pda_common.h"

#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <algorithm>
#include <globals.h>

bool pdaGetFs(FS *&fs) {
    if (!getFsStorage(fs) || fs == nullptr) {
        displayError("No storage (insert SD)", true);
        return false;
    }
    return true;
}

void pdaEnsureDirs(FS *fs) {
    if (fs == nullptr) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_NOTES);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_MEMOS);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_TODO);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_CALENDAR);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_CONTACTS);
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_ALARMS);
}

std::vector<String> pdaReadLines(FS *fs, const String &path) {
    std::vector<String> lines;
    if (fs == nullptr || !fs->exists(path)) return lines;

    File file = fs->open(path, FILE_READ);
    if (!file) return lines;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        lines.push_back(line);
    }
    file.close();

    // Drop trailing empty lines for a tidy list.
    while (!lines.empty() && lines.back().length() == 0) lines.pop_back();
    return lines;
}

bool pdaWriteLines(FS *fs, const String &path, const std::vector<String> &lines) {
    if (fs == nullptr) return false;
    kvx::paths::ensureParentDirs(*fs, path.c_str());

    File file = fs->open(path, FILE_WRITE);
    if (!file) return false;
    for (const String &line : lines) {
        file.print(line);
        file.print('\n');
    }
    file.close();
    return true;
}

std::vector<String> pdaListFiles(FS *fs, const String &dir, const String &ext) {
    std::vector<String> names;
    if (fs == nullptr) return names;

    File root = fs->open(dir);
    if (!root || !root.isDirectory()) return names;

    String wantExt = ext;
    wantExt.toLowerCase();

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        if (fullPath == "") break;
        if (isDir) continue;

        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        int dot = nameOnly.lastIndexOf('.');
        String fileExt = dot >= 0 ? nameOnly.substring(dot + 1) : "";
        fileExt.toLowerCase();
        if (wantExt == "*" || fileExt == wantExt) names.push_back(nameOnly);
    }
    root.close();

    std::sort(names.begin(), names.end(), [](const String &a, const String &b) {
        String ua = a, ub = b;
        ua.toUpperCase();
        ub.toUpperCase();
        return ua < ub;
    });
    return names;
}

String pdaSanitizeName(const String &name) {
    String out = "";
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        bool ok = isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.';
        out += ok ? c : '_';
    }
    out.trim();
    if (out.length() == 0) out = "untitled";
    return out;
}

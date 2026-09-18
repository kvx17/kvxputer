#include "pda_notes.h"
#include "pda_common.h"
#include "pda_editor.h"

#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <globals.h>

static String noteTitleFromBody(const String &content) {
    int nl = content.indexOf('\n');
    String t = (nl < 0) ? content : content.substring(0, nl);
    t.trim();
    if (t.length() > 40) {
        t = t.substring(0, 40);
        t.trim();
    }
    return pdaSanitizeName(t);
}

static String uniqueNotePath(FS *fs, const String &base, const String &keepPath) {
    String candidate = String(kvx::paths::PDA_NOTES) + "/" + base + ".txt";
    if (candidate == keepPath || !fs->exists(candidate)) return candidate;
    for (int n = 2; n < 1000; n++) {
        candidate = String(kvx::paths::PDA_NOTES) + "/" + base + "-" + String(n) + ".txt";
        if (candidate == keepPath || !fs->exists(candidate)) return candidate;
    }
    return candidate;
}

static bool saveNote(FS *fs, String &path, const String &content) {
    String base = noteTitleFromBody(content);
    String newPath = uniqueNotePath(fs, base, path);
    if (path.length() && newPath != path) {
        if (!fs->rename(path, newPath)) {
            displayError("Rename failed", true);
            newPath = path;
        }
    }
    path = newPath;

    File file = fs->open(path, FILE_WRITE);
    if (!file) {
        displayError("Save failed", true);
        return false;
    }
    file.print(content);
    file.close();
    displaySuccess("Note saved", true);
    return true;
}

static void pdaNoteCreate(FS *fs) {
    String content = "";
    if (pdaTextEditor(content, "New Note", 2048, true) != PDA_EDIT_OK) return;
    String path;
    saveNote(fs, path, content);
}

static void pdaNoteOpen(FS *fs, const String &filename) {
    String path = String(kvx::paths::PDA_NOTES) + "/" + filename;
    bool deleted = false;

    while (true) {
        std::vector<Option> opts = {
            {"Open",
             [&]() {
                 String content = readSmallFile(*fs, path);
                 String label = filename;
                 if (label.endsWith(".txt")) label.remove(label.length() - 4);
                 if (pdaTextEditor(content, label.c_str(), 2048, true) != PDA_EDIT_OK) return;
                 saveNote(fs, path, content);
             }},
            {"Delete",
             [&]() {
                 fs->remove(path);
                 deleted = true;
                 displaySuccess("Deleted", true);
             }},
        };
        int r = loopOptions(opts, MENU_TYPE_SUBMENU, filename.c_str());
        if (r < 0 || deleted || returnToMenu || forceHome) break;
    }
}

void pdaNotes() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_NOTES);

    while (true) {
        std::vector<String> files = pdaListFiles(fs, kvx::paths::PDA_NOTES, "txt");

        std::vector<Option> opts;
        opts.push_back({"New Note", [&]() { pdaNoteCreate(fs); }});
        for (const String &f : files) {
            String label = f;
            if (label.endsWith(".txt")) label.remove(label.length() - 4);
            opts.push_back({label, [&, f]() { pdaNoteOpen(fs, f); }});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Notes");
        if (r < 0 || returnToMenu || forceHome) break;
    }
}

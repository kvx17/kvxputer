#include "kremote_store.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include <SD.h>
#include <LittleFS.h>
#include <globals.h>

bool kremoteEnsureDir(FS *fs) {
    if (fs == nullptr) return false;
    kvx::paths::ensureDir(*fs, kvx::paths::IR_REMOTES);
    return (*fs).exists(kvx::paths::IR_REMOTES);
}

FS *kremotePickFs() {
    FS *fs = nullptr;
    if (getFsStorage(fs) && fs != nullptr) {
        kremoteEnsureDir(fs);
        return fs;
    }
    return nullptr;
}

String kremoteSanitizeName(const String &raw) {
    String out;
    out.reserve(KREMOTE_NAME_MAX);
    for (size_t i = 0; i < raw.length() && (int)out.length() < KREMOTE_NAME_MAX; i++) {
        char c = raw[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
            c == '-') {
            out += c;
        } else if (c == ' ') {
            out += '_';
        }
    }
    return out;
}

String kremoteFileNameFor(const String &slug) { return "kremote_" + slug + ".ir"; }

String kremotePathFor(const String &slug) {
    return String(kvx::paths::IR_REMOTES) + "/" + kremoteFileNameFor(slug);
}

std::vector<KremoteProfile> kremoteListProfiles() {
    std::vector<KremoteProfile> list;
    FS *fs = kremotePickFs();
    if (fs == nullptr) return list;

    File dir = fs->open(kvx::paths::IR_REMOTES);
    if (!dir || !dir.isDirectory()) return list;

    File f = dir.openNextFile();
    while (f) {
        String name = f.name();
        // Strip directory prefix if present
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        f.close();

        if (name.startsWith("kremote_") && name.endsWith(".ir") && name.length() > 11) {
            KremoteProfile p;
            p.displayName = name.substring(8, name.length() - 3);
            p.path = String(kvx::paths::IR_REMOTES) + "/" + name;
            p.fs = fs;
            list.push_back(p);
        }
        f = dir.openNextFile();
    }
    dir.close();
    return list;
}

bool kremoteProfileExists(const String &slug) {
    FS *fs = kremotePickFs();
    if (fs == nullptr || slug.length() == 0) return false;
    return fs->exists(kremotePathFor(slug));
}

bool kremoteDeleteProfile(const KremoteProfile &profile) {
    if (profile.fs == nullptr || profile.path.length() == 0) return false;
    return profile.fs->remove(profile.path);
}

static int kremoteSlotIndexForName(const String &name) {
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) {
        if (name.equalsIgnoreCase(kremoteSlotName(i))) return i;
    }
    return -1;
}

void kremoteFreeSlots(IRCode *slots[KREMOTE_SLOT_COUNT]) {
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) {
        delete slots[i];
        slots[i] = nullptr;
    }
}

bool kremoteLoadSlots(FS *fs, const String &path, IRCode *slots[KREMOTE_SLOT_COUNT]) {
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) slots[i] = nullptr;
    if (fs == nullptr) return false;

    File file = fs->open(path, FILE_READ);
    if (!file) return false;

    IRCode *cur = new IRCode();
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        String txt = line.substring(line.indexOf(':') + 1);
        txt.trim();

        if (line.startsWith("name:")) {
            if (cur->name.length() > 0) {
                int idx = kremoteSlotIndexForName(cur->name);
                if (idx >= 0) {
                    delete slots[idx];
                    slots[idx] = cur;
                } else {
                    delete cur;
                }
                cur = new IRCode();
            }
            cur->name = txt;
            cur->filepath = path;
        } else if (line.startsWith("type:")) {
            cur->type = txt;
        } else if (line.startsWith("protocol:")) {
            cur->protocol = txt;
        } else if (line.startsWith("address:")) {
            cur->address = txt;
        } else if (line.startsWith("frequency:")) {
            cur->frequency = (uint16_t)txt.toInt();
        } else if (line.startsWith("bits:")) {
            cur->bits = (uint8_t)txt.toInt();
        } else if (line.startsWith("command:")) {
            cur->command = txt;
        } else if (line.startsWith("data:") || line.startsWith("value:") || line.startsWith("state:")) {
            cur->data = txt;
        } else if (line.startsWith("#") && cur->name.length() > 0) {
            int idx = kremoteSlotIndexForName(cur->name);
            if (idx >= 0) {
                delete slots[idx];
                slots[idx] = cur;
            } else {
                delete cur;
            }
            cur = new IRCode();
        }
    }

    if (cur->name.length() > 0) {
        int idx = kremoteSlotIndexForName(cur->name);
        if (idx >= 0) {
            delete slots[idx];
            slots[idx] = cur;
            cur = nullptr;
        }
    }
    delete cur;
    file.close();
    return true;
}

String kremoteCodeToBlock(const IRCode &code) {
    String block;
    block.reserve(160);
    block += "name: ";
    block += code.name;
    block += "\n";
    if (code.type == "raw" || code.protocol.length() == 0) {
        block += "type: raw\n";
        block += "frequency: ";
        block += String(code.frequency ? code.frequency : 38000);
        block += "\n";
        block += "duty_cycle: 0.330000\n";
        block += "data: ";
        block += code.data;
        block += "\n";
    } else {
        block += "type: parsed\n";
        block += "protocol: ";
        block += code.protocol;
        block += "\n";
        block += "address: ";
        block += code.address;
        block += "\n";
        block += "command: ";
        block += code.command;
        block += "\n";
        block += "bits: ";
        block += String(code.bits);
        block += "\n";
        if (code.data.length() > 0) {
            // Prefer value: for Flipper; state/data both stored in data field
            if (code.data.indexOf(' ') >= 0 && code.data.length() > 11) {
                block += "value: ";
            } else if (code.bits > 32) {
                block += "state: ";
            } else {
                block += "value: ";
            }
            block += code.data;
            block += "\n";
        }
    }
    block += "#\n";
    return block;
}

bool kremoteSaveSlots(FS *fs, const String &path, const String &slug, IRCode *slots[KREMOTE_SLOT_COUNT]) {
    if (fs == nullptr) return false;
    kremoteEnsureDir(fs);

    File file = fs->open(path, FILE_WRITE);
    if (!file) return false;

    file.println("Filetype: IR signals file");
    file.println("Version: 1");
    file.println("#");
    file.println("# kremote_" + slug);

    int written = 0;
    for (int i = 0; i < KREMOTE_SLOT_COUNT; i++) {
        if (slots[i] == nullptr) continue;
        slots[i]->name = kremoteSlotName(i);
        file.print(kremoteCodeToBlock(*slots[i]));
        written++;
    }
    file.close();
    return written > 0;
}

#include "kremote_store.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <LittleFS.h>
#include <globals.h>

bool kremoteEnsureDir(FS *fs) {
    if (fs == nullptr) return false;
    kvx::paths::ensureDir(*fs, kvx::paths::IR_REMOTES);
    return (*fs).exists(kvx::paths::IR_REMOTES);
}

FS *kremotePickFs() {
    setupSdCard();
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
    bool ok = profile.fs->remove(profile.path);
    if (ok) kremoteRemoveFavorite(profile.displayName);
    return ok;
}

static bool kremoteSdReady() {
    return setupSdCard() && sdcardMounted;
}

bool kremoteSaveFavorites(const std::vector<String> &favs);

std::vector<String> kremoteLoadFavorites() {
    std::vector<String> favs;
    if (!kremoteSdReady()) return favs;
    if (!SD.exists(kvx::paths::KREMOTE_USER_SETTINGS)) return favs;

    File file = SD.open(kvx::paths::KREMOTE_USER_SETTINGS, FILE_READ);
    if (!file) return favs;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return favs;

    JsonArray arr = doc["favorites"].as<JsonArray>();
    if (arr.isNull()) return favs;

    bool dirty = false;
    for (JsonVariant v : arr) {
        String slug = v.as<String>();
        if (slug.length() == 0) continue;
        if (!kremoteProfileExists(slug)) {
            dirty = true;
            continue;
        }
        favs.push_back(slug);
    }
    if (dirty) kremoteSaveFavorites(favs);
    return favs;
}

bool kremoteSaveFavorites(const std::vector<String> &favs) {
    if (!kremoteSdReady()) return false;
    kvx::paths::ensureDir(SD, kvx::paths::KREMOTE_DIR);

    JsonDocument doc;
    JsonArray arr = doc["favorites"].to<JsonArray>();
    for (const auto &s : favs) arr.add(s);

    File file = SD.open(kvx::paths::KREMOTE_USER_SETTINGS, FILE_WRITE);
    if (!file) return false;
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

bool kremoteIsFavorite(const String &slug) {
    auto favs = kremoteLoadFavorites();
    for (const auto &f : favs) {
        if (f == slug) return true;
    }
    return false;
}

bool kremoteToggleFavorite(const String &slug) {
    if (slug.length() == 0) return false;
    auto favs = kremoteLoadFavorites();
    for (size_t i = 0; i < favs.size(); i++) {
        if (favs[i] == slug) {
            favs.erase(favs.begin() + i);
            kremoteSaveFavorites(favs);
            return false;
        }
    }
    if (!kremoteSdReady()) {
        displayError("No SD card", true);
        return false;
    }
    favs.push_back(slug);
    if (!kremoteSaveFavorites(favs)) {
        displayError("Save failed", true);
        return false;
    }
    return true;
}

void kremoteRemoveFavorite(const String &slug) {
    auto favs = kremoteLoadFavorites();
    bool changed = false;
    for (size_t i = 0; i < favs.size();) {
        if (favs[i] == slug) {
            favs.erase(favs.begin() + i);
            changed = true;
        } else {
            i++;
        }
    }
    if (changed) kremoteSaveFavorites(favs);
}

std::vector<KremoteProfile> kremoteListFavoriteProfiles() {
    std::vector<KremoteProfile> out;
    auto favs = kremoteLoadFavorites();
    auto all = kremoteListProfiles();
    for (const auto &slug : favs) {
        for (const auto &p : all) {
            if (p.displayName == slug) {
                out.push_back(p);
                break;
            }
        }
    }
    return out;
}

String kremoteSlugFromPath(const String &filepath) {
    String name = filepath;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    // Case-insensitive .ir and kremote_ prefix
    String lower = name;
    lower.toLowerCase();
    if (lower.startsWith("kremote_") && lower.endsWith(".ir") && name.length() > 11) {
        return name.substring(8, name.length() - 3);
    }
    // Any other .ir → sanitized basename (so Browse IR always can favorite)
    if (lower.endsWith(".ir") && name.length() > 3) {
        String base = name.substring(0, name.length() - 3);
        return kremoteSanitizeName(base);
    }
    return kremoteSanitizeName(name);
}

// Ensure a kremote_<slug>.ir exists for Favorites (copy arbitrary .ir if needed).
bool kremoteEnsureFavoriteFile(FS *srcFs, const String &filepath, const String &slug) {
    if (slug.length() == 0 || srcFs == nullptr) return false;
    FS *dst = kremotePickFs();
    if (dst == nullptr) return false;
    String dest = kremotePathFor(slug);
    if (dst->exists(dest)) return true;
    kremoteEnsureDir(dst);
    File in = srcFs->open(filepath, FILE_READ);
    if (!in) return false;
    File out = dst->open(dest, FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }
    uint8_t buf[256];
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        if (n) out.write(buf, n);
    }
    in.close();
    out.close();
    return dst->exists(dest);
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

static void kremoteParseCodeFields(IRCode *cur, const String &line, const String &txt) {
    if (line.startsWith("type:")) cur->type = txt;
    else if (line.startsWith("protocol:")) cur->protocol = txt;
    else if (line.startsWith("address:")) cur->address = txt;
    else if (line.startsWith("frequency:")) cur->frequency = (uint16_t)txt.toInt();
    else if (line.startsWith("bits:")) cur->bits = (uint8_t)txt.toInt();
    else if (line.startsWith("command:")) cur->command = txt;
    else if (line.startsWith("data:") || line.startsWith("value:") || line.startsWith("state:")) {
        cur->data = txt;
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
        } else if (line.startsWith("#") && cur->name.length() > 0) {
            int idx = kremoteSlotIndexForName(cur->name);
            if (idx >= 0) {
                delete slots[idx];
                slots[idx] = cur;
            } else {
                delete cur;
            }
            cur = new IRCode();
        } else {
            kremoteParseCodeFields(cur, line, txt);
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

void kremoteFreeAllCodes(std::vector<IRCode *> &codes) {
    for (IRCode *c : codes) delete c;
    codes.clear();
}

bool kremoteLoadAllCodes(FS *fs, const String &path, std::vector<IRCode *> &out) {
    kremoteFreeAllCodes(out);
    if (fs == nullptr) return false;

    File file = fs->open(path, FILE_READ);
    if (!file) return false;

    IRCode *cur = new IRCode();
    while (file.available() && (int)out.size() < KREMOTE_ALL_CODES_MAX) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        String txt = line.substring(line.indexOf(':') + 1);
        txt.trim();

        if (line.startsWith("name:")) {
            if (cur->name.length() > 0) {
                out.push_back(cur);
                cur = new IRCode();
                if ((int)out.size() >= KREMOTE_ALL_CODES_MAX) break;
            }
            cur->name = txt;
            cur->filepath = path;
        } else if (line.startsWith("#") && cur->name.length() > 0) {
            out.push_back(cur);
            cur = new IRCode();
            if ((int)out.size() >= KREMOTE_ALL_CODES_MAX) break;
        } else {
            kremoteParseCodeFields(cur, line, txt);
        }
    }

    if (cur->name.length() > 0 && (int)out.size() < KREMOTE_ALL_CODES_MAX) {
        out.push_back(cur);
        cur = nullptr;
    }
    delete cur;
    file.close();
    return !out.empty();
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

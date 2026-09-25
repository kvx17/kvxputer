#pragma once

#include "menu/infrared/custom_ir.h"
#include "kremote_config.h"
#include <FS.h>
#include <vector>

struct KremoteProfile {
    String displayName; // slug without prefix/suffix
    String path;        // full path on FS
    FS *fs = nullptr;
};

bool kremoteEnsureDir(FS *fs);
FS *kremotePickFs();
String kremoteSanitizeName(const String &raw);
String kremoteFileNameFor(const String &slug);
String kremotePathFor(const String &slug);

std::vector<KremoteProfile> kremoteListProfiles();
bool kremoteProfileExists(const String &slug);
bool kremoteDeleteProfile(const KremoteProfile &profile);

// Favorites (SD /kvxputer/kvxuniversalremote/userSettings.json only)
std::vector<String> kremoteLoadFavorites();
bool kremoteSaveFavorites(const std::vector<String> &favs);
bool kremoteIsFavorite(const String &slug);
bool kremoteToggleFavorite(const String &slug); // returns new favorited state; false if no SD on add
void kremoteRemoveFavorite(const String &slug);
std::vector<KremoteProfile> kremoteListFavoriteProfiles();
// Slug from kremote_<slug>.ir or sanitized basename of any .ir
String kremoteSlugFromPath(const String &filepath);
// Copy filepath → IR_REMOTES/kremote_<slug>.ir when missing (so Favorites can open it).
bool kremoteEnsureFavoriteFile(FS *srcFs, const String &filepath, const String &slug);

// Load named codes from a .ir file into slots[KREMOTE_SLOT_COUNT] (nullptr = missing).
// Caller owns returned IRCode pointers and must delete them.
bool kremoteLoadSlots(FS *fs, const String &path, IRCode *slots[KREMOTE_SLOT_COUNT]);
void kremoteFreeSlots(IRCode *slots[KREMOTE_SLOT_COUNT]);

// Load every named command from a .ir file (cap KREMOTE_ALL_CODES_MAX).
// Caller owns the pointers and must call kremoteFreeAllCodes.
bool kremoteLoadAllCodes(FS *fs, const String &path, std::vector<IRCode *> &out);
void kremoteFreeAllCodes(std::vector<IRCode *> &codes);

// Write Flipper-compatible .ir from non-null slots. Overwrites path.
bool kremoteSaveSlots(FS *fs, const String &path, const String &slug, IRCode *slots[KREMOTE_SLOT_COUNT]);

// Serialize one captured decode_results-style IRCode into Flipper block text.
String kremoteCodeToBlock(const IRCode &code);

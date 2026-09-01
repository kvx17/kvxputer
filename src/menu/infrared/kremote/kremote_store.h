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

// Load named codes from a .ir file into slots[KREMOTE_SLOT_COUNT] (nullptr = missing).
// Caller owns returned IRCode pointers and must delete them.
bool kremoteLoadSlots(FS *fs, const String &path, IRCode *slots[KREMOTE_SLOT_COUNT]);
void kremoteFreeSlots(IRCode *slots[KREMOTE_SLOT_COUNT]);

// Write Flipper-compatible .ir from non-null slots. Overwrites path.
bool kremoteSaveSlots(FS *fs, const String &path, const String &slug, IRCode *slots[KREMOTE_SLOT_COUNT]);

// Serialize one captured decode_results-style IRCode into Flipper block text.
String kremoteCodeToBlock(const IRCode &code);

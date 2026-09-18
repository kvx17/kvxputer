#ifndef __PDA_COMMON_H__
#define __PDA_COMMON_H__

#include <Arduino.h>
#include <FS.h>
#include <vector>

// Shared helpers for the kvxputer PDA (Pocket Device Assistant).
// All PDA data lives under /support_files/pda/* (see kvx::paths).

// Pick the active storage (SD if mounted, otherwise LittleFS). Shows an error
// and returns false when no storage is usable.
bool pdaGetFs(FS *&fs);

// Ensure every PDA subfolder exists on the given filesystem.
void pdaEnsureDirs(FS *fs);

// Read a text file into a vector of lines (trailing empty lines dropped).
std::vector<String> pdaReadLines(FS *fs, const String &path);

// Overwrite a text file with the given lines (one per line, '\n' separated).
bool pdaWriteLines(FS *fs, const String &path, const std::vector<String> &lines);

// List base filenames in a directory that match the given extension (no dot).
std::vector<String> pdaListFiles(FS *fs, const String &dir, const String &ext);

// Replace filesystem-unsafe characters so a title can be used as a filename.
String pdaSanitizeName(const String &name);

#endif

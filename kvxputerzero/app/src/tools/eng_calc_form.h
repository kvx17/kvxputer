#pragma once

#include "eng_calc_lib.h"

#include <string>
#include <vector>

namespace tools::engcalc {

enum class FormKind {
    Labeled = 0,
    Paste,
    List,
};

// Auto-insert separators while typing so Sym is not required for common values.
enum class AutoFormat {
    None = 0,
    Mac,     // AA:BB:CC:DD:EE:FF (or AA:BB:CC for OUI)
    Ipv4,    // a.b.c.d — '.' after 3 digits, or earlier if user taps '.'
    GeoDec,  // lat/lon: insert '.' after the integer part
    IsoDate, // YYYY-MM-DD
    IsoTime, // HH:MM:SS
};

struct FieldDef {
    const char* key;
    const char* label;
    AutoFormat format{AutoFormat::None};
};

struct ModeDef {
    const char* token; // "" = no leading token
    const char* label;
    const FieldDef* fields;
    int field_count;
};

struct OpSchema {
    FormKind kind;
    const char* glyphs;
    const ModeDef* modes;
    int mode_count;
    // For Paste / single-mode Labeled without ModeDef array:
    const FieldDef* fields;
    int field_count;
    int list_min;
    int list_max;
};

const OpSchema& schema_for(Op op);

// Active field defs for the current mode (nullptr / 0 if paste/list without labels).
const FieldDef* active_fields(Op op, int mode_index, int& count);

// Split a saved/canonical line into mode index + field values for the form.
void parse_line_into(Op op, const std::string& line, int& mode_index, std::vector<std::string>& values);

// Build the engcalc::run() input line from mode + values.
std::string build_line(Op op, int mode_index, const std::vector<std::string>& values);

// Reformat a field value after a keystroke. Returns the new text and caret index.
std::string apply_auto_format(AutoFormat fmt, const std::string& raw, int& caret);

} // namespace tools::engcalc

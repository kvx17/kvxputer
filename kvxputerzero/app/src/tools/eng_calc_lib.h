#pragma once

// Engineering calculator library (pure C++17).
//
// Ported from the Cardputer calcs in src/menu/others/calc/. One Op per calc
// menu entry. Input is a single text line:
//
//     [mode] field field field ...
//
// The first whitespace token is the mode name (or a short alias) when it
// matches one; the remaining tokens are the Cardputer fields in order. Unknown
// fields are written as "-" (or "_" / "?") where a calc solves for one of them.
// Where a calc has a historical mode-less form it is still accepted
// (see input_hint()).

#include <string>

namespace tools::engcalc {

enum class Op {
    Subnet = 0,
    BaseConvert,
    ByteUnits,
    Resistor,
    LedResistor,
    VoltageDivider,
    Baud,
    Dbm,
    Epoch,
    Measure,
    Bitmask,
    Crc,
    Units,
    OhmsLaw,
    VoltageDrop,
    Motor,
    Gas,
    Flow,
    Parallel,
    RcTimer,
    Battery,
    Frequency,
    Pwm,
    Percentage,
    DateDiff,
    TimeDiff,
    TipSplit,
    Distance,
    Count,
};

// Menu label (matches the Cardputer calc menu).
const char* op_label(Op op);

// Brief input grammar (may contain '\n').
const char* input_hint(Op op);

// One short example line for the eng-calc screen header (fits 320px).
const char* input_example(Op op);

// Evaluates one calculator line. On success ok=true and the multi-line result
// is returned; on failure ok=false and a one-line error message is returned.
std::string run(Op op, const std::string& input, bool& ok);

} // namespace tools::engcalc

#pragma once

#include "eng_calc_lib.h"

namespace tools::engcalc {

// Multi-line help for the calculator modal (what / fields / example / keys).
const char* help_text(Op op);

// Life-counter scoring help (game 0=Yu-Gi-Oh, 1=Magic, 2=Truco).
const char* life_help_text(int game);

const char* scientific_help_text();

} // namespace tools::engcalc

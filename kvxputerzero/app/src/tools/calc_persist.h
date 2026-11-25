#pragma once

#include "eng_calc_lib.h"

#include <string>
#include <vector>

namespace tools::calcpersist {

struct Favorite {
    std::string name;
    std::string line;
};

struct OpState {
    std::string last_line;
    std::vector<Favorite> favorites; // newest first, capped
};

constexpr int kMaxFavorites = 8;

std::string op_file_stem(engcalc::Op op);
OpState load_op(engcalc::Op op);
void save_op(engcalc::Op op, const OpState& state);

// Scientific calculator expression + history lines.
struct SciState {
    std::string last_expr;
    std::vector<std::string> history;
};
SciState load_scientific();
void save_scientific(const SciState& state);

// Add current line as a favorite (default name = truncated line).
void add_favorite(OpState& state, const std::string& line, const std::string& name = {});

} // namespace tools::calcpersist

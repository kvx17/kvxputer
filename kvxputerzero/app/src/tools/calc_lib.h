#pragma once

#include <string>
#include <vector>

namespace tools::calc {

struct State {
    bool deg_mode = true;
    double ans = 0.0;
    bool has_ans = false;
    std::vector<std::string> history; // newest first, capped at 8
};

// Evaluate a scientific expression. On success sets result and updates ans/history.
bool evaluate(const std::string& expr, State& state, double& result, std::string& error);

std::string format_result(double value);

// Persist deg_mode under ~/.config/kvxputerzero/
void load_prefs(State& state);
void save_prefs(const State& state);

} // namespace tools::calc

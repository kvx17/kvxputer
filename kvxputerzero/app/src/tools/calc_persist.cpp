#include "calc_persist.h"

#include "user_paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace tools::calcpersist {
namespace {

namespace fs = std::filesystem;

std::string calc_dir() {
    const fs::path dir = fs::path(tools::config_dir()) / "calc";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

std::string path_for(const std::string& stem) {
    return (fs::path(calc_dir()) / (stem + ".txt")).string();
}

} // namespace

std::string op_file_stem(engcalc::Op op) {
    switch (op) {
    case engcalc::Op::Subnet: return "subnet";
    case engcalc::Op::BaseConvert: return "base";
    case engcalc::Op::ByteUnits: return "bytes";
    case engcalc::Op::Resistor: return "resistor";
    case engcalc::Op::LedResistor: return "led";
    case engcalc::Op::VoltageDivider: return "divider";
    case engcalc::Op::Baud: return "baud";
    case engcalc::Op::Dbm: return "dbm";
    case engcalc::Op::Epoch: return "epoch";
    case engcalc::Op::Measure: return "measure";
    case engcalc::Op::Bitmask: return "bitmask";
    case engcalc::Op::Crc: return "crc";
    case engcalc::Op::Units: return "units";
    case engcalc::Op::OhmsLaw: return "ohms";
    case engcalc::Op::VoltageDrop: return "vdrop";
    case engcalc::Op::Motor: return "motor";
    case engcalc::Op::Gas: return "gas";
    case engcalc::Op::Flow: return "flow";
    case engcalc::Op::Parallel: return "parallel";
    case engcalc::Op::RcTimer: return "rc";
    case engcalc::Op::Battery: return "battery";
    case engcalc::Op::Frequency: return "freq";
    case engcalc::Op::Pwm: return "pwm";
    case engcalc::Op::Percentage: return "pct";
    case engcalc::Op::DateDiff: return "datediff";
    case engcalc::Op::TimeDiff: return "timediff";
    case engcalc::Op::TipSplit: return "tip";
    case engcalc::Op::Distance: return "distance";
    case engcalc::Op::Reactance: return "reactance";
    case engcalc::Op::Timer555: return "555";
    case engcalc::Op::Entropy: return "entropy";
    case engcalc::Op::HashId: return "hashid";
    case engcalc::Op::Jwt: return "jwt";
    case engcalc::Op::Xor: return "xor";
    case engcalc::Op::CidrAgg: return "cidr";
    case engcalc::Op::Oui: return "oui";
    case engcalc::Op::Fspl: return "fspl";
    case engcalc::Op::Transfer: return "transfer";
    case engcalc::Op::Kwh: return "kwh";
    case engcalc::Op::Coord: return "coord";
    case engcalc::Op::HexView: return "hexview";
    case engcalc::Op::Count: break;
    }
    return "op";
}

OpState load_op(engcalc::Op op) {
    OpState st;
    std::ifstream in(path_for(op_file_stem(op)));
    if (!in) return st;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("last=", 0) == 0) {
            st.last_line = line.substr(5);
        } else if (line.rfind("fav=", 0) == 0) {
            const std::string rest = line.substr(4);
            const auto tab = rest.find('\t');
            Favorite f;
            if (tab == std::string::npos) {
                f.name = rest.substr(0, 24);
                f.line = rest;
            } else {
                f.name = rest.substr(0, tab);
                f.line = rest.substr(tab + 1);
            }
            if (!f.line.empty()) st.favorites.push_back(std::move(f));
        }
    }
    if (static_cast<int>(st.favorites.size()) > kMaxFavorites) {
        st.favorites.resize(static_cast<size_t>(kMaxFavorites));
    }
    return st;
}

void save_op(engcalc::Op op, const OpState& state) {
    std::ofstream out(path_for(op_file_stem(op)), std::ios::trunc);
    if (!out) return;
    out << "last=" << state.last_line << "\n";
    const int n = std::min(static_cast<int>(state.favorites.size()), kMaxFavorites);
    for (int i = 0; i < n; ++i) {
        out << "fav=" << state.favorites[static_cast<size_t>(i)].name << '\t'
            << state.favorites[static_cast<size_t>(i)].line << "\n";
    }
}

SciState load_scientific() {
    SciState st;
    std::ifstream in(path_for("scientific"));
    if (!in) return st;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("last=", 0) == 0) st.last_expr = line.substr(5);
        else if (line.rfind("hist=", 0) == 0) st.history.push_back(line.substr(5));
    }
    return st;
}

void save_scientific(const SciState& state) {
    std::ofstream out(path_for("scientific"), std::ios::trunc);
    if (!out) return;
    out << "last=" << state.last_expr << "\n";
    const int n = std::min(static_cast<int>(state.history.size()), 8);
    for (int i = 0; i < n; ++i) {
        out << "hist=" << state.history[static_cast<size_t>(i)] << "\n";
    }
}

void add_favorite(OpState& state, const std::string& line, const std::string& name) {
    if (line.empty()) return;
    Favorite f;
    f.line = line;
    f.name = name.empty() ? line.substr(0, std::min<size_t>(line.size(), 24)) : name;
    // Drop duplicates of the same line.
    state.favorites.erase(
        std::remove_if(state.favorites.begin(), state.favorites.end(),
                       [&](const Favorite& x) { return x.line == line; }),
        state.favorites.end());
    state.favorites.insert(state.favorites.begin(), std::move(f));
    if (static_cast<int>(state.favorites.size()) > kMaxFavorites) {
        state.favorites.resize(static_cast<size_t>(kMaxFavorites));
    }
}

} // namespace tools::calcpersist

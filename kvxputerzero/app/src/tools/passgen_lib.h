#pragma once

#include <string>
#include <vector>

namespace tools::passgen {

struct Options {
    int length = 16;
    int count = 1;
    bool digits = true;
    bool lower = true;
    bool upper = true;
    bool symbols = false;
};

std::string build_alphabet(const Options& opt);
std::vector<std::string> generate(const Options& opt);
bool append_to_file(const std::vector<std::string>& passwords, std::string& path_out, std::string& error);

} // namespace tools::passgen

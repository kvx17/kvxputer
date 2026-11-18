#include "passgen_lib.h"

#include "user_paths.h"

#include <fstream>
#include <random>
#include <sys/stat.h>

namespace tools::passgen {
namespace {

constexpr char kDigits[] = "0123456789";
constexpr char kLower[] = "abcdefghijklmnopqrstuvwxyz";
constexpr char kUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char kSymbols[] = "!#$%^&*.-_";

} // namespace

std::string build_alphabet(const Options& opt) {
    std::string a;
    if (opt.digits) a += kDigits;
    if (opt.lower) a += kLower;
    if (opt.upper) a += kUpper;
    if (opt.symbols) a += kSymbols;
    return a;
}

std::vector<std::string> generate(const Options& opt) {
    std::vector<std::string> out;
    const std::string alpha = build_alphabet(opt);
    if (alpha.empty() || opt.length <= 0 || opt.count <= 0) return out;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, alpha.size() - 1);

    out.reserve(static_cast<size_t>(opt.count));
    for (int i = 0; i < opt.count; ++i) {
        std::string pw;
        pw.reserve(static_cast<size_t>(opt.length));
        for (int j = 0; j < opt.length; ++j) pw.push_back(alpha[dist(gen)]);
        out.push_back(std::move(pw));
    }
    return out;
}

bool append_to_file(const std::vector<std::string>& passwords, std::string& path_out, std::string& error) {
    path_out = tools::config_file("passwords.txt");
    std::ofstream out(path_out, std::ios::app);
    if (!out) {
        error = "cannot open passwords.txt";
        return false;
    }
    for (const auto& pw : passwords) out << pw << '\n';
    out.close();
#if !defined(_WIN32)
    ::chmod(path_out.c_str(), 0600);
#endif
    return true;
}

} // namespace tools::passgen

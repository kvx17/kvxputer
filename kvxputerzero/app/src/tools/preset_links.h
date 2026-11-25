#pragma once

#include <cstddef>
#include <string>

// Shared preset URLs for QRCodes and NFC Emulate Tag menus.
namespace tools::links {

inline constexpr const char* kRepoUrl = "https://github.com/kvx17/kvxputer";
inline constexpr const char* kRepoLabel = "kvxputer repo";
inline constexpr const char* kGithubUrl = "https://github.com/kvx17";
inline constexpr const char* kGithubLabel = "kvx17 GitHub";
inline constexpr const char* kCoffeeUrl = "https://buymeacoffee.com/kvx17";
inline constexpr const char* kCoffeeLabel = "Buy me a coffee";
inline constexpr const char* kZeroDocsUrl = "https://docs.m5stack.com/en/CardputerZero";

// Useful Links submenu (same list in QR + NFC).
inline constexpr const char* kUseful[] = {
    "https://docs.m5stack.com/en/CardputerZero",
    "https://learn-esp32.vercel.app/#board",
    "https://burner.m5stack.com/",
    "https://syncthing.net/",
    "https://jwtsecrets.com/#generator",
    "https://www.theodinproject.com/",
    "https://it-tools.tech/",
    "https://free-for.dev/#/",
    "https://www.gitreverse.com/",
    "https://www.skills.sh/",
};
inline constexpr size_t kUsefulCount = sizeof(kUseful) / sizeof(kUseful[0]);

inline std::string short_label(const char* url) {
    std::string label = url ? url : "";
    if (label.rfind("https://", 0) == 0) label = label.substr(8);
    if (label.rfind("http://", 0) == 0) label = label.substr(7);
    if (label.rfind("www.", 0) == 0) label = label.substr(4);
    return label;
}

} // namespace tools::links

#pragma once

#include <string>

namespace tools {

// XDG config dir for kvxputerzero (~/.config/kvxputerzero), created if needed.
std::string config_dir();
std::string config_file(const char* name);

} // namespace tools

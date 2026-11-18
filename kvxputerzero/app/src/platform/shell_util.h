#pragma once

#include <string>

namespace platform {

// Run a shell command, capture stdout+stderr. Returns exit code (-1 on popen fail).
int run_shell(const std::string& cmd, std::string& output);

} // namespace platform

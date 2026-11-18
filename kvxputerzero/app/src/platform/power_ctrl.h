#pragma once

#include <string>

namespace platform {

bool safe_reboot(std::string& error);
bool safe_shutdown(std::string& error);

} // namespace platform

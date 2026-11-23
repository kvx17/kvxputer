#pragma once

#include <string>

namespace platform {

// Ask the desktop session to lock, the same idea as Win+L.
// Returns false when no lock helper answered. `detail` explains why.
bool request_session_lock(std::string& detail);

} // namespace platform

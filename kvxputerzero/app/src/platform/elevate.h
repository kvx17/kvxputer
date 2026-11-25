#pragma once

#include <string>
#include <vector>

namespace platform {

// sudo exists on PATH-equivalent locations used by the image.
bool sudo_available();

// Wi-Fi monitor and BLE raw scan can open their sockets (root, or file capabilities).
bool net_helpers_ready();

// A root helper can be launched now: we are root, or a password was accepted this session.
bool root_session_ready();

// Check the password, then set cap_net_raw,cap_net_admin on the installed helpers.
// The password stays in memory for later root commands and is not written to disk.
bool grant_net_helpers(const std::string& password, std::string& error);

// Check the password and remember it for this process. Does not change file modes.
bool accept_root_password(const std::string& password, std::string& error);

// argv[0] is the program. Runs it under sudo with the remembered password. No shell.
bool sudo_exec(const std::vector<std::string>& argv, std::string& output, std::string& error);

} // namespace platform

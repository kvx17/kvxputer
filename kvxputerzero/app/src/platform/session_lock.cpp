#include "session_lock.h"

#include <sys/wait.h>

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace platform {
namespace {

// popen status is a wait(2) word. system()/pclose returning 0 is success;
// a normal non-zero exit is shifted and must not be treated as success.
bool command_ok(const std::string& command, std::string* output) {
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) return false;
    std::string text;
    char buffer[256];
    while (std::fgets(buffer, sizeof buffer, pipe)) text += buffer;
    const int status = ::pclose(pipe);
    if (output) *output = text;
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool command_ok(const std::string& command) {
    return command_ok(command + " >/dev/null 2>&1", nullptr);
}

bool digits_only(const std::string& text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string property_value(const std::string& blob, const char* key) {
    const std::string prefix = std::string(key) + "=";
    std::istringstream lines(blob);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return {};
}

// Sessions this process belongs to are often the IDE or a user manager, not
// the seat. GNOME (and other desktops) lock only when their own logind
// session receives the Lock signal.
std::vector<std::string> graphical_session_ids() {
    std::vector<std::string> ids;
    std::string listing;
    if (!command_ok("loginctl list-sessions --no-legend 2>/dev/null", &listing)) return ids;
    std::istringstream rows(listing);
    std::string row;
    while (std::getline(rows, row)) {
        std::istringstream fields(row);
        std::string id;
        if (!(fields >> id) || !digits_only(id)) continue;
        std::string info;
        if (!command_ok("loginctl show-session " + id + " -p Type -p Active -p State 2>/dev/null", &info)) {
            continue;
        }
        const std::string type = property_value(info, "Type");
        const std::string active = property_value(info, "Active");
        const std::string state = property_value(info, "State");
        const bool graphical = type == "wayland" || type == "x11" || type == "mir";
        const bool awake = active == "yes" || state == "active";
        if (graphical && awake) ids.push_back(id);
    }
    return ids;
}

} // namespace

bool request_session_lock(std::string& detail) {
    // The app owns the keypad, so the desktop shortcut never sees Tab.
    // Lock the seat session by id. `loginctl lock-session` with no id locks
    // the caller, which is not the screen the user is looking at.
    bool locked = false;
    for (const auto& id : graphical_session_ids()) {
        if (command_ok("loginctl lock-session " + id)) locked = true;
    }
    if (!locked) locked = command_ok("loginctl lock-session");
    if (!locked) locked = command_ok("loginctl lock-sessions");

    // Screensaver helpers are a backup for desktops that do not listen to
    // logind. gnome-shell's Lock method does not reply until the shield is
    // visible, so cap the wait and never run it ahead of loginctl.
    if (!locked) {
        locked = command_ok(
            "gdbus call --session --timeout 3 --dest org.gnome.ScreenSaver "
            "--object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.Lock");
    }
    if (!locked) {
        locked = command_ok(
            "gdbus call --session --timeout 3 --dest org.gnome.ScreenSaver "
            "--object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.SetActive true");
    }
    if (!locked) locked = command_ok("xdg-screensaver lock");
    if (!locked) locked = command_ok("swaylock -f");
    if (!locked) locked = command_ok("gtklock -d");
    if (!locked) locked = command_ok("hyprlock");
    if (!locked) locked = command_ok("waylock");

    if (!locked) detail = "Could not lock the session";
    return locked;
}

} // namespace platform

#include "power_ctrl.h"

#include "shell_util.h"

namespace platform {

bool safe_reboot(std::string& error) {
    error.clear();
#if USE_DESKTOP
    error = "not available on desktop";
    return false;
#else
    std::string out;
    const int rc = run_shell("systemctl reboot 2>&1", out);
    if (rc != 0) {
        error = out.empty() ? "reboot failed" : out;
        return false;
    }
    return true;
#endif
}

bool safe_shutdown(std::string& error) {
    error.clear();
#if USE_DESKTOP
    error = "not available on desktop";
    return false;
#else
    std::string out;
    const int rc = run_shell("systemctl poweroff 2>&1", out);
    if (rc != 0) {
        error = out.empty() ? "shutdown failed" : out;
        return false;
    }
    return true;
#endif
}

} // namespace platform

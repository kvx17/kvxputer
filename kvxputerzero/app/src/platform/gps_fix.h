#pragma once

#include <string>

namespace platform {

struct GpsFix {
    bool valid{false};
    double lat{0};
    double lon{0};
    std::string time_utc;
    std::string error;
};

// Wait up to timeout_ms for a 2D/3D fix via gpsd (localhost:2947) or NMEA tty path.
// Desktop / missing gpsd returns quickly with error set. cancel may stop early.
GpsFix wait_for_fix(int timeout_ms = 3000, bool* cancel = nullptr,
                    const std::string& nmea_tty = {});

} // namespace platform

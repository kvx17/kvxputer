#pragma once

#include <string>

namespace platform {

// One reading from the on-board motion sensors. Accel is in g, gyro in deg/s,
// mag in scaled units (microtesla-ish; good enough for a heading).
struct ImuSample {
    bool accel{false};
    float ax{0.f}, ay{0.f}, az{0.f};
    bool gyro{false};
    float gx{0.f}, gy{0.f}, gz{0.f};
    bool mag{false};
    float mx{0.f}, my{0.f}, mz{0.f};
};

struct ImuInfo {
    bool present{false};     // at least accel or mag is available
    bool has_accel{false};
    bool has_gyro{false};
    bool has_mag{false};
    std::string accel_name;  // IIO device name, e.g. "bmi270"
    std::string mag_name;    // e.g. "bmm150"
};

// Reads the Cardputer IMU/magnetometer over the Linux IIO sysfs. On the initial
// Kickstarter Cardputer Zero these enumerate natively; elsewhere open() reports
// nothing present and callers should show the Kickstarter-only notice.
class Imu {
public:
    bool open(std::string& error);
    bool present() const { return info_.present; }
    const ImuInfo& info() const { return info_; }

    // Fills out with whatever axes are available. Returns false only on a hard
    // read error; missing sensors simply leave their flags false.
    bool read(ImuSample& out, std::string& error);

private:
    std::string accel_dir_;
    std::string gyro_dir_;
    std::string mag_dir_;
    ImuInfo info_;
};

} // namespace platform

#include "imu.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace platform {
namespace {

namespace fs = std::filesystem;

bool read_file_string(const std::string& path, std::string& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::getline(in, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return true;
}

bool read_double(const std::string& path, double& out) {
    std::ifstream in(path);
    if (!in) return false;
    in >> out;
    return static_cast<bool>(in);
}

// Scaled SI value for one axis: (raw + offset) * scale. Falls back from
// per-axis scale/offset to the channel-shared file.
bool read_axis(const std::string& dir, const char* chan, char axis, double& value) {
    double raw = 0.0;
    if (!read_double(dir + "/in_" + chan + "_" + axis + "_raw", raw)) return false;
    double scale = 1.0, offset = 0.0;
    if (!read_double(dir + "/in_" + chan + "_" + axis + "_scale", scale))
        read_double(dir + "/in_" + chan + "_scale", scale);
    if (!read_double(dir + "/in_" + chan + "_" + axis + "_offset", offset))
        read_double(dir + "/in_" + chan + "_offset", offset);
    value = (raw + offset) * scale;
    return true;
}

bool has_channel(const std::string& dir, const char* chan) {
    std::error_code ec;
    return fs::exists(dir + "/in_" + chan + "_x_raw", ec);
}

} // namespace

bool Imu::open(std::string& error) {
    info_ = ImuInfo{};
    accel_dir_.clear();
    gyro_dir_.clear();
    mag_dir_.clear();

    const fs::path root = "/sys/bus/iio/devices";
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        error = "IIO sysfs unavailable";
        return false;
    }

    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (ec) break;
        const std::string name = ent.path().filename().string();
        if (name.rfind("iio:device", 0) != 0) continue;
        const std::string dir = ent.path().string();
        std::string dev_name;
        read_file_string(dir + "/name", dev_name);

        if (accel_dir_.empty() && has_channel(dir, "accel")) {
            accel_dir_ = dir;
            info_.has_accel = true;
            info_.accel_name = dev_name;
        }
        if (gyro_dir_.empty() && has_channel(dir, "anglvel")) {
            gyro_dir_ = dir;
            info_.has_gyro = true;
        }
        if (mag_dir_.empty() && has_channel(dir, "magn")) {
            mag_dir_ = dir;
            info_.has_mag = true;
            info_.mag_name = dev_name;
        }
    }

    info_.present = info_.has_accel || info_.has_mag;
    if (!info_.present) {
        error = "no IMU/magnetometer on IIO";
        return false;
    }
    return true;
}

bool Imu::read(ImuSample& out, std::string& error) {
    out = ImuSample{};
    if (!info_.present) {
        error = "IMU not present";
        return false;
    }

    if (!accel_dir_.empty()) {
        double x = 0, y = 0, z = 0;
        if (read_axis(accel_dir_, "accel", 'x', x) && read_axis(accel_dir_, "accel", 'y', y) &&
            read_axis(accel_dir_, "accel", 'z', z)) {
            // IIO accel scale yields m/s^2; convert to g.
            out.ax = static_cast<float>(x / 9.80665);
            out.ay = static_cast<float>(y / 9.80665);
            out.az = static_cast<float>(z / 9.80665);
            out.accel = true;
        }
    }
    if (!gyro_dir_.empty()) {
        double x = 0, y = 0, z = 0;
        if (read_axis(gyro_dir_, "anglvel", 'x', x) && read_axis(gyro_dir_, "anglvel", 'y', y) &&
            read_axis(gyro_dir_, "anglvel", 'z', z)) {
            // IIO anglvel scale yields rad/s; convert to deg/s.
            const double k = 180.0 / 3.14159265358979323846;
            out.gx = static_cast<float>(x * k);
            out.gy = static_cast<float>(y * k);
            out.gz = static_cast<float>(z * k);
            out.gyro = true;
        }
    }
    if (!mag_dir_.empty()) {
        double x = 0, y = 0, z = 0;
        if (read_axis(mag_dir_, "magn", 'x', x) && read_axis(mag_dir_, "magn", 'y', y) &&
            read_axis(mag_dir_, "magn", 'z', z)) {
            // IIO magn scale yields Gauss; *100 for microtesla.
            out.mx = static_cast<float>(x * 100.0);
            out.my = static_cast<float>(y * 100.0);
            out.mz = static_cast<float>(z * 100.0);
            out.mag = true;
        }
    }
    return out.accel || out.gyro || out.mag;
}

} // namespace platform

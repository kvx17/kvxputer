#include "gpio_line.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

} // namespace

GpioLine::GpioLine(std::string chip_path, unsigned offset)
    : chip_path_(std::move(chip_path)), offset_(offset) {}

GpioLine::~GpioLine() {
    release();
}

GpioLine::GpioLine(GpioLine&& o) noexcept
    : chip_path_(std::move(o.chip_path_)), offset_(o.offset_), req_fd_(o.req_fd_) {
    o.req_fd_ = -1;
}

GpioLine& GpioLine::operator=(GpioLine&& o) noexcept {
    if (this == &o) return *this;
    release();
    chip_path_ = std::move(o.chip_path_);
    offset_ = o.offset_;
    req_fd_ = o.req_fd_;
    o.req_fd_ = -1;
    return *this;
}

void GpioLine::release() {
    if (req_fd_ >= 0) {
        ::close(req_fd_);
        req_fd_ = -1;
    }
}

bool GpioLine::request_output(bool initial_high, std::string& error) {
    error.clear();
    release();
    if (kDesktop) {
        error = "GPIO unavailable";
        return false;
    }
    const int chip = ::open(chip_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (chip < 0) {
        error = "open " + chip_path_ + " failed";
        return false;
    }
    gpio_v2_line_request req{};
    std::snprintf(req.consumer, sizeof(req.consumer), "kvxputerzero");
    req.num_lines = 1;
    req.offsets[0] = offset_;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1;
    req.config.attrs[0].mask = 1ULL;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    if (initial_high) req.config.attrs[0].attr.values = 1ULL;
    if (::ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        error = std::string("GPIO request output failed: ") + std::strerror(errno);
        ::close(chip);
        return false;
    }
    ::close(chip);
    req_fd_ = req.fd;
    return true;
}

bool GpioLine::request_input(std::string& error) {
    error.clear();
    release();
    if (kDesktop) {
        error = "GPIO unavailable";
        return false;
    }
    const int chip = ::open(chip_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (chip < 0) {
        error = "open " + chip_path_ + " failed";
        return false;
    }
    gpio_v2_line_request req{};
    std::snprintf(req.consumer, sizeof(req.consumer), "kvxputerzero");
    req.num_lines = 1;
    req.offsets[0] = offset_;
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT;
    if (::ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        error = std::string("GPIO request input failed: ") + std::strerror(errno);
        ::close(chip);
        return false;
    }
    ::close(chip);
    req_fd_ = req.fd;
    return true;
}

bool GpioLine::set_value(bool high, std::string& error) {
    error.clear();
    if (req_fd_ < 0) {
        error = "GPIO not requested";
        return false;
    }
    gpio_v2_line_values vals{};
    vals.mask = 1ULL;
    vals.bits = high ? 1ULL : 0ULL;
    if (::ioctl(req_fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
        error = std::string("GPIO set failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool GpioLine::get_value(bool& high, std::string& error) const {
    error.clear();
    if (req_fd_ < 0) {
        error = "GPIO not requested";
        return false;
    }
    gpio_v2_line_values vals{};
    vals.mask = 1ULL;
    if (::ioctl(req_fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
        error = std::string("GPIO get failed: ") + std::strerror(errno);
        return false;
    }
    high = (vals.bits & 1ULL) != 0;
    return true;
}

} // namespace platform

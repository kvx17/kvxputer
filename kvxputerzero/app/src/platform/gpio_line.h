#pragma once

#include <string>

namespace platform {

// Thin /dev/gpiochip* GPIO_V2 wrapper (no libgpiod). Desktop: no-ops that fail clearly.
class GpioLine {
public:
    GpioLine() = default;
    GpioLine(std::string chip_path, unsigned offset);
    ~GpioLine();

    GpioLine(const GpioLine&) = delete;
    GpioLine& operator=(const GpioLine&) = delete;
    GpioLine(GpioLine&&) noexcept;
    GpioLine& operator=(GpioLine&&) noexcept;

    bool request_output(bool initial_high, std::string& error);
    bool request_input(std::string& error);
    bool set_value(bool high, std::string& error);
    bool get_value(bool& high, std::string& error) const;
    void release();
    bool ok() const { return req_fd_ >= 0; }
    unsigned offset() const { return offset_; }

private:
    std::string chip_path_{"/dev/gpiochip0"};
    unsigned offset_{0};
    int req_fd_{-1};
};

} // namespace platform

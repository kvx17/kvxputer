#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct GpioEdgeEvent {
    uint64_t timestamp_ns{0};
    bool rising{false};
};

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
    // Input + both-edge events with kernel timestamps (GPIO_V2).
    bool request_edge_events(std::string& error);
    bool set_value(bool high, std::string& error);
    bool get_value(bool& high, std::string& error) const;
    // Non-blocking drain of pending edge events into out (appended).
    bool read_edge_events(std::vector<GpioEdgeEvent>& out, std::string& error);
    void release();
    bool ok() const { return req_fd_ >= 0; }
    int fd() const { return req_fd_; }
    unsigned offset() const { return offset_; }

private:
    std::string chip_path_{"/dev/gpiochip0"};
    unsigned offset_{0};
    int req_fd_{-1};
};

} // namespace platform

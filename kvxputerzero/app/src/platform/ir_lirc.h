#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace platform {

struct IrDeviceInfo {
    bool present{false};
    std::string path;
    std::string name;
    std::string error;
};

struct IrPacket {
    uint16_t address{0};
    uint16_t command{0};
    bool valid{false};
};

IrDeviceInfo read_receiver_info();
IrDeviceInfo read_sender_info();

struct IrSendResult {
    bool ok{false};
    std::string error;
};

IrSendResult send_nec_packet(uint16_t address, uint16_t command);

// Pulse/space pairs in microseconds (mark, space, mark, space, ...). Carrier in Hz.
IrSendResult send_raw(const uint32_t* pulses_us, size_t count, uint32_t carrier_hz = 38000);

// Hold a continuous IR carrier for up to duration_ms (or until *cancel). Default 38 kHz.
IrSendResult send_carrier(uint32_t carrier_hz, int duration_ms, bool* cancel = nullptr);

class IrReceiverSession {
public:
    IrReceiverSession() = default;
    ~IrReceiverSession();

    IrReceiverSession(const IrReceiverSession&) = delete;
    IrReceiverSession& operator=(const IrReceiverSession&) = delete;

    bool start(uint16_t expected_address = 0, bool filter_address = false);
    void stop();
    IrPacket poll();
    bool running() const;
    const std::string& last_error() const;

private:
    int fd_{-1};
    bool use_scancode_{false};
    uint16_t expected_address_{0};
    bool filter_address_{false};
    std::string last_error_;
    // MODE2 decode scratch (pulse/space durations in us).
    unsigned int mode2_buf_[128]{};
    size_t mode2_len_{0};
};

} // namespace platform

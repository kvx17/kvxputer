#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform {

// One full-sensor still, packed as I420 (Y, then U, then V) at `stride`.
struct SensorStill {
    int width{0};
    int height{0};
    int stride{0};
    std::vector<uint8_t> i420;
    std::vector<uint8_t> rgb888;
};

// Thin CSI preview wrapper. Built when KVX_HAVE_LIBCAMERA=1; otherwise start() fails.
class LibcameraPreview {
public:
    LibcameraPreview();
    ~LibcameraPreview();

    LibcameraPreview(const LibcameraPreview&) = delete;
    LibcameraPreview& operator=(const LibcameraPreview&) = delete;

    bool start(std::string& error);
    void stop();
    bool running() const;

    // True while the viewfinder is running and a still can be taken.
    bool still_ready() const;

    // Stop the viewfinder, capture one full-sensor still, then restore the viewfinder.
    bool capture_sensor_still(SensorStill& out, std::string& error);

    // Block until the next still frame is converted to RGB888 at sensor size.
    bool capture_rgb888(std::vector<uint8_t>& rgb, int& width, int& height, std::string& error);

    // Latest converted RGB565 frame (may be scaled toward 320-wide).
    bool copy_frame(std::vector<uint16_t>& rgb565, int& width, int& height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform

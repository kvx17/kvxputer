#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform {

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

    // Latest converted RGB565 frame (may be scaled toward 320-wide).
    bool copy_frame(std::vector<uint16_t>& rgb565, int& width, int& height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform

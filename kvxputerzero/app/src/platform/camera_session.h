#pragma once

#include "camera_filter.h"
#include "camera_libcamera.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {

struct CameraFrame {
    int width{0};
    int height{0};
    std::vector<uint16_t> rgb565;
};

// Live camera session: desktop simulation, optional device CSI/V4L2.
class CameraSession {
public:
    CameraSession();
    ~CameraSession();

    CameraSession(const CameraSession&) = delete;
    CameraSession& operator=(const CameraSession&) = delete;

    bool start(std::string& error);
    void stop();
    bool running() const { return running_.load(); }

    // "simulation", "libcamera", "v4l2", or "none"
    const std::string& backend_name() const { return backend_; }

    void set_filter(tools::camfilter::Filter filter);
    tools::camfilter::Filter filter() const;

    // Software gain when hardware brightness is unavailable. Range ~0.4..2.0
    void set_brightness(float gain);
    float brightness() const;

    bool copy_preview(CameraFrame& out) const;

    // Save filtered still under ~/Pictures/DCIM/Camera. Returns path or empty + error.
    bool capture_still(std::string& out_path, std::string& error);

    bool start_recording(std::string& out_path, std::string& error);
    void stop_recording();
    bool recording() const { return recording_.load(); }
    const std::string& record_path() const { return record_path_; }

private:
    void worker_loop();
    void generate_simulation_frame(std::vector<uint16_t>& out, int w, int h, uint32_t tick);
    bool open_libcamera(std::string& error);
    void close_libcamera();
    bool grab_libcamera(std::vector<uint16_t>& out, int& w, int& h);
    bool open_v4l2(std::string& error);
    bool open_v4l2_device(const std::string& path, std::string& error);
    void close_v4l2();
    bool grab_v4l2(std::vector<uint16_t>& out, int& w, int& h);
    bool encode_jpeg_rgb888(const std::vector<uint8_t>& rgb, int w, int h, const std::string& path,
                            std::string& error);
    void rgb565_to_rgb888(const std::vector<uint16_t>& src, std::vector<uint8_t>& dst);
    bool append_avi_frame(const std::vector<uint8_t>& jpeg);
    bool finish_avi();

    std::string backend_{"none"};
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::thread worker_;

    mutable std::mutex frame_mu_;
    CameraFrame latest_;
    std::atomic<bool> frame_ready_{false};

    std::atomic<int> filter_{static_cast<int>(tools::camfilter::Filter::Original)};
    std::atomic<float> brightness_{1.0f};

    LibcameraPreview libcamera_;

    // V4L2 (USB UVC fallback). Supports multiple mmap buffers.
    int v4l2_fd_{-1};
    int v4l2_w_{0};
    int v4l2_h_{0};
    uint32_t v4l2_pixfmt_{0};
    struct V4l2Buf {
        void* start{nullptr};
        size_t length{0};
    };
    std::vector<V4l2Buf> v4l2_bufs_;

    // Recording
    std::atomic<bool> recording_{false};
    std::string record_path_;
    FILE* avi_fp_{nullptr};
    uint32_t avi_frames_{0};
    long avi_movi_list_pos_{0};
    long avi_frames_pos_{0};
    long avi_strh_len_pos_{0};
    int avi_w_{0};
    int avi_h_{0};
    struct AviChunk {
        uint32_t offset{0};
        uint32_t size{0};
    };
    std::vector<AviChunk> avi_index_;
    std::mutex record_mu_;
    uint32_t tick_{0};
};

// Decode a still image (JPEG/PNG) into RGB565, optionally downscaling to max_pixels.
bool decode_image_rgb565(const std::string& path, std::vector<uint16_t>& out, int& width, int& height,
                         std::string& error, int max_pixels = 320 * 240 * 8);

// Decode a JPEG buffer (for example one MJPEG AVI frame) into RGB565.
bool decode_jpeg_rgb565(const uint8_t* data, size_t size, std::vector<uint16_t>& out, int& width, int& height,
                        std::string& error, int max_pixels = 320 * 240 * 8);

std::string pictures_dir();
std::string camera_stills_dir();
std::string camera_videos_dir();

} // namespace platform

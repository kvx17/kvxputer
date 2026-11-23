#include "camera_libcamera.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libcamera/libcamera.h>
#endif

namespace platform {
namespace {

uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

uint8_t clip_u8(int v) {
    return static_cast<uint8_t>(std::max(0, std::min(255, v)));
}

void yuv_to_rgb_fast(int y, int u, int v, uint8_t& r, uint8_t& g, uint8_t& b) {
    const int yy = y;
    const int uu = u - 128;
    const int vv = v - 128;
    r = clip_u8(yy + ((1436 * vv) >> 10));
    g = clip_u8(yy - ((352 * uu + 731 * vv) >> 10));
    b = clip_u8(yy + ((1815 * uu) >> 10));
}

#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
void sync_dma_buf(int fd, uint64_t flags) {
    if (fd < 0) return;
    struct dma_buf_sync sync {};
    sync.flags = flags;
    (void)::ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

bool format_supported(const libcamera::PixelFormat& format) {
    return format == libcamera::formats::YUV420 || format == libcamera::formats::NV12 ||
           format == libcamera::formats::YUYV || format == libcamera::formats::UYVY ||
           format == libcamera::formats::RGB565 || format == libcamera::formats::RGB888 ||
           format == libcamera::formats::BGR888 || format == libcamera::formats::XRGB8888 ||
           format == libcamera::formats::XBGR8888;
}
#endif

} // namespace

#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA

// FXCam-style CSI path. Do NOT inherit libcamera::Object: that queues
// requestCompleted onto the object's thread, and we never run that dispatcher,
// so frames never arrive (black preview / "no preview frame"). FXCam uses a
// plain class so the slot runs directly on the camera thread.
struct LibcameraPreview::Impl {
    std::unique_ptr<libcamera::CameraManager> manager;
    std::shared_ptr<libcamera::Camera> camera;
    std::unique_ptr<libcamera::CameraConfiguration> config;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
    libcamera::Stream* stream{nullptr};
    std::vector<std::unique_ptr<libcamera::Request>> requests;

    struct MappedPlane {
        void* addr{nullptr};
        size_t size{0};
        int fd{-1};
        size_t data_offset{0};
        size_t data_size{0};
    };
    struct MappedBuffer {
        std::vector<MappedPlane> planes;
    };
    std::map<const libcamera::FrameBuffer*, MappedBuffer> mapped;

    libcamera::PixelFormat pixel_format{libcamera::formats::RGB565};
    int width{0};
    int height{0};
    int stride{0};
    bool streaming{false};
    bool software_rotate180{false};

    libcamera::Stream* still_stream{nullptr};
    libcamera::Stream* raw_stream{nullptr};
    std::vector<libcamera::FrameBuffer*> free_still;
    libcamera::PixelFormat still_format{libcamera::formats::YUV420};
    int still_w{0};
    int still_h{0};
    int still_stride{0};
    bool still_mode{false};
    bool still_as_rgb{false};
    int still_seen{0};
    int live_w{1640};
    int live_h{1232};
    int dma_heap_{-1};
    std::vector<std::unique_ptr<libcamera::FrameBuffer>> owned_buffers;
    std::vector<uint8_t> still_i420;

    std::mutex still_mu;
    std::condition_variable still_cv;
    bool still_busy{false};
    bool still_done{false};
    bool still_ok{false};
    std::string still_error;
    std::vector<uint8_t> still_rgb;
    std::atomic<bool> capture_requested{false};

    std::mutex frame_mu;
    std::vector<uint16_t> latest;
    int latest_w{0};
    int latest_h{0};
    bool frame_ready{false};

    static constexpr int64_t kPreviewMinFrameDurationUs = 16667;
    static constexpr int64_t kPreviewMaxFrameDurationUs = 33333;

    ~Impl() { stop(); }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(still_mu);
            if (!still_done) still_error = "camera stopped";
            still_ok = false;
            still_done = true;
            still_busy = false;
        }
        still_cv.notify_all();
        capture_requested = false;
        still_mode = false;
        if (camera && streaming) {
            camera->requestCompleted.disconnect(this);
            camera->stop();
            streaming = false;
        }
        requests.clear();
        for (auto& item : mapped) {
            for (auto& plane : item.second.planes) {
                if (plane.addr && plane.addr != MAP_FAILED) ::munmap(plane.addr, plane.size);
            }
        }
        mapped.clear();
        owned_buffers.clear();
        allocator.reset();
        config.reset();
        stream = nullptr;
        still_stream = nullptr;
        raw_stream = nullptr;
        free_still.clear();
        if (dma_heap_ >= 0) {
            ::close(dma_heap_);
            dma_heap_ = -1;
        }
        if (camera) {
            camera->release();
            camera.reset();
        }
        if (manager) {
            manager->stop();
            manager.reset();
        }
    }

    void apply_request_controls(libcamera::Request* request) {
        if (!request) return;
        if (still_mode) {
            // Let the still use the sensor's own exposure, the same window the stock camera uses.
            request->controls().set(libcamera::controls::FrameDurationLimits,
                                    {static_cast<int64_t>(100), static_cast<int64_t>(1000000000)});
        } else {
            request->controls().set(libcamera::controls::FrameDurationLimits,
                                    {kPreviewMinFrameDurationUs, kPreviewMaxFrameDurationUs});
            // Keep the viewfinder on the full sensor, the same field the still records.
            if (camera) {
                if (auto max_crop = camera->properties().get(libcamera::properties::ScalerCropMaximum)) {
                    request->controls().set(libcamera::controls::ScalerCrop, *max_crop);
                }
            }
        }
    }

    bool map_buffer(const libcamera::FrameBuffer* buffer) {
        MappedBuffer mapped_buf;
        const auto planes = buffer->planes();
        if (planes.empty()) return false;
        for (const auto& plane : planes) {
            const long page = ::sysconf(_SC_PAGE_SIZE);
            const size_t page_size = page > 0 ? static_cast<size_t>(page) : 4096;
            const size_t plane_offset = static_cast<size_t>(plane.offset);
            const size_t map_offset = plane_offset & ~(page_size - 1);
            const size_t data_offset = plane_offset - map_offset;
            const size_t map_length = data_offset + static_cast<size_t>(plane.length);
            void* memory = ::mmap(nullptr, map_length, PROT_READ | PROT_WRITE, MAP_SHARED, plane.fd.get(),
                                  static_cast<off_t>(map_offset));
            if (memory == MAP_FAILED) return false;
            mapped_buf.planes.push_back({memory, map_length, plane.fd.get(), data_offset,
                                         static_cast<size_t>(plane.length)});
        }
        if (mapped_buf.planes.size() != planes.size()) return false;
        mapped[buffer] = std::move(mapped_buf);
        return true;
    }

    bool convert_to_rgb565(const std::vector<const uint8_t*>& plane_ptrs,
                           const std::vector<size_t>& bytes_used, std::vector<uint16_t>& out, int& dst_w,
                           int& dst_h) {
        const int src_w = width;
        const int src_h = height;
        if (src_w <= 0 || src_h <= 0 || plane_ptrs.empty() || !plane_ptrs[0] || bytes_used.empty()) {
            return false;
        }

        dst_w = src_w;
        dst_h = src_h;
        if (dst_w > 320) {
            dst_h = std::max(1, dst_h * 320 / dst_w);
            dst_w = 320;
        }
        out.assign(static_cast<size_t>(dst_w * dst_h), 0);

        auto put = [&](int dx, int dy, uint8_t r, uint8_t g, uint8_t b) {
            // Software rotate only when the pipeline did not apply Rotate180 (FXCam).
            if (software_rotate180) {
                dx = dst_w - 1 - dx;
                dy = dst_h - 1 - dy;
            }
            out[static_cast<size_t>(dy * dst_w + dx)] = pack_rgb565(r, g, b);
        };

        auto sample_src = [&](int sx, int sy, uint8_t& r, uint8_t& g, uint8_t& b) -> bool {
            if (sx < 0 || sy < 0 || sx >= src_w || sy >= src_h) return false;

            if (pixel_format == libcamera::formats::RGB565) {
                const int row_stride = stride > 0 ? stride : src_w * 2;
                if (static_cast<size_t>(sy) * row_stride + sx * 2 + 1 >= bytes_used[0]) return false;
                const auto* line = plane_ptrs[0] + static_cast<size_t>(sy) * row_stride;
                const uint16_t p = static_cast<uint16_t>(line[sx * 2] | (line[sx * 2 + 1] << 8));
                r = static_cast<uint8_t>(((p >> 11) & 0x1f) * 255 / 31);
                g = static_cast<uint8_t>(((p >> 5) & 0x3f) * 255 / 63);
                b = static_cast<uint8_t>((p & 0x1f) * 255 / 31);
                return true;
            }

            if (pixel_format == libcamera::formats::RGB888 || pixel_format == libcamera::formats::BGR888) {
                const int bpp = 3;
                const int row_stride = stride > 0 ? stride : src_w * bpp;
                if (static_cast<size_t>(sy) * row_stride + sx * bpp + 2 >= bytes_used[0]) return false;
                const auto* p = plane_ptrs[0] + static_cast<size_t>(sy) * row_stride + sx * bpp;
                if (pixel_format == libcamera::formats::RGB888) {
                    r = p[0];
                    g = p[1];
                    b = p[2];
                } else {
                    b = p[0];
                    g = p[1];
                    r = p[2];
                }
                return true;
            }

            if (pixel_format == libcamera::formats::XRGB8888 ||
                pixel_format == libcamera::formats::XBGR8888) {
                const int bpp = 4;
                const int row_stride = stride > 0 ? stride : src_w * bpp;
                if (static_cast<size_t>(sy) * row_stride + sx * bpp + 2 >= bytes_used[0]) return false;
                const auto* p = plane_ptrs[0] + static_cast<size_t>(sy) * row_stride + sx * bpp;
                // DRM little-endian: XRGB8888 bytes are B,G,R,X ; XBGR8888 are R,G,B,X
                if (pixel_format == libcamera::formats::XRGB8888) {
                    b = p[0];
                    g = p[1];
                    r = p[2];
                } else {
                    r = p[0];
                    g = p[1];
                    b = p[2];
                }
                return true;
            }

            if (pixel_format == libcamera::formats::YUYV || pixel_format == libcamera::formats::UYVY) {
                const int row_stride = stride > 0 ? stride : src_w * 2;
                if (static_cast<size_t>(sy) * row_stride + (sx & ~1) * 2 + 3 >= bytes_used[0]) return false;
                const auto* line = plane_ptrs[0] + static_cast<size_t>(sy) * row_stride;
                const int x2 = sx & ~1;
                const uint8_t b0 = line[x2 * 2];
                const uint8_t b1 = line[x2 * 2 + 1];
                const uint8_t b2 = line[x2 * 2 + 2];
                const uint8_t b3 = line[x2 * 2 + 3];
                const bool yuyv = pixel_format == libcamera::formats::YUYV;
                const uint8_t y = (sx & 1) ? (yuyv ? b2 : b3) : (yuyv ? b0 : b1);
                const uint8_t u = yuyv ? b1 : b0;
                const uint8_t v = yuyv ? b3 : b2;
                yuv_to_rgb_fast(y, u, v, r, g, b);
                return true;
            }

            // YUV420 planar or NV12 (semi-planar).
            const int y_stride = stride > 0 ? stride : src_w;
            const uint8_t* y_plane = plane_ptrs[0];
            if (static_cast<size_t>(sy) * y_stride + sx >= bytes_used[0]) return false;
            const int yy = y_plane[sy * y_stride + sx];

            if (pixel_format == libcamera::formats::NV12 ||
                (plane_ptrs.size() == 2 && pixel_format != libcamera::formats::YUV420)) {
                const uint8_t* uv_plane = nullptr;
                int uv_stride = std::max(1, y_stride);
                if (plane_ptrs.size() >= 2) {
                    uv_plane = plane_ptrs[1];
                    uv_stride = std::max(1, y_stride);
                } else {
                    const size_t y_size = static_cast<size_t>(y_stride) * src_h;
                    if (bytes_used[0] < y_size + static_cast<size_t>(uv_stride) * ((src_h + 1) / 2)) {
                        return false;
                    }
                    uv_plane = y_plane + y_size;
                }
                const int uv_index = (sy / 2) * uv_stride + (sx & ~1);
                const uint8_t u = uv_plane[uv_index];
                const uint8_t v = uv_plane[uv_index + 1];
                yuv_to_rgb_fast(yy, u, v, r, g, b);
                return true;
            }

            // Planar YUV420
            const int uv_stride = std::max(1, y_stride / 2);
            const size_t y_size = static_cast<size_t>(y_stride) * src_h;
            const size_t uv_size = static_cast<size_t>(uv_stride) * ((src_h + 1) / 2);
            const uint8_t* u_plane = nullptr;
            const uint8_t* v_plane = nullptr;
            if (plane_ptrs.size() >= 3) {
                u_plane = plane_ptrs[1];
                v_plane = plane_ptrs[2];
            } else if (bytes_used[0] >= y_size + uv_size * 2) {
                u_plane = y_plane + y_size;
                v_plane = u_plane + uv_size;
            } else {
                return false;
            }
            yuv_to_rgb_fast(yy, u_plane[(sy / 2) * uv_stride + (sx / 2)],
                            v_plane[(sy / 2) * uv_stride + (sx / 2)], r, g, b);
            return true;
        };

        // Fast path: full-size RGB565 copy when no scale/rotate.
        if (pixel_format == libcamera::formats::RGB565 && dst_w == src_w && dst_h == src_h &&
            !software_rotate180) {
            const int row_stride = stride > 0 ? stride : src_w * 2;
            auto* dst = reinterpret_cast<uint8_t*>(out.data());
            for (int y = 0; y < src_h; ++y) {
                const size_t offset = static_cast<size_t>(y) * row_stride;
                if (offset + static_cast<size_t>(src_w) * 2 > bytes_used[0]) return false;
                std::memcpy(dst + static_cast<size_t>(y) * src_w * 2, plane_ptrs[0] + offset,
                            static_cast<size_t>(src_w) * 2);
            }
            return true;
        }

        for (int dy = 0; dy < dst_h; ++dy) {
            for (int dx = 0; dx < dst_w; ++dx) {
                const int sx = dx * src_w / dst_w;
                const int sy = dy * src_h / dst_h;
                uint8_t r = 0, g = 0, b = 0;
                (void)sample_src(sx, sy, r, g, b);
                put(dx, dy, r, g, b);
            }
        }
        return true;
    }

    bool convert_request(libcamera::Request* request) {
        if (!request || !stream) return false;
        auto it = request->buffers().find(stream);
        if (it == request->buffers().end()) return false;
        const libcamera::FrameBuffer* buffer = it->second;
        auto map_it = mapped.find(buffer);
        if (map_it == mapped.end() || map_it->second.planes.empty()) return false;

        const auto& mapped_planes = map_it->second.planes;
        std::vector<const uint8_t*> plane_ptrs;
        std::vector<size_t> bytes_used;
        plane_ptrs.reserve(mapped_planes.size());
        bytes_used.reserve(mapped_planes.size());
        for (const auto& plane : mapped_planes) {
            plane_ptrs.push_back(static_cast<const uint8_t*>(plane.addr) + plane.data_offset);
            bytes_used.push_back(plane.data_size);
        }

        // Prefer metadata bytesused when present (FXCam), else plane length.
        const auto& meta = buffer->metadata().planes();
        if (!meta.empty()) {
            const size_t n = std::min(meta.size(), bytes_used.size());
            for (size_t i = 0; i < n; ++i) {
                if (meta[i].bytesused > 0) {
                    bytes_used[i] = std::min(bytes_used[i], static_cast<size_t>(meta[i].bytesused));
                }
            }
        }

        for (const auto& plane : mapped_planes) {
            sync_dma_buf(plane.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
        }

        std::vector<uint16_t> out;
        int dst_w = 0;
        int dst_h = 0;
        const bool ok = convert_to_rgb565(plane_ptrs, bytes_used, out, dst_w, dst_h);

        for (const auto& plane : mapped_planes) {
            sync_dma_buf(plane.fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
        }

        if (!ok || out.empty()) return false;

        std::lock_guard<std::mutex> lock(frame_mu);
        latest = std::move(out);
        latest_w = dst_w;
        latest_h = dst_h;
        frame_ready = true;
        return true;
    }

    void fail_still(const std::string& error) {
        std::lock_guard<std::mutex> lock(still_mu);
        still_ok = false;
        still_error = error;
        still_done = true;
        still_cv.notify_all();
    }

    void deliver_still(const libcamera::FrameBuffer* buffer) {
        std::vector<uint8_t> rgb;
        std::string error;
        const bool ok = convert_still(buffer, rgb, error);
        {
            std::lock_guard<std::mutex> lock(still_mu);
            if (ok) {
                still_rgb = std::move(rgb);
                still_ok = true;
                still_error.clear();
            } else {
                still_ok = false;
                still_error = error.empty() ? "still convert failed" : error;
            }
            still_done = true;
        }
        still_cv.notify_all();
    }

    void request_complete(libcamera::Request* request) {
        if (!request || request->status() == libcamera::Request::RequestCancelled) return;
        if (!streaming || !camera) return;

        if (still_mode) {
            if (request->status() != libcamera::Request::RequestComplete) return;
            libcamera::FrameBuffer* still = still_stream ? request->findBuffer(still_stream) : nullptr;
            if (!still) return;
            // The first frames after a mode switch are still settling exposure. Keep the third.
            if (++still_seen < 3) {
                request->reuse(libcamera::Request::ReuseBuffers);
                apply_request_controls(request);
                if (streaming && camera) camera->queueRequest(request);
                return;
            }
            std::vector<uint8_t> packed;
            std::vector<uint8_t> rgb;
            std::string pack_error;
            bool packed_ok = false;
            if (still_as_rgb) {
                packed_ok = convert_still(still, rgb, pack_error);
            } else {
                packed_ok = pack_i420(still, packed, pack_error);
            }
            {
                std::lock_guard<std::mutex> lock(still_mu);
                still_ok = packed_ok;
                still_error = pack_error;
                if (packed_ok && still_as_rgb) still_rgb = std::move(rgb);
                if (packed_ok && !still_as_rgb) still_i420 = std::move(packed);
                still_done = true;
            }
            still_cv.notify_all();
            return;
        }

        libcamera::FrameBuffer* preview = stream ? request->findBuffer(stream) : nullptr;
        libcamera::FrameBuffer* still = still_stream ? request->findBuffer(still_stream) : nullptr;
        if (request->status() == libcamera::Request::RequestComplete && preview) {
            (void)convert_request(request);
        }
        if (request->status() == libcamera::Request::RequestComplete && still) {
            deliver_still(still);
        }

        // Drop buffers so a still frame is attached only for the shutter, not every preview.
        request->reuse();
        if (still) free_still.push_back(still);
        if (preview && request->addBuffer(stream, preview) < 0) return;

        if (capture_requested.exchange(false) && still_stream && !free_still.empty()) {
            libcamera::FrameBuffer* buffer = free_still.back();
            free_still.pop_back();
            if (request->addBuffer(still_stream, buffer) < 0) {
                free_still.push_back(buffer);
                fail_still("cannot queue still");
            }
        }

        apply_request_controls(request);
        if (streaming && camera) camera->queueRequest(request);
    }

    void release_streams() {
        still_mode = false;
        if (camera && streaming) {
            camera->requestCompleted.disconnect(this);
            camera->stop();
            streaming = false;
        }
        requests.clear();
        for (auto& item : mapped) {
            for (auto& plane : item.second.planes) {
                if (plane.addr && plane.addr != MAP_FAILED) ::munmap(plane.addr, plane.size);
            }
        }
        mapped.clear();
        owned_buffers.clear();
        allocator.reset();
        config.reset();
        stream = nullptr;
        still_stream = nullptr;
        raw_stream = nullptr;
        free_still.clear();
    }

    bool configure_and_start(int want_w, int want_h, std::string& error) {
        release_streams();

        config = camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
        if (!config || config->empty()) {
            error = "generateConfiguration failed";
            return false;
        }

        // Match FXCam: request RGB565 viewfinder; accept validated fallback formats.
        config->orientation = libcamera::Orientation::Rotate180;
        auto& cfg = config->at(0);
        cfg.size.width = static_cast<unsigned int>(want_w);
        cfg.size.height = static_cast<unsigned int>(want_h);
        cfg.pixelFormat = libcamera::formats::RGB565;
        cfg.bufferCount = 4;
        if (config->validate() == libcamera::CameraConfiguration::Invalid) {
            error = "invalid camera configuration";
            return false;
        }
        if (!format_supported(cfg.pixelFormat)) {
            // Retry with YUV420 if RGB565 was rewritten to something exotic.
            cfg.pixelFormat = libcamera::formats::YUV420;
            if (config->validate() == libcamera::CameraConfiguration::Invalid ||
                !format_supported(cfg.pixelFormat)) {
                error = "unsupported pixel format after validate";
                return false;
            }
        }
        if (camera->configure(config.get()) < 0) {
            error = "camera configure failed";
            return false;
        }

        // If the pipeline accepted Rotate180, do not also flip in software.
        software_rotate180 = (config->orientation != libcamera::Orientation::Rotate180);
        stream = cfg.stream();
        width = static_cast<int>(cfg.size.width);
        height = static_cast<int>(cfg.size.height);
        stride = static_cast<int>(cfg.stride);
        pixel_format = cfg.pixelFormat;

        allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
        if (allocator->allocate(stream) < 0) {
            error = "framebuffer allocate failed";
            return false;
        }
        for (const auto& buffer : allocator->buffers(stream)) {
            if (!map_buffer(buffer.get())) continue;
            auto request = camera->createRequest();
            if (!request || request->addBuffer(stream, buffer.get()) < 0) continue;
            apply_request_controls(request.get());
            requests.push_back(std::move(request));
        }
        if (requests.empty()) {
            error = "no camera requests";
            return false;
        }
        camera->requestCompleted.connect(this, &Impl::request_complete);
        if (camera->start()) {
            camera->requestCompleted.disconnect(this);
            error = "camera start failed";
            return false;
        }
        streaming = true;
        // A validated size far below the request is the cropped 640x480 sensor mode.
        if (want_w >= 1280 && width * 4 < want_w * 3) {
            error = "preview mode cropped";
            release_streams();
            return false;
        }
        live_w = want_w;
        live_h = want_h;
        for (auto& request : requests) camera->queueRequest(request.get());
        return true;
    }

    bool still_ready() const {
        return streaming && camera && !still_mode;
    }

    bool convert_still(const libcamera::FrameBuffer* buffer, std::vector<uint8_t>& rgb, std::string& error) {
        if (!buffer || still_w <= 0 || still_h <= 0) {
            error = "still stream is empty";
            return false;
        }
        auto map_it = mapped.find(buffer);
        if (map_it == mapped.end() || map_it->second.planes.empty()) {
            error = "still buffer not mapped";
            return false;
        }
        const auto& mapped_planes = map_it->second.planes;
        std::vector<const uint8_t*> plane_ptrs;
        std::vector<size_t> bytes_used;
        plane_ptrs.reserve(mapped_planes.size());
        bytes_used.reserve(mapped_planes.size());
        for (const auto& plane : mapped_planes) {
            plane_ptrs.push_back(static_cast<const uint8_t*>(plane.addr) + plane.data_offset);
            bytes_used.push_back(plane.data_size);
        }
        const auto& meta = buffer->metadata().planes();
        if (!meta.empty()) {
            const size_t n = std::min(meta.size(), bytes_used.size());
            for (size_t i = 0; i < n; ++i) {
                if (meta[i].bytesused > 0) {
                    bytes_used[i] = std::min(bytes_used[i], static_cast<size_t>(meta[i].bytesused));
                }
            }
        }
        for (const auto& plane : mapped_planes) {
            sync_dma_buf(plane.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
        }

        bool ok = false;
        try {
            const int w = still_w;
            const int h = still_h;
            rgb.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 3, 0);
            const bool rot = software_rotate180;
            auto store = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                if (rot) {
                    x = w - 1 - x;
                    y = h - 1 - y;
                }
                uint8_t* px = rgb.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
                px[0] = r;
                px[1] = g;
                px[2] = b;
            };

            if (still_format == libcamera::formats::YUV420 || still_format == libcamera::formats::NV12) {
                const int y_stride = still_stride > 0 ? still_stride : w;
                const uint8_t* y_plane = plane_ptrs[0];
                const bool nv12 = still_format == libcamera::formats::NV12 || plane_ptrs.size() == 2;
                const uint8_t* u_plane = nullptr;
                const uint8_t* v_plane = nullptr;
                const uint8_t* uv_plane = nullptr;
                const int uv_stride = std::max(1, y_stride / 2);
                if (nv12) {
                    if (plane_ptrs.size() >= 2) {
                        uv_plane = plane_ptrs[1];
                    } else {
                        uv_plane = y_plane + static_cast<size_t>(y_stride) * static_cast<size_t>(h);
                    }
                } else if (plane_ptrs.size() >= 3) {
                    u_plane = plane_ptrs[1];
                    v_plane = plane_ptrs[2];
                } else {
                    const size_t y_size = static_cast<size_t>(y_stride) * static_cast<size_t>(h);
                    const size_t uv_size = static_cast<size_t>(uv_stride) * static_cast<size_t>((h + 1) / 2);
                    if (bytes_used[0] < y_size + uv_size * 2) {
                        error = "short yuv frame";
                        ok = false;
                    } else {
                        u_plane = y_plane + y_size;
                        v_plane = u_plane + uv_size;
                    }
                }
                if (error.empty()) {
                    for (int y = 0; y < h; ++y) {
                        const uint8_t* yrow = y_plane + static_cast<size_t>(y) * static_cast<size_t>(y_stride);
                        for (int x = 0; x < w; ++x) {
                            uint8_t r = 0, g = 0, b = 0;
                            if (nv12) {
                                const uint8_t* uv = uv_plane + static_cast<size_t>(y / 2) * static_cast<size_t>(y_stride) +
                                                    (x & ~1);
                                yuv_to_rgb_fast(yrow[x], uv[0], uv[1], r, g, b);
                            } else if (u_plane && v_plane) {
                                yuv_to_rgb_fast(yrow[x], u_plane[(y / 2) * uv_stride + x / 2],
                                                v_plane[(y / 2) * uv_stride + x / 2], r, g, b);
                            }
                            store(x, y, r, g, b);
                        }
                    }
                    ok = true;
                }
            } else if (still_format == libcamera::formats::RGB888 || still_format == libcamera::formats::BGR888) {
                const int row_stride = still_stride > 0 ? still_stride : w * 3;
                const bool bgr = still_format == libcamera::formats::BGR888;
                for (int y = 0; y < h; ++y) {
                    const uint8_t* row = plane_ptrs[0] + static_cast<size_t>(y) * static_cast<size_t>(row_stride);
                    for (int x = 0; x < w; ++x) {
                        const uint8_t* p = row + x * 3;
                        if (bgr) store(x, y, p[2], p[1], p[0]);
                        else store(x, y, p[0], p[1], p[2]);
                    }
                }
                ok = true;
            } else if (still_format == libcamera::formats::YUYV || still_format == libcamera::formats::UYVY) {
                const int row_stride = still_stride > 0 ? still_stride : w * 2;
                const bool yuyv = still_format == libcamera::formats::YUYV;
                for (int y = 0; y < h; ++y) {
                    const uint8_t* row = plane_ptrs[0] + static_cast<size_t>(y) * static_cast<size_t>(row_stride);
                    for (int x = 0; x < w; ++x) {
                        const int x2 = x & ~1;
                        const uint8_t b0 = row[x2 * 2];
                        const uint8_t b1 = row[x2 * 2 + 1];
                        const uint8_t b2 = row[x2 * 2 + 2];
                        const uint8_t b3 = row[x2 * 2 + 3];
                        const uint8_t yy = (x & 1) ? (yuyv ? b2 : b3) : (yuyv ? b0 : b1);
                        const uint8_t uu = yuyv ? b1 : b0;
                        const uint8_t vv = yuyv ? b3 : b2;
                        uint8_t r, g, b;
                        yuv_to_rgb_fast(yy, uu, vv, r, g, b);
                        store(x, y, r, g, b);
                    }
                }
                ok = true;
            } else if (still_format == libcamera::formats::RGB565) {
                const int row_stride = still_stride > 0 ? still_stride : w * 2;
                for (int y = 0; y < h; ++y) {
                    const uint8_t* row = plane_ptrs[0] + static_cast<size_t>(y) * static_cast<size_t>(row_stride);
                    for (int x = 0; x < w; ++x) {
                        const uint16_t p = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
                        store(x, y, static_cast<uint8_t>(((p >> 11) & 0x1f) * 255 / 31),
                              static_cast<uint8_t>(((p >> 5) & 0x3f) * 255 / 63),
                              static_cast<uint8_t>((p & 0x1f) * 255 / 31));
                    }
                }
                ok = true;
            } else {
                error = "unsupported still format";
            }
        } catch (const std::bad_alloc&) {
            error = "out of memory";
            ok = false;
        }

        for (const auto& plane : mapped_planes) {
            sync_dma_buf(plane.fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
        }
        return ok;
    }

    bool capture_rgb888(std::vector<uint8_t>& rgb, int& w, int& h, std::string& error) {
        if (!streaming || !still_stream) {
            error = "still stream unavailable";
            return false;
        }
        std::unique_lock<std::mutex> lock(still_mu);
        if (still_busy) {
            error = "capture busy";
            return false;
        }
        still_busy = true;
        still_done = false;
        still_ok = false;
        still_error.clear();
        still_rgb.clear();
        capture_requested = true;
        const bool signaled =
            still_cv.wait_for(lock, std::chrono::seconds(8), [this] { return still_done; });
        still_busy = false;
        if (!signaled) {
            capture_requested = false;
            error = "still capture timed out";
            return false;
        }
        if (!still_ok) {
            error = still_error.empty() ? "still capture failed" : still_error;
            return false;
        }
        rgb = std::move(still_rgb);
        still_rgb.clear();
        w = still_w;
        h = still_h;
        return true;
    }

    bool pack_i420(const libcamera::FrameBuffer* buffer, std::vector<uint8_t>& out, std::string& error) {
        auto map_it = mapped.find(buffer);
        if (map_it == mapped.end() || map_it->second.planes.empty()) {
            error = "still buffer not mapped";
            return false;
        }
        const int w = still_w;
        const int h = still_h;
        const int y_stride = still_stride > 0 ? still_stride : w;
        if (w <= 0 || h <= 0 || (w & 1) || (h & 1) || (y_stride & 1) || y_stride < w) {
            error = "still geometry cannot be encoded";
            return false;
        }
        const int uv_stride = y_stride / 2;
        const int uv_rows = h / 2;
        const size_t y_size = static_cast<size_t>(y_stride) * static_cast<size_t>(h);
        const size_t uv_size = static_cast<size_t>(uv_stride) * static_cast<size_t>(uv_rows);
        const auto& planes = map_it->second.planes;
        for (const auto& plane : planes) sync_dma_buf(plane.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);

        bool ok = false;
        try {
            out.assign(y_size + uv_size * 2, 0);
            const uint8_t* y_src = static_cast<const uint8_t*>(planes[0].addr) + planes[0].data_offset;
            std::memcpy(out.data(), y_src, std::min(y_size, planes[0].data_size));
            uint8_t* u_dst = out.data() + y_size;
            uint8_t* v_dst = u_dst + uv_size;

            const bool nv21 = still_format == libcamera::formats::NV21;
            const bool yvu = still_format == libcamera::formats::YVU420;
            if (still_format == libcamera::formats::NV12 || nv21) {
                const uint8_t* uv = nullptr;
                if (planes.size() >= 2) {
                    uv = static_cast<const uint8_t*>(planes[1].addr) + planes[1].data_offset;
                } else if (planes[0].data_size >= y_size + static_cast<size_t>(y_stride) * static_cast<size_t>(uv_rows)) {
                    uv = y_src + y_size;
                }
                if (!uv) {
                    error = "short yuv frame";
                } else {
                    for (int row = 0; row < uv_rows; ++row) {
                        const uint8_t* src = uv + static_cast<size_t>(row) * static_cast<size_t>(y_stride);
                        uint8_t* u = u_dst + static_cast<size_t>(row) * static_cast<size_t>(uv_stride);
                        uint8_t* v = v_dst + static_cast<size_t>(row) * static_cast<size_t>(uv_stride);
                        for (int x = 0; x < w / 2; ++x) {
                            u[x] = src[x * 2 + (nv21 ? 1 : 0)];
                            v[x] = src[x * 2 + (nv21 ? 0 : 1)];
                        }
                    }
                    ok = true;
                }
            } else if (still_format == libcamera::formats::YUV420 || yvu) {
                if (planes.size() >= 3) {
                    const uint8_t* p1 = static_cast<const uint8_t*>(planes[1].addr) + planes[1].data_offset;
                    const uint8_t* p2 = static_cast<const uint8_t*>(planes[2].addr) + planes[2].data_offset;
                    const uint8_t* u_src = yvu ? p2 : p1;
                    const uint8_t* v_src = yvu ? p1 : p2;
                    const size_t u_bytes = yvu ? planes[2].data_size : planes[1].data_size;
                    const size_t v_bytes = yvu ? planes[1].data_size : planes[2].data_size;
                    std::memcpy(u_dst, u_src, std::min(uv_size, u_bytes));
                    std::memcpy(v_dst, v_src, std::min(uv_size, v_bytes));
                    ok = true;
                } else if (planes[0].data_size >= y_size + uv_size * 2) {
                    const uint8_t* chroma = y_src + y_size;
                    std::memcpy(yvu ? v_dst : u_dst, chroma, uv_size);
                    std::memcpy(yvu ? u_dst : v_dst, chroma + uv_size, uv_size);
                    ok = true;
                } else {
                    error = "short yuv frame";
                }
            } else {
                error = "unsupported still format";
            }

            if (ok && software_rotate180) {
                std::vector<uint8_t> flipped(out.size());
                const uint8_t* y = out.data();
                const uint8_t* u = y + y_size;
                const uint8_t* v = u + uv_size;
                uint8_t* yd = flipped.data();
                uint8_t* ud = yd + y_size;
                uint8_t* vd = ud + uv_size;
                for (int row = 0; row < h; ++row) {
                    const uint8_t* src = y + static_cast<size_t>(h - 1 - row) * static_cast<size_t>(y_stride);
                    uint8_t* dst = yd + static_cast<size_t>(row) * static_cast<size_t>(y_stride);
                    for (int x = 0; x < w; ++x) dst[x] = src[w - 1 - x];
                }
                for (int row = 0; row < uv_rows; ++row) {
                    const uint8_t* su = u + static_cast<size_t>(uv_rows - 1 - row) * static_cast<size_t>(uv_stride);
                    const uint8_t* sv = v + static_cast<size_t>(uv_rows - 1 - row) * static_cast<size_t>(uv_stride);
                    uint8_t* du = ud + static_cast<size_t>(row) * static_cast<size_t>(uv_stride);
                    uint8_t* dv = vd + static_cast<size_t>(row) * static_cast<size_t>(uv_stride);
                    for (int x = 0; x < w / 2; ++x) {
                        du[x] = su[w / 2 - 1 - x];
                        dv[x] = sv[w / 2 - 1 - x];
                    }
                }
                out.swap(flipped);
            }
        } catch (const std::bad_alloc&) {
            error = "out of memory";
            ok = false;
        }

        for (const auto& plane : planes) sync_dma_buf(plane.fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
        if (!ok && error.empty()) error = "still pack failed";
        return ok;
    }

    bool configure_sensor_still(int want_w, int want_h, bool with_raw, std::string& error) {
        release_streams();
        {
            std::lock_guard<std::mutex> lock(still_mu);
            still_done = false;
            still_ok = false;
            still_error.clear();
            still_i420.clear();
            still_seen = 0;
        }
        still_mode = true;

        auto fail = [&](const char* msg) {
            error = msg;
            release_streams();
            return false;
        };

        if (with_raw) {
            config = camera->generateConfiguration(
                {libcamera::StreamRole::StillCapture, libcamera::StreamRole::Raw});
        } else {
            config = camera->generateConfiguration({libcamera::StreamRole::StillCapture});
        }
        if (!config || config->empty() || (with_raw && config->size() < 2)) {
            return fail("still configuration unavailable");
        }

        config->orientation = libcamera::Orientation::Rotate180;
        auto& still = config->at(0);
        still.pixelFormat = libcamera::formats::YUV420;
        still.size = {static_cast<unsigned int>(want_w), static_cast<unsigned int>(want_h)};
        still.bufferCount = 1;
        if (with_raw) {
            auto& raw = config->at(1);
            raw.pixelFormat = libcamera::formats::SBGGR10_CSI2P;
            raw.size = {static_cast<unsigned int>(want_w), static_cast<unsigned int>(want_h)};
            raw.bufferCount = 1;
            libcamera::SensorConfiguration sensor;
            sensor.bitDepth = 10;
            sensor.outputSize =
                libcamera::Size(static_cast<unsigned int>(want_w), static_cast<unsigned int>(want_h));
            config->sensorConfig = sensor;
        }
        if (config->validate() == libcamera::CameraConfiguration::Invalid) {
            return fail("still configuration invalid");
        }

        const unsigned got_w = still.size.width;
        const unsigned got_h = still.size.height;
        const auto fmt = still.pixelFormat;
        const bool yuv_pack = fmt == libcamera::formats::YUV420 || fmt == libcamera::formats::YVU420 ||
                              fmt == libcamera::formats::NV12 || fmt == libcamera::formats::NV21;
        // JPEG 4:2:0 wants multiples of 16. Keep the sensor frame and encode the aligned part.
        const unsigned enc_w = got_w & ~15u;
        const unsigned enc_h = got_h & ~15u;
        if ((got_w & 1) || (got_h & 1) || enc_w < 640 || enc_h < 480 || (!yuv_pack && !format_supported(fmt))) {
            const std::string why =
                "rej " + std::to_string(got_w) + "x" + std::to_string(got_h) + " " + fmt.toString();
            return fail(why.c_str());
        }
        // The pipeline sometimes substitutes a much smaller mode. Try the next candidate.
        if (static_cast<uint64_t>(got_w) * got_h * 2 < static_cast<uint64_t>(want_w) * want_h) {
            return fail("still size adjusted");
        }
        if (camera->configure(config.get()) < 0) return fail("still configure failed");

        software_rotate180 = (config->orientation != libcamera::Orientation::Rotate180);
        still_stream = still.stream();
        raw_stream = (with_raw && config->size() > 1) ? config->at(1).stream() : nullptr;
        still_as_rgb = !yuv_pack;
        still_w = static_cast<int>(yuv_pack ? enc_w : got_w);
        still_h = static_cast<int>(yuv_pack ? enc_h : got_h);
        still_stride = static_cast<int>(still.stride > 0 ? still.stride : got_w);
        still_format = fmt;
        if (still_stride < static_cast<int>(got_w) || (yuv_pack && (still_stride & 1))) {
            return fail("still stride rejected");
        }

        libcamera::FrameBuffer* still_buffer = nullptr;
        libcamera::FrameBuffer* raw_buffer = nullptr;
        const bool heap_still = allocate_cma_buffer(config->at(0), "kvx-still", still_buffer);
        const bool heap_raw = !raw_stream || (config->size() > 1 && allocate_cma_buffer(config->at(1), "kvx-raw", raw_buffer));
        if (!heap_still || !heap_raw || !still_buffer || (raw_stream && !raw_buffer)) {
            for (libcamera::FrameBuffer* buf : {still_buffer, raw_buffer}) {
                if (!buf) continue;
                auto mapped_it = mapped.find(buf);
                if (mapped_it == mapped.end()) continue;
                for (auto& plane : mapped_it->second.planes) {
                    if (plane.addr && plane.addr != MAP_FAILED) ::munmap(plane.addr, plane.size);
                }
                mapped.erase(mapped_it);
            }
            owned_buffers.clear();
            still_buffer = nullptr;
            raw_buffer = nullptr;
            allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
            if (allocator->allocate(still_stream) < 0) return fail("still buffer allocate failed");
            if (raw_stream && allocator->allocate(raw_stream) < 0) return fail("raw buffer allocate failed");
            for (const auto& buffer : allocator->buffers(still_stream)) {
                if (!map_buffer(buffer.get())) continue;
                still_buffer = buffer.get();
                break;
            }
            if (raw_stream) {
                const auto& raw_buffers = allocator->buffers(raw_stream);
                if (!raw_buffers.empty()) raw_buffer = raw_buffers[0].get();
            }
        }
        if (!still_buffer || (raw_stream && !raw_buffer)) return fail("no still capture buffers");

        auto request = camera->createRequest();
        if (!request || request->addBuffer(still_stream, still_buffer) < 0) return fail("still request failed");
        if (raw_buffer && request->addBuffer(raw_stream, raw_buffer) < 0) return fail("raw request failed");
        apply_request_controls(request.get());
        requests.push_back(std::move(request));

        camera->requestCompleted.connect(this, &Impl::request_complete);
        if (camera->start()) {
            camera->requestCompleted.disconnect(this);
            return fail("still start failed");
        }
        streaming = true;
        camera->queueRequest(requests.back().get());
        return true;
    }

    bool capture_sensor_still(SensorStill& out, std::string& error) {
        if (!camera || !streaming || still_mode) {
            error = "camera not streaming";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(still_mu);
            if (still_busy) {
                error = "capture busy";
                return false;
            }
            still_busy = true;
        }
        const int saved_w = live_w > 0 ? live_w : 1640;
        const int saved_h = live_h > 0 ? live_h : 1232;

        const int attempts[][3] = {{3280, 2464, 1}, {3280, 2464, 0}, {1640, 1232, 1}, {1640, 1232, 0},
                                    {1280, 960, 1}, {1280, 960, 0}};
        bool started = false;
        for (const auto& attempt : attempts) {
            if (configure_sensor_still(attempt[0], attempt[1], attempt[2] != 0, error)) {
                started = true;
                break;
            }
        }

        bool ok = false;
        if (started) {
            std::unique_lock<std::mutex> lock(still_mu);
            const bool signaled =
                still_cv.wait_for(lock, std::chrono::seconds(20), [this] { return still_done; });
            if (!signaled) {
                error = "still capture timed out";
            } else if (!still_ok || still_i420.empty()) {
                error = still_error.empty() ? "still capture failed" : still_error;
            } else {
                out.width = still_w;
                out.height = still_h;
                out.stride = still_stride > 0 ? still_stride : still_w;
                out.i420 = std::move(still_i420);
                out.rgb888 = std::move(still_rgb);
                ok = !out.i420.empty() || !out.rgb888.empty();
                if (!ok) error = "still capture failed";
            }
        }

        if (camera) {
            release_streams();
            std::string preview_error;
            if (!configure_and_start(saved_w, saved_h, preview_error)) {
                configure_and_start(320, 240, preview_error);
            }
        }
        {
            std::lock_guard<std::mutex> lock(still_mu);
            still_busy = false;
        }
        if (!ok && error.empty()) error = "full still unavailable";
        return ok;
    }

    bool configure_dual(int preview_w, int preview_h, int want_w, int want_h, std::string& error) {
        requests.clear();
        for (auto& item : mapped) {
            for (auto& plane : item.second.planes) {
                if (plane.addr && plane.addr != MAP_FAILED) ::munmap(plane.addr, plane.size);
            }
        }
        mapped.clear();
        allocator.reset();
        stream = nullptr;
        still_stream = nullptr;
        free_still.clear();
        if (streaming) {
            camera->requestCompleted.disconnect(this);
            camera->stop();
            streaming = false;
        }

        config = camera->generateConfiguration(
            {libcamera::StreamRole::Viewfinder, libcamera::StreamRole::StillCapture});
        if (!config || config->size() < 2) {
            error = "still configuration unavailable";
            return false;
        }
        config->orientation = libcamera::Orientation::Rotate180;
        auto& preview = config->at(0);
        preview.size.width = static_cast<unsigned int>(preview_w);
        preview.size.height = static_cast<unsigned int>(preview_h);
        preview.pixelFormat = libcamera::formats::RGB565;
        preview.bufferCount = 4;
        auto& still = config->at(1);
        still.size.width = static_cast<unsigned int>(want_w);
        still.size.height = static_cast<unsigned int>(want_h);
        still.pixelFormat = libcamera::formats::YUV420;
        still.bufferCount = 1;
        if (config->validate() == libcamera::CameraConfiguration::Invalid) {
            error = "invalid still configuration";
            return false;
        }
        if (!format_supported(preview.pixelFormat) || !format_supported(still.pixelFormat)) {
            error = "unsupported still format";
            return false;
        }
        if (camera->configure(config.get()) < 0) {
            error = "still configure failed";
            return false;
        }
        software_rotate180 = (config->orientation != libcamera::Orientation::Rotate180);
        stream = preview.stream();
        width = static_cast<int>(preview.size.width);
        height = static_cast<int>(preview.size.height);
        stride = static_cast<int>(preview.stride);
        pixel_format = preview.pixelFormat;
        still_stream = still.stream();
        still_w = static_cast<int>(still.size.width);
        still_h = static_cast<int>(still.size.height);
        still_stride = static_cast<int>(still.stride);
        still_format = still.pixelFormat;
        if (still_w < 1280 || still_h < 720) {
            error = "still stream too small";
            still_stream = nullptr;
            return false;
        }

        allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
        if (allocator->allocate(stream) < 0 || allocator->allocate(still_stream) < 0) {
            error = "still framebuffer allocate failed";
            return false;
        }
        for (const auto& buffer : allocator->buffers(stream)) {
            if (!map_buffer(buffer.get())) continue;
            auto request = camera->createRequest();
            if (!request || request->addBuffer(stream, buffer.get()) < 0) continue;
            apply_request_controls(request.get());
            requests.push_back(std::move(request));
        }
        for (const auto& buffer : allocator->buffers(still_stream)) {
            if (!map_buffer(buffer.get())) continue;
            free_still.push_back(buffer.get());
        }
        if (requests.empty() || free_still.empty()) {
            error = "no still capture buffers";
            return false;
        }
        camera->requestCompleted.connect(this, &Impl::request_complete);
        if (camera->start()) {
            camera->requestCompleted.disconnect(this);
            error = "camera start failed";
            return false;
        }
        streaming = true;
        for (auto& request : requests) camera->queueRequest(request.get());
        return true;
    }

    bool allocate_cma_buffer(const libcamera::StreamConfiguration& cfg, const char* name,
                             libcamera::FrameBuffer*& out) {
        out = nullptr;
        if (cfg.frameSize == 0) return false;
        if (dma_heap_ < 0) {
            dma_heap_ = ::open("/dev/dma_heap/default_cma_region", O_RDWR | O_CLOEXEC);
        }
        if (dma_heap_ < 0) return false;

        dma_heap_allocation_data allocation{};
        allocation.len = cfg.frameSize;
        allocation.fd_flags = O_CLOEXEC | O_RDWR;
        if (::ioctl(dma_heap_, DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) return false;

        const int buf_fd = static_cast<int>(allocation.fd);
        if (name && name[0]) ::ioctl(buf_fd, DMA_BUF_SET_NAME, name);

        libcamera::FrameBuffer::Plane plane;
        plane.fd = libcamera::SharedFD(libcamera::UniqueFD(buf_fd));
        plane.offset = 0;
        plane.length = cfg.frameSize;
        auto buffer = std::make_unique<libcamera::FrameBuffer>(
            std::span<const libcamera::FrameBuffer::Plane>(&plane, 1));
        if (!map_buffer(buffer.get())) return false;
        out = buffer.get();
        owned_buffers.push_back(std::move(buffer));
        return true;
    }

    static void use_rpi_pipeline_config() {
        if (const char* existing = std::getenv("LIBCAMERA_RPI_CONFIG_FILE")) {
            if (existing[0]) return;
        }
        const char* paths[] = {
            "/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml",
            "/usr/local/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml",
        };
        for (const char* path : paths) {
            if (::access(path, R_OK) == 0) {
                ::setenv("LIBCAMERA_RPI_CONFIG_FILE", path, 0);
                return;
            }
        }
    }

    bool start(std::string& error) {
        stop();
        use_rpi_pipeline_config();
        manager = std::make_unique<libcamera::CameraManager>();
        if (manager->start()) {
            error = "CameraManager start failed";
            stop();
            return false;
        }
        if (manager->cameras().empty()) {
            error = "No libcamera camera found";
            stop();
            return false;
        }
        camera = manager->cameras().front();
        for (const auto& cam : manager->cameras()) {
            std::string lower = cam->id();
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            camera = cam;
            if (lower.find("imx219") != std::string::npos || lower.find("ov5647") != std::string::npos ||
                lower.find("imx708") != std::string::npos || lower.find("/base/soc/i2c") != std::string::npos) {
                break;
            }
        }
        if (camera->acquire()) {
            error = "Camera acquire failed";
            stop();
            return false;
        }
        // 1640x1232 is the binned full-sensor mode. 640x480 is a center crop, so it stays last.
        const std::pair<int, int> sizes[] = {{1640, 1232}, {1280, 960}, {640, 480}, {320, 240}};
        for (const auto& sz : sizes) {
            if (configure_and_start(sz.first, sz.second, error)) return true;
        }
        if (error.empty()) error = "libcamera stream configure failed";
        stop();
        return false;
    }

    bool copy_frame(std::vector<uint16_t>& rgb565, int& w, int& h) {
        std::lock_guard<std::mutex> lock(frame_mu);
        if (!frame_ready) return false;
        rgb565 = latest;
        w = latest_w;
        h = latest_h;
        return true;
    }
};

#else

struct LibcameraPreview::Impl {
    bool start(std::string& error) {
        error = "libcamera not linked (need device libs: scripts/pull_device_libcamera.sh)";
        return false;
    }
    void stop() {}
    bool copy_frame(std::vector<uint16_t>&, int&, int&) { return false; }
    bool streaming{false};
};

#endif

LibcameraPreview::LibcameraPreview() : impl_(std::make_unique<Impl>()) {}
LibcameraPreview::~LibcameraPreview() { stop(); }

bool LibcameraPreview::start(std::string& error) {
    return impl_->start(error);
}

void LibcameraPreview::stop() {
    if (impl_) impl_->stop();
}

bool LibcameraPreview::running() const {
#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
    return impl_ && impl_->streaming;
#else
    return false;
#endif
}

bool LibcameraPreview::still_ready() const {
#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
    return impl_ && impl_->still_ready();
#else
    return false;
#endif
}

bool LibcameraPreview::capture_sensor_still(SensorStill& out, std::string& error) {
#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
    if (!impl_) {
        error = "camera not started";
        return false;
    }
    return impl_->capture_sensor_still(out, error);
#else
    (void)out;
    error = "libcamera not linked";
    return false;
#endif
}

bool LibcameraPreview::capture_rgb888(std::vector<uint8_t>& rgb, int& width, int& height, std::string& error) {
#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
    if (!impl_) {
        error = "camera not started";
        return false;
    }
    return impl_->capture_rgb888(rgb, width, height, error);
#else
    (void)rgb;
    (void)width;
    (void)height;
    error = "libcamera not linked";
    return false;
#endif
}

bool LibcameraPreview::copy_frame(std::vector<uint16_t>& rgb565, int& width, int& height) {
    return impl_ && impl_->copy_frame(rgb565, width, height);
}

} // namespace platform

#include "camera_libcamera.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(KVX_HAVE_LIBCAMERA) && KVX_HAVE_LIBCAMERA
#include <fcntl.h>
#include <linux/dma-buf.h>
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

    std::mutex frame_mu;
    std::vector<uint16_t> latest;
    int latest_w{0};
    int latest_h{0};
    bool frame_ready{false};

    static constexpr int64_t kPreviewMinFrameDurationUs = 16667;
    static constexpr int64_t kPreviewMaxFrameDurationUs = 33333;

    ~Impl() { stop(); }

    void stop() {
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
        allocator.reset();
        config.reset();
        stream = nullptr;
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
        request->controls().set(libcamera::controls::FrameDurationLimits,
                                {kPreviewMinFrameDurationUs, kPreviewMaxFrameDurationUs});
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

    void request_complete(libcamera::Request* request) {
        if (!request || request->status() == libcamera::Request::RequestCancelled) return;
        if (!streaming || !camera) return;
        if (request->status() == libcamera::Request::RequestComplete) {
            (void)convert_request(request);
        }
        request->reuse(libcamera::Request::ReuseBuffers);
        apply_request_controls(request);
        camera->queueRequest(request);
    }

    bool configure_and_start(int want_w, int want_h, std::string& error) {
        requests.clear();
        for (auto& item : mapped) {
            for (auto& plane : item.second.planes) {
                if (plane.addr && plane.addr != MAP_FAILED) ::munmap(plane.addr, plane.size);
            }
        }
        mapped.clear();
        allocator.reset();
        stream = nullptr;
        if (streaming) {
            camera->requestCompleted.disconnect(this);
            camera->stop();
            streaming = false;
        }

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
        for (auto& request : requests) camera->queueRequest(request.get());
        return true;
    }

    bool start(std::string& error) {
        stop();
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
        // FXCam uses a small RGB565 preview; try that first.
        const std::pair<int, int> sizes[] = {{320, 240}, {640, 480}, {320, 180}, {1280, 720}};
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

bool LibcameraPreview::copy_frame(std::vector<uint16_t>& rgb565, int& width, int& height) {
    return impl_ && impl_->copy_frame(rgb565, width, height);
}

} // namespace platform

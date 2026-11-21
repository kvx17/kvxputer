#include "camera_session.h"

#include <jpeglib.h>
#include <png.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <pwd.h>
#include <unistd.h>

#if !USE_DESKTOP
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#endif

namespace platform {
namespace {

#if USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

std::string home_dir() {
    if (const char* home = std::getenv("HOME")) return home;
    if (auto* pw = getpwuid(getuid())) return pw->pw_dir;
    return ".";
}

uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void unpack_rgb565(uint16_t pixel, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
    g = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
    b = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
}

std::string timestamp_name(const char* ext) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "KVX_%04d%02d%02d_%02d%02d%02d%s", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ext);
    return buf;
}

void write_u16_le(FILE* fp, uint16_t v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff)};
    std::fwrite(b, 1, 2, fp);
}

void write_u32_le(FILE* fp, uint32_t v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff),
                          static_cast<uint8_t>((v >> 16) & 0xff), static_cast<uint8_t>((v >> 24) & 0xff)};
    std::fwrite(b, 1, 4, fp);
}

struct JpegErrMgr {
    jpeg_error_mgr pub{};
    jmp_buf jump{};
};

void jpeg_fail(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegErrMgr*>(cinfo->err);
    longjmp(err->jump, 1);
}

bool encode_jpeg_mem(const std::vector<uint8_t>& rgb, int w, int h, std::vector<uint8_t>& out,
                     std::string& error) {
    out.clear();
    if (w <= 0 || h <= 0 || w > 1920 || h > 1080 || rgb.size() < static_cast<size_t>(w) * h * 3) {
        error = "bad rgb buffer";
        return false;
    }
    // libjpeg's default error handler calls exit(). A failed encode must not take down the app.
    jpeg_compress_struct cinfo{};
    JpegErrMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_fail;
    unsigned char* mem = nullptr;
    unsigned long mem_size = 0;
    volatile bool created = false;
    if (setjmp(jerr.jump)) {
        if (created) jpeg_destroy_compress(&cinfo);
        if (mem) std::free(mem);
        error = "jpeg encode failed";
        return false;
    }
    jpeg_create_compress(&cinfo);
    created = true;
    jpeg_mem_dest(&cinfo, &mem, &mem_size);
    cinfo.image_width = static_cast<JDIMENSION>(w);
    cinfo.image_height = static_cast<JDIMENSION>(h);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 70, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(&rgb[static_cast<size_t>(cinfo.next_scanline) * static_cast<size_t>(w) * 3]);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    created = false;
    if (!mem || mem_size == 0) {
        error = "jpeg encode failed";
        return false;
    }
    out.assign(mem, mem + mem_size);
    std::free(mem);
    return true;
}

void scale_rgb565(const std::vector<uint16_t>& src, int sw, int sh, std::vector<uint16_t>& dst, int dw, int dh) {
    dst.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0);
    if (sw <= 0 || sh <= 0 || src.size() < static_cast<size_t>(sw) * static_cast<size_t>(sh)) return;
    for (int y = 0; y < dh; ++y) {
        const int sy = y * sh / dh;
        for (int x = 0; x < dw; ++x) {
            const int sx = x * sw / dw;
            dst[static_cast<size_t>(y * dw + x)] = src[static_cast<size_t>(sy * sw + sx)];
        }
    }
}

void record_size_for(int w, int h, int& dw, int& dh) {
    dw = std::max(2, w);
    dh = std::max(2, h);
    if (dw > 320) {
        dh = std::max(2, dh * 320 / dw);
        dw = 320;
    }
    if (dh > 240) {
        dw = std::max(2, dw * 240 / dh);
        dh = 240;
    }
    dw &= ~1;
    dh &= ~1;
    if (dw < 2) dw = 2;
    if (dh < 2) dh = 2;
}

} // namespace

std::string pictures_dir() {
    return (std::filesystem::path(home_dir()) / "Pictures").string();
}

std::string camera_stills_dir() {
    return (std::filesystem::path(home_dir()) / "Pictures" / "DCIM" / "Camera").string();
}

std::string camera_videos_dir() {
    return (std::filesystem::path(home_dir()) / "Videos" / "Camera").string();
}

CameraSession::CameraSession() = default;

CameraSession::~CameraSession() {
    stop();
}

bool CameraSession::start(std::string& error) {
    if (running_.load()) return true;
    backend_ = "none";
    error.clear();

    if (kDesktop) {
        backend_ = "simulation";
    } else {
        std::string cam_err;
        std::string v4l_err;
        if (open_libcamera(cam_err)) {
            backend_ = "libcamera";
            error.clear();
        } else if (open_v4l2(v4l_err)) {
            backend_ = "v4l2";
            error.clear();
        } else {
            backend_ = "none";
            if (!cam_err.empty() && !v4l_err.empty()) {
                error = cam_err + "; " + v4l_err;
            } else if (!cam_err.empty()) {
                error = cam_err;
            } else {
                error = v4l_err.empty() ? "no camera" : v4l_err;
            }
            return false;
        }
    }

    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this]() { worker_loop(); });
    return true;
}

void CameraSession::stop() {
    cancel_ = true;
    stop_recording();
    if (worker_.joinable()) worker_.join();
    running_ = false;
    close_libcamera();
    close_v4l2();
}

void CameraSession::set_filter(tools::camfilter::Filter filter) {
    filter_.store(static_cast<int>(filter));
}

tools::camfilter::Filter CameraSession::filter() const {
    return static_cast<tools::camfilter::Filter>(filter_.load());
}

void CameraSession::set_brightness(float gain) {
    if (gain < 0.4f) gain = 0.4f;
    if (gain > 2.0f) gain = 2.0f;
    brightness_.store(gain);
}

float CameraSession::brightness() const {
    return brightness_.load();
}

bool CameraSession::copy_preview(CameraFrame& out) const {
    std::lock_guard<std::mutex> lock(frame_mu_);
    if (!frame_ready_.load()) return false;
    out = latest_;
    return true;
}

void CameraSession::rgb565_to_rgb888(const std::vector<uint16_t>& src, std::vector<uint8_t>& dst) {
    dst.resize(src.size() * 3);
    for (size_t i = 0; i < src.size(); ++i) {
        uint8_t r, g, b;
        unpack_rgb565(src[i], r, g, b);
        dst[i * 3] = r;
        dst[i * 3 + 1] = g;
        dst[i * 3 + 2] = b;
    }
}

bool CameraSession::encode_jpeg_rgb888(const std::vector<uint8_t>& rgb, int w, int h,
                                       const std::string& path, std::string& error) {
    std::vector<uint8_t> jpeg;
    if (!encode_jpeg_mem(rgb, w, h, jpeg, error)) return false;
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        error = "cannot write jpeg";
        return false;
    }
    const size_t n = std::fwrite(jpeg.data(), 1, jpeg.size(), fp);
    std::fclose(fp);
    if (n != jpeg.size()) {
        error = "short jpeg write";
        return false;
    }
    return true;
}

bool CameraSession::capture_still(std::string& out_path, std::string& error) {
    CameraFrame frame;
    if (!copy_preview(frame) || frame.width <= 0 || frame.height <= 0) {
        error = "no preview frame";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(camera_stills_dir(), ec);
    out_path = (std::filesystem::path(camera_stills_dir()) / timestamp_name(".jpg")).string();
    std::vector<uint8_t> rgb;
    rgb565_to_rgb888(frame.rgb565, rgb);
    return encode_jpeg_rgb888(rgb, frame.width, frame.height, out_path, error);
}

bool CameraSession::start_recording(std::string& out_path, std::string& error) {
    if (recording_.load()) {
        out_path = record_path_;
        return true;
    }
    int src_w = 0;
    int src_h = 0;
    {
        std::lock_guard<std::mutex> fl(frame_mu_);
        if (!frame_ready_.load() || latest_.width <= 0 || latest_.height <= 0 || latest_.rgb565.empty()) {
            error = "no preview frame";
            return false;
        }
        src_w = latest_.width;
        src_h = latest_.height;
    }
    int dw = 0;
    int dh = 0;
    record_size_for(src_w, src_h, dw, dh);

    std::lock_guard<std::mutex> lock(record_mu_);
    if (recording_.load()) {
        out_path = record_path_;
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(camera_videos_dir(), ec);
    record_path_ = (std::filesystem::path(camera_videos_dir()) / timestamp_name(".avi")).string();
    avi_fp_ = std::fopen(record_path_.c_str(), "wb+");
    if (!avi_fp_) {
        error = "cannot open avi";
        record_path_.clear();
        return false;
    }
    avi_w_ = dw;
    avi_h_ = dh;
    avi_frames_ = 0;
    avi_index_.clear();

    auto fourcc = [&](const char* tag) { std::fwrite(tag, 1, 4, avi_fp_); };

    fourcc("RIFF");
    write_u32_le(avi_fp_, 0);
    fourcc("AVI ");

    const long hdrl_list = std::ftell(avi_fp_);
    fourcc("LIST");
    write_u32_le(avi_fp_, 0);
    fourcc("hdrl");

    fourcc("avih");
    write_u32_le(avi_fp_, 56);
    write_u32_le(avi_fp_, 100000);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0x10);
    avi_frames_pos_ = std::ftell(avi_fp_);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 1);
    write_u32_le(avi_fp_, static_cast<uint32_t>(dw * dh * 3));
    write_u32_le(avi_fp_, static_cast<uint32_t>(dw));
    write_u32_le(avi_fp_, static_cast<uint32_t>(dh));
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);

    const long strl_list = std::ftell(avi_fp_);
    fourcc("LIST");
    write_u32_le(avi_fp_, 0);
    fourcc("strl");
    fourcc("strh");
    write_u32_le(avi_fp_, 56);
    fourcc("vids");
    fourcc("MJPG");
    write_u32_le(avi_fp_, 0);
    write_u16_le(avi_fp_, 0);
    write_u16_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 1);
    write_u32_le(avi_fp_, 10);
    write_u32_le(avi_fp_, 0);
    avi_strh_len_pos_ = std::ftell(avi_fp_);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, static_cast<uint32_t>(dw * dh * 3));
    write_u32_le(avi_fp_, 0);
    write_u16_le(avi_fp_, 0);
    write_u16_le(avi_fp_, 0);
    write_u16_le(avi_fp_, static_cast<uint16_t>(dw));
    write_u16_le(avi_fp_, static_cast<uint16_t>(dh));
    fourcc("strf");
    write_u32_le(avi_fp_, 40);
    write_u32_le(avi_fp_, 40);
    write_u32_le(avi_fp_, static_cast<uint32_t>(dw));
    write_u32_le(avi_fp_, static_cast<uint32_t>(dh));
    write_u16_le(avi_fp_, 1);
    write_u16_le(avi_fp_, 24);
    fourcc("MJPG");
    write_u32_le(avi_fp_, static_cast<uint32_t>(dw * dh * 3));
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);
    write_u32_le(avi_fp_, 0);

    const long strl_end = std::ftell(avi_fp_);
    std::fseek(avi_fp_, strl_list + 4, SEEK_SET);
    write_u32_le(avi_fp_, static_cast<uint32_t>(strl_end - (strl_list + 8)));
    const long hdrl_end = strl_end;
    std::fseek(avi_fp_, hdrl_list + 4, SEEK_SET);
    write_u32_le(avi_fp_, static_cast<uint32_t>(hdrl_end - (hdrl_list + 8)));
    std::fseek(avi_fp_, 0, SEEK_END);

    avi_movi_list_pos_ = std::ftell(avi_fp_);
    fourcc("LIST");
    write_u32_le(avi_fp_, 0);
    fourcc("movi");

    recording_ = true;
    out_path = record_path_;
    return true;
}

bool CameraSession::append_avi_frame(const std::vector<uint8_t>& jpeg) {
    std::lock_guard<std::mutex> lock(record_mu_);
    if (!avi_fp_ || !recording_.load() || jpeg.empty()) return false;
    const long chunk = std::ftell(avi_fp_);
    if (chunk < 0) return false;
    if (std::fwrite("00dc", 1, 4, avi_fp_) != 4) return false;
    write_u32_le(avi_fp_, static_cast<uint32_t>(jpeg.size()));
    if (std::fwrite(jpeg.data(), 1, jpeg.size(), avi_fp_) != jpeg.size()) return false;
    if (jpeg.size() & 1) {
        const uint8_t pad = 0;
        std::fwrite(&pad, 1, 1, avi_fp_);
    }
    const long movi_data = avi_movi_list_pos_ + 8;
    avi_index_.push_back(AviChunk{static_cast<uint32_t>(chunk - movi_data), static_cast<uint32_t>(jpeg.size())});
    ++avi_frames_;
    return true;
}

bool CameraSession::finish_avi() {
    if (!avi_fp_) return false;
    const long movi_end = std::ftell(avi_fp_);
    if (movi_end >= avi_movi_list_pos_ + 8) {
        std::fseek(avi_fp_, avi_movi_list_pos_ + 4, SEEK_SET);
        write_u32_le(avi_fp_, static_cast<uint32_t>(movi_end - (avi_movi_list_pos_ + 8)));
    }
    std::fseek(avi_fp_, 0, SEEK_END);
    std::fwrite("idx1", 1, 4, avi_fp_);
    write_u32_le(avi_fp_, static_cast<uint32_t>(avi_index_.size() * 16));
    for (const auto& chunk : avi_index_) {
        std::fwrite("00dc", 1, 4, avi_fp_);
        write_u32_le(avi_fp_, 0x10);
        write_u32_le(avi_fp_, chunk.offset);
        write_u32_le(avi_fp_, chunk.size);
    }
    if (avi_frames_pos_ > 0) {
        std::fseek(avi_fp_, avi_frames_pos_, SEEK_SET);
        write_u32_le(avi_fp_, avi_frames_);
    }
    if (avi_strh_len_pos_ > 0) {
        std::fseek(avi_fp_, avi_strh_len_pos_, SEEK_SET);
        write_u32_le(avi_fp_, avi_frames_);
    }
    std::fseek(avi_fp_, 0, SEEK_END);
    const long file_end = std::ftell(avi_fp_);
    if (file_end >= 8) {
        std::fseek(avi_fp_, 4, SEEK_SET);
        write_u32_le(avi_fp_, static_cast<uint32_t>(file_end - 8));
    }
    std::fclose(avi_fp_);
    avi_fp_ = nullptr;
    avi_index_.clear();
    return true;
}

void CameraSession::stop_recording() {
    std::lock_guard<std::mutex> lock(record_mu_);
    if (!recording_.load()) return;
    recording_ = false;
    finish_avi();
}

void CameraSession::generate_simulation_frame(std::vector<uint16_t>& out, int w, int h, uint32_t tick) {
    out.resize(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cx = (x + static_cast<int>(tick)) % w;
            const uint8_t r = static_cast<uint8_t>((cx * 255) / std::max(1, w - 1));
            const uint8_t g = static_cast<uint8_t>((y * 255) / std::max(1, h - 1));
            const uint8_t b = static_cast<uint8_t>((std::sin((x + tick) * 0.05f) * 0.5f + 0.5f) * 255);
            out[static_cast<size_t>(y * w + x)] = pack_rgb565(r, g, b);
        }
    }
}

bool CameraSession::open_libcamera(std::string& error) {
#if USE_DESKTOP
    error = "no libcamera on desktop";
    return false;
#else
    return libcamera_.start(error);
#endif
}

void CameraSession::close_libcamera() {
    libcamera_.stop();
}

bool CameraSession::grab_libcamera(std::vector<uint16_t>& out, int& w, int& h) {
    return libcamera_.copy_frame(out, w, h);
}

bool CameraSession::open_v4l2(std::string& error) {
#if USE_DESKTOP
    (void)error;
    return false;
#else
    close_v4l2();
    std::vector<int> indices;
    DIR* dir = opendir("/dev");
    if (!dir) {
        error = "cannot list /dev";
        return false;
    }
    while (auto* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "video", 5) != 0) continue;
        char* end = nullptr;
        const long n = std::strtol(ent->d_name + 5, &end, 10);
        if (end && *end == '\0' && n >= 0) indices.push_back(static_cast<int>(n));
    }
    closedir(dir);
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) {
        error = "no /dev/video*";
        return false;
    }

    std::string last;
    for (int idx : indices) {
        const std::string path = "/dev/video" + std::to_string(idx);
        std::string err;
        if (open_v4l2_device(path, err)) {
            error.clear();
            return true;
        }
        if (!err.empty()) last = err;
    }
    error = last.empty() ? "no usable V4L2 USB camera" : last;
    return false;
#endif
}

bool CameraSession::open_v4l2_device(const std::string& path, std::string& error) {
#if USE_DESKTOP
    (void)path;
    (void)error;
    return false;
#else
    close_v4l2();
    v4l2_fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (v4l2_fd_ < 0) {
        error = path + ": open failed";
        return false;
    }

    v4l2_capability cap{};
    if (ioctl(v4l2_fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        error = path + ": QUERYCAP failed";
        close_v4l2();
        return false;
    }
    const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        error = path + ": not a streaming capture device";
        close_v4l2();
        return false;
    }

    // Prefer USB UVC. Onboard CSI nodes often advertise capture but need libcamera;
    // STREAMON fails on those (what you saw as VIDIOC_STREAMON failed).
    const std::string driver(reinterpret_cast<const char*>(cap.driver));
    const std::string bus(reinterpret_cast<const char*>(cap.bus_info));
    const bool usb = driver == "uvcvideo" || bus.rfind("usb", 0) == 0;
    if (!usb) {
        error = path + ": skip non-USB (" + driver + ")";
        close_v4l2();
        return false;
    }

    const uint32_t formats[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_MJPEG};
    bool fmt_ok = false;
    v4l2_format fmt{};
    for (uint32_t pix : formats) {
        fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = pix;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == pix) {
            fmt_ok = true;
            break;
        }
    }
    if (!fmt_ok) {
        error = path + ": S_FMT failed";
        close_v4l2();
        return false;
    }
    v4l2_w_ = static_cast<int>(fmt.fmt.pix.width);
    v4l2_h_ = static_cast<int>(fmt.fmt.pix.height);
    v4l2_pixfmt_ = fmt.fmt.pix.pixelformat;

    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        error = path + ": REQBUFS failed";
        close_v4l2();
        return false;
    }
    v4l2_bufs_.assign(req.count, {});
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            error = path + ": QUERYBUF failed";
            close_v4l2();
            return false;
        }
        void* mem = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd_, buf.m.offset);
        if (mem == MAP_FAILED) {
            error = path + ": mmap failed";
            close_v4l2();
            return false;
        }
        v4l2_bufs_[i] = {mem, buf.length};
    }
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
            error = path + ": QBUF failed";
            close_v4l2();
            return false;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &type) < 0) {
        error = path + ": STREAMON failed";
        close_v4l2();
        return false;
    }
    return true;
#endif
}

void CameraSession::close_v4l2() {
#if !USE_DESKTOP
    if (v4l2_fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
    }
    for (auto& b : v4l2_bufs_) {
        if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
    }
    v4l2_bufs_.clear();
    if (v4l2_fd_ >= 0) {
        ::close(v4l2_fd_);
        v4l2_fd_ = -1;
    }
#endif
}

bool CameraSession::grab_v4l2(std::vector<uint16_t>& out, int& w, int& h) {
#if USE_DESKTOP
    (void)out;
    (void)w;
    (void)h;
    return false;
#else
    if (v4l2_fd_ < 0 || v4l2_bufs_.empty()) return false;
    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) return false;
    if (buf.index >= v4l2_bufs_.size()) {
        ioctl(v4l2_fd_, VIDIOC_QBUF, &buf);
        return false;
    }
    w = v4l2_w_;
    h = v4l2_h_;
    out.resize(static_cast<size_t>(w * h));
    const auto* src = static_cast<const uint8_t*>(v4l2_bufs_[buf.index].start);
    if (v4l2_pixfmt_ == V4L2_PIX_FMT_RGB565) {
        std::memcpy(out.data(), src, out.size() * sizeof(uint16_t));
    } else if (v4l2_pixfmt_ == V4L2_PIX_FMT_YUYV) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; x += 2) {
                const size_t i = static_cast<size_t>((y * w + x) * 2);
                const int y0 = src[i];
                const int u = src[i + 1];
                const int y1 = src[i + 2];
                const int v = src[i + 3];
                auto yuv = [](int yy, int uu, int vv, uint8_t& r, uint8_t& g, uint8_t& b) {
                    const int c = yy - 16;
                    const int d = uu - 128;
                    const int e = vv - 128;
                    r = static_cast<uint8_t>(std::max(0, std::min(255, (298 * c + 409 * e + 128) >> 8)));
                    g = static_cast<uint8_t>(
                        std::max(0, std::min(255, (298 * c - 100 * d - 208 * e + 128) >> 8)));
                    b = static_cast<uint8_t>(std::max(0, std::min(255, (298 * c + 516 * d + 128) >> 8)));
                };
                uint8_t r, g, b;
                yuv(y0, u, v, r, g, b);
                out[static_cast<size_t>(y * w + x)] = pack_rgb565(r, g, b);
                if (x + 1 < w) {
                    yuv(y1, u, v, r, g, b);
                    out[static_cast<size_t>(y * w + x + 1)] = pack_rgb565(r, g, b);
                }
            }
        }
    } else {
        // MJPEG or unknown: keep last frame / black
        std::fill(out.begin(), out.end(), 0);
    }
    ioctl(v4l2_fd_, VIDIOC_QBUF, &buf);
    return true;
#endif
}

void CameraSession::worker_loop() {
    constexpr int kW = 320;
    constexpr int kH = 134; // content height when chrome visible; screen may resize
    std::vector<uint16_t> raw;
    while (!cancel_.load()) {
        int w = kW;
        int h = kH;
        bool ok = false;
        if (backend_ == "simulation") {
            generate_simulation_frame(raw, w, h, tick_++);
            ok = true;
        } else if (backend_ == "libcamera") {
            ok = grab_libcamera(raw, w, h);
            if (!ok) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
        } else if (backend_ == "v4l2") {
            ok = grab_v4l2(raw, w, h);
            if (!ok) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
        } else {
            break;
        }

        const float gain = brightness_.load();
        tools::camfilter::apply_gain_rgb565(raw, gain);
        const auto filt = static_cast<tools::camfilter::Filter>(filter_.load());
        tools::camfilter::apply_rgb565(raw, w, h, filt);

        {
            std::lock_guard<std::mutex> lock(frame_mu_);
            latest_.width = w;
            latest_.height = h;
            latest_.rgb565 = raw;
            frame_ready_ = true;
        }

        if (recording_.load() && w > 0 && h > 0 &&
            raw.size() >= static_cast<size_t>(w) * static_cast<size_t>(h)) {
            try {
                std::vector<uint16_t> scaled;
                const uint16_t* src = raw.data();
                int fw = w;
                int fh = h;
                if (w != avi_w_ || h != avi_h_) {
                    scale_rgb565(raw, w, h, scaled, avi_w_, avi_h_);
                    src = scaled.data();
                    fw = avi_w_;
                    fh = avi_h_;
                }
                std::vector<uint8_t> rgb(static_cast<size_t>(fw) * static_cast<size_t>(fh) * 3);
                for (size_t i = 0; i < static_cast<size_t>(fw) * static_cast<size_t>(fh); ++i) {
                    uint8_t r, g, b;
                    unpack_rgb565(src[i], r, g, b);
                    rgb[i * 3] = r;
                    rgb[i * 3 + 1] = g;
                    rgb[i * 3 + 2] = b;
                }
                std::vector<uint8_t> jpeg;
                std::string err;
                if (encode_jpeg_mem(rgb, fw, fh, jpeg, err)) append_avi_frame(jpeg);
            } catch (const std::exception&) {
                // Drop this frame. A failed encode must not kill the camera thread.
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // ~10 fps
    }
}

bool decode_image_rgb565(const std::string& path, std::vector<uint16_t>& out, int& width, int& height,
                         std::string& error, int max_pixels) {
    out.clear();
    width = 0;
    height = 0;
    error.clear();
    const std::filesystem::path p(path);
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto finish_rgb = [&](std::vector<uint8_t>& rgb, int w, int h) {
        int dw = w;
        int dh = h;
        if (w * h > max_pixels && max_pixels > 0) {
            const float scale = std::sqrt(static_cast<float>(max_pixels) / static_cast<float>(w * h));
            dw = std::max(1, static_cast<int>(w * scale));
            dh = std::max(1, static_cast<int>(h * scale));
        }
        out.resize(static_cast<size_t>(dw * dh));
        for (int y = 0; y < dh; ++y) {
            const int sy = y * h / dh;
            for (int x = 0; x < dw; ++x) {
                const int sx = x * w / dw;
                const size_t si = static_cast<size_t>(sy * w + sx) * 3;
                out[static_cast<size_t>(y * dw + x)] = pack_rgb565(rgb[si], rgb[si + 1], rgb[si + 2]);
            }
        }
        width = dw;
        height = dh;
        return true;
    };

    if (ext == ".png") {
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) {
            error = "open failed";
            return false;
        }
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        png_infop info = png ? png_create_info_struct(png) : nullptr;
        if (!png || !info || setjmp(png_jmpbuf(png))) {
            if (png) png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
            std::fclose(fp);
            error = "png decode failed";
            return false;
        }
        png_init_io(png, fp);
        png_read_info(png, info);
        const int w = static_cast<int>(png_get_image_width(png, info));
        const int h = static_cast<int>(png_get_image_height(png, info));
        const png_byte color = png_get_color_type(png, info);
        const png_byte bit = png_get_bit_depth(png, info);
        if (bit == 16) png_set_strip_16(png);
        if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
        if (color == PNG_COLOR_TYPE_GRAY && bit < 8) png_set_expand_gray_1_2_4_to_8(png);
        if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
        if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
        if (color == PNG_COLOR_TYPE_RGB_ALPHA || color == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_strip_alpha(png);
        }
        png_read_update_info(png, info);
        std::vector<uint8_t> rgb(static_cast<size_t>(w * h * 3));
        std::vector<png_bytep> rows(static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) {
            rows[static_cast<size_t>(y)] = rgb.data() + static_cast<size_t>(y) * w * 3;
        }
        png_read_image(png, rows.data());
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return finish_rgb(rgb, w, h);
    }

    if (ext == ".jpg" || ext == ".jpeg") {
        FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) {
            error = "open failed";
            return false;
        }
        jpeg_decompress_struct cinfo{};
        jpeg_error_mgr jerr{};
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, fp);
        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            std::fclose(fp);
            error = "jpeg header failed";
            return false;
        }
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);
        const int w = static_cast<int>(cinfo.output_width);
        const int h = static_cast<int>(cinfo.output_height);
        std::vector<uint8_t> rgb(static_cast<size_t>(w * h * 3));
        while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW row = &rgb[static_cast<size_t>(cinfo.output_scanline) * w * 3];
            jpeg_read_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        std::fclose(fp);
        return finish_rgb(rgb, w, h);
    }

    error = "unsupported image type";
    return false;
}

bool decode_jpeg_rgb565(const uint8_t* data, size_t size, std::vector<uint16_t>& out, int& width, int& height,
                        std::string& error, int max_pixels) {
    out.clear();
    width = 0;
    height = 0;
    error.clear();
    if (!data || size < 4) {
        error = "empty jpeg";
        return false;
    }
    jpeg_decompress_struct cinfo{};
    JpegErrMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_fail;
    // malloc, not a C++ vector: libjpeg errors longjmp, which must not skip a destructor.
    uint8_t* rgb = nullptr;
    volatile bool created = false;
    int w = 0;
    int h = 0;
    if (setjmp(jerr.jump)) {
        if (created) jpeg_destroy_decompress(&cinfo);
        std::free(rgb);
        error = "jpeg decode failed";
        return false;
    }
    jpeg_create_decompress(&cinfo);
    created = true;
    jpeg_mem_src(&cinfo, const_cast<unsigned char*>(data), static_cast<unsigned long>(size));
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        error = "jpeg header failed";
        return false;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    w = static_cast<int>(cinfo.output_width);
    h = static_cast<int>(cinfo.output_height);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        jpeg_destroy_decompress(&cinfo);
        error = "bad jpeg size";
        return false;
    }
    rgb = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * 3));
    if (!rgb) {
        jpeg_destroy_decompress(&cinfo);
        error = "out of memory";
        return false;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb + static_cast<size_t>(cinfo.output_scanline) * static_cast<size_t>(w) * 3;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    created = false;

    int dw = w;
    int dh = h;
    if (w * h > max_pixels && max_pixels > 0) {
        const float scale = std::sqrt(static_cast<float>(max_pixels) / static_cast<float>(w * h));
        dw = std::max(1, static_cast<int>(w * scale));
        dh = std::max(1, static_cast<int>(h * scale));
    }
    out.resize(static_cast<size_t>(dw) * static_cast<size_t>(dh));
    for (int y = 0; y < dh; ++y) {
        const int sy = y * h / dh;
        for (int x = 0; x < dw; ++x) {
            const int sx = x * w / dw;
            const size_t si = (static_cast<size_t>(sy) * static_cast<size_t>(w) + static_cast<size_t>(sx)) * 3;
            out[static_cast<size_t>(y * dw + x)] = pack_rgb565(rgb[si], rgb[si + 1], rgb[si + 2]);
        }
    }
    std::free(rgb);
    width = dw;
    height = dh;
    return true;
}

} // namespace platform

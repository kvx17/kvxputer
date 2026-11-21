#include "gallery_screen.h"

#include "asset_manager.h"
#include "camera_session.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

extern "C" void lv_image_cache_drop(const void* src);

namespace screen {
namespace {

bool is_image_ext(std::string ext) {
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

bool is_video_ext(std::string ext) {
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".avi" || ext == ".mp4" || ext == ".mov" || ext == ".mkv";
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Index MJPEG '00dc' chunks so gallery can thumbnail and play camera recordings.
bool index_mjpeg(const std::string& path, std::vector<uint32_t>& off, std::vector<uint32_t>& len) {
    off.clear();
    len.clear();
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    // Stream the file. A full-file copy is large enough to kill the device when
    // the screen is destroyed during playback.
    uint8_t chunk[64 * 1024];
    uint8_t window[4] = {};
    long base = 0;
    while (true) {
        const size_t got = std::fread(chunk, 1, sizeof(chunk), fp);
        if (got == 0) break;
        bool jumped = false;
        for (size_t i = 0; i < got; ++i) {
            window[0] = window[1];
            window[1] = window[2];
            window[2] = window[3];
            window[3] = chunk[i];
            if (window[0] != '0' || window[1] != '0' || window[2] != 'd' || window[3] != 'c') continue;
            const long marker = base + static_cast<long>(i) - 3;
            if (marker < 0 || std::fseek(fp, marker + 4, SEEK_SET) != 0) {
                std::fclose(fp);
                return !off.empty();
            }
            uint8_t size_bytes[4];
            if (std::fread(size_bytes, 1, 4, fp) != 4) {
                std::fclose(fp);
                return !off.empty();
            }
            const uint32_t n = read_u32_le(size_bytes);
            const long payload = marker + 8;
            uint8_t magic[2] = {};
            const bool jpeg = n >= 16 && n <= 2 * 1024 * 1024 &&
                              std::fread(magic, 1, 2, fp) == 2 && magic[0] == 0xff && magic[1] == 0xd8;
            long resume = marker + 1;
            if (jpeg) {
                off.push_back(static_cast<uint32_t>(payload));
                len.push_back(n);
                resume = payload + static_cast<long>(n) + ((n & 1u) ? 1 : 0);
            }
            if (std::fseek(fp, resume, SEEK_SET) != 0) {
                std::fclose(fp);
                return !off.empty();
            }
            window[0] = window[1] = window[2] = window[3] = 0;
            base = resume;
            jumped = true;
            break;
        }
        if (jumped) continue;
        base += static_cast<long>(got);
        if (got < sizeof(chunk)) break;
    }
    std::fclose(fp);
    return !off.empty();
}

bool first_video_thumb(const std::string& path, platform::CameraFrame& frame) {
    std::vector<uint32_t> off;
    std::vector<uint32_t> len;
    if (!index_mjpeg(path, off, len)) return false;
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::vector<uint8_t> jpeg(len[0]);
    if (std::fseek(fp, static_cast<long>(off[0]), SEEK_SET) != 0 ||
        std::fread(jpeg.data(), 1, jpeg.size(), fp) != jpeg.size()) {
        std::fclose(fp);
        return false;
    }
    std::fclose(fp);
    std::string err;
    return platform::decode_jpeg_rgb565(jpeg.data(), jpeg.size(), frame.rgb565, frame.width, frame.height, err,
                                        64 * 48);
}

uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

uint16_t color_rgb565(lv_color_t c) {
    return pack_rgb565(c.red, c.green, c.blue);
}

void fill_rect(uint8_t* dst, int stride, int plot_w, int plot_h, int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(plot_w, x + w);
    const int y1 = std::min(plot_h, y + h);
    for (int row = y0; row < y1; ++row) {
        auto* line = reinterpret_cast<uint16_t*>(dst + row * stride);
        for (int col = x0; col < x1; ++col) line[col] = color;
    }
}

void stroke_rect(uint8_t* dst, int stride, int plot_w, int plot_h, int x, int y, int w, int h, uint16_t color) {
    fill_rect(dst, stride, plot_w, plot_h, x, y, w, 1, color);
    fill_rect(dst, stride, plot_w, plot_h, x, y + h - 1, w, 1, color);
    fill_rect(dst, stride, plot_w, plot_h, x, y, 1, h, color);
    fill_rect(dst, stride, plot_w, plot_h, x + w - 1, y, 1, h, color);
}

void blit_fit(uint8_t* dst, int stride, int plot_w, int plot_h, int x, int y, int w, int h,
              const uint16_t* src, int sw, int sh) {
    if (!src || sw <= 0 || sh <= 0 || w <= 2 || h <= 2) return;
    const float scale = std::min(static_cast<float>(w - 2) / static_cast<float>(sw),
                                 static_cast<float>(h - 2) / static_cast<float>(sh));
    const int dw = std::max(1, static_cast<int>(sw * scale));
    const int dh = std::max(1, static_cast<int>(sh * scale));
    const int ox = x + (w - dw) / 2;
    const int oy = y + (h - dh) / 2;
    for (int dy = 0; dy < dh; ++dy) {
        const int py = oy + dy;
        if (py < 0 || py >= plot_h) continue;
        const int sy = dy * sh / dh;
        auto* line = reinterpret_cast<uint16_t*>(dst + py * stride);
        for (int dx = 0; dx < dw; ++dx) {
            const int px = ox + dx;
            if (px < 0 || px >= plot_w) continue;
            const int sx = dx * sw / dw;
            line[px] = src[static_cast<size_t>(sy * sw + sx)];
        }
    }
}

} // namespace

GalleryScreen::GalleryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

GalleryScreen::~GalleryScreen() {
    alive_.store(false);
    // Stop playback before releasing the buffer LVGL may still be drawing.
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    stop_video();
    stop_scan();
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_image_set_src(canvas_, nullptr);
    if (draw_) {
        // The canvas buffer is marked allocated. A cache drop would free it,
        // and the explicit destroy below would free it again.
        draw_->header.flags &= ~static_cast<uint32_t>(LV_IMAGE_FLAGS_ALLOCATED);
        lv_image_cache_drop(draw_);
    }
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
    }
    if (draw_) {
        lv_draw_buf_destroy(draw_);
        draw_ = nullptr;
    }
}

void GalleryScreen::build() {
    auto* root = content();
    plot_w_ = view::kScreenWidth;
    plot_h_ = view::kContentHeight;

    draw_ = lv_draw_buf_create(plot_w_, plot_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, plot_w_, plot_h_);
        lv_obj_align(canvas_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);
    }

    hud_ = lv_obj_create(root);
    lv_obj_remove_style_all(hud_);
    lv_obj_set_size(hud_, plot_w_, 16);
    lv_obj_align(hud_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(hud_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hud_, LV_OPA_60, 0);

    const auto tok = tokens();
    auto* font = assets().load_standard_font(11);
    status_lbl_ = lv_label_create(hud_);
    lv_obj_set_style_text_color(status_lbl_, tok.ink, 0);
    if (font) lv_obj_set_style_text_font(status_lbl_, font, 0);
    lv_obj_align(status_lbl_, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(status_lbl_, "Scanning Pictures…");

    start_scan();
    timer_ = lv_timer_create(timer_cb, 80, this);
}

void GalleryScreen::start_scan() {
    cancel_ = false;
    thumb_epoch_ = 0;
    scan_thread_ = std::thread([this]() {
        std::vector<std::string> photos;
        std::vector<std::string> videos;
        std::string err;
        auto collect = [&](const std::filesystem::path& root, bool video) {
            if (!std::filesystem::exists(root)) return;
            std::vector<std::pair<std::filesystem::file_time_type, std::string>> stamped;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied);
                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (cancel_.load()) break;
                if (!it->is_regular_file()) continue;
                const auto ext = it->path().extension().string();
                if (video) {
                    if (!is_video_ext(ext)) continue;
                } else if (!is_image_ext(ext)) {
                    continue;
                }
                std::error_code ec;
                stamped.emplace_back(it->last_write_time(ec), it->path().string());
            }
            std::sort(stamped.begin(), stamped.end(), [](const auto& a, const auto& b) {
                if (a.first != b.first) return a.first > b.first;
                return a.second > b.second;
            });
            auto& dest = video ? videos : photos;
            dest.reserve(stamped.size());
            for (auto& item : stamped) dest.push_back(std::move(item.second));
        };
        try {
            collect(platform::pictures_dir(), false);
            const auto video_root = std::filesystem::path(platform::pictures_dir()).parent_path() / "Videos";
            collect(video_root, true);
            if (photos.empty() && !std::filesystem::exists(platform::pictures_dir())) err = "no Pictures folder";
        } catch (const std::exception& ex) {
            err = ex.what();
        }

        {
            std::lock_guard<std::mutex> lock(files_mu_);
            photos_ = photos;
            videos_ = videos;
            photo_thumbs_.assign(photos.size(), Thumb{});
            video_thumbs_.assign(videos.size(), Thumb{});
            error_ = err;
            const auto& active = show_videos_ ? videos_ : photos_;
            if (sel_ >= static_cast<int>(active.size())) sel_ = 0;
        }
        thumb_epoch_.fetch_add(1);
        grid_dirty_ = true;

        auto publish = [&](bool video, size_t i, Thumb thumb) {
            std::lock_guard<std::mutex> lock(files_mu_);
            auto& thumbs = video ? video_thumbs_ : photo_thumbs_;
            if (i < thumbs.size()) thumbs[i] = std::move(thumb);
        };
        try {
            for (size_t i = 0; i < photos.size() && !cancel_.load(); ++i) {
                Thumb thumb;
                std::string dec_err;
                if (platform::decode_image_rgb565(photos[i], thumb.px, thumb.w, thumb.h, dec_err, 64 * 48)) {
                    thumb.ready = true;
                }
                publish(false, i, std::move(thumb));
                thumb_epoch_.fetch_add(1);
                grid_dirty_ = true;
            }
            for (size_t i = 0; i < videos.size() && !cancel_.load(); ++i) {
                Thumb thumb;
                platform::CameraFrame frame;
                if (first_video_thumb(videos[i], frame)) {
                    thumb.px = std::move(frame.rgb565);
                    thumb.w = frame.width;
                    thumb.h = frame.height;
                    thumb.ready = true;
                }
                publish(true, i, std::move(thumb));
                thumb_epoch_.fetch_add(1);
                grid_dirty_ = true;
            }
        } catch (const std::exception&) {
            // A failed thumbnail must not terminate the process while the screen is closing.
        }
    });
}

void GalleryScreen::stop_scan() {
    cancel_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
}

void GalleryScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<GalleryScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void GalleryScreen::on_timer() {
    if (!alive_.load()) return;
    if (mode_ == Mode::Detail && show_videos_ && video_fp_ && !video_off_.empty()) {
        if (++play_tick_ >= 2) {
            play_tick_ = 0;
            video_frame_ = (video_frame_ + 1) % static_cast<int>(video_off_.size());
            if (show_video_frame(video_frame_)) draw_image();
        }
    }
    if (mode_ == Mode::Grid) {
        const int epoch = thumb_epoch_.load();
        if (grid_dirty_.exchange(false) || epoch != drawn_epoch_) {
            draw_grid();
            drawn_epoch_ = epoch;
        }
    }
    update_hud();
}

int GalleryScreen::file_count() {
    std::lock_guard<std::mutex> lock(files_mu_);
    return static_cast<int>((show_videos_ ? videos_ : photos_).size());
}

void GalleryScreen::move_grid(int dcol, int drow) {
    std::lock_guard<std::mutex> lock(files_mu_);
    const auto& files = show_videos_ ? videos_ : photos_;
    const int n = static_cast<int>(files.size());
    if (n <= 0) return;
    const int rows = (n + kCols - 1) / kCols;
    int col = sel_ % kCols;
    int row = sel_ / kCols;
    col = std::clamp(col + dcol, 0, kCols - 1);
    row = std::clamp(row + drow, 0, rows - 1);
    int idx = row * kCols + col;
    if (idx >= n) idx = n - 1;
    if (idx == sel_) return;
    sel_ = idx;
    grid_dirty_ = true;
}

void GalleryScreen::step_linear(int delta) {
    std::lock_guard<std::mutex> lock(files_mu_);
    const auto& files = show_videos_ ? videos_ : photos_;
    const int n = static_cast<int>(files.size());
    if (n <= 0) return;
    const int next = std::clamp(sel_ + delta, 0, n - 1);
    if (next == sel_) return;
    sel_ = next;
    grid_dirty_ = true;
}

void GalleryScreen::step_detail(int delta) {
    {
        std::lock_guard<std::mutex> lock(files_mu_);
        const auto& files = show_videos_ ? videos_ : photos_;
        const int n = static_cast<int>(files.size());
        if (n <= 0) return;
        const int next = std::clamp(sel_ + delta, 0, n - 1);
        if (next == sel_) return;
        sel_ = next;
    }
    if (show_videos_) {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(files_mu_);
            if (sel_ >= 0 && sel_ < static_cast<int>(videos_.size())) path = videos_[static_cast<size_t>(sel_)];
        }
        start_video(path);
        draw_image();
        return;
    }
    load_current_image();
    draw_image();
}

void GalleryScreen::load_current_image() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(files_mu_);
        const auto& files = show_videos_ ? videos_ : photos_;
        if (files.empty() || sel_ < 0 || sel_ >= static_cast<int>(files.size())) {
            error_ = show_videos_ ? "no video" : "no image";
            return;
        }
        path = files[static_cast<size_t>(sel_)];
    }
    std::string err;
    if (!platform::decode_image_rgb565(path, pixels_, img_w_, img_h_, err)) {
        error_ = err.empty() ? "decode failed" : err;
        pixels_.clear();
        img_w_ = img_h_ = 0;
        return;
    }
    error_.clear();
    reset_view();
}

void GalleryScreen::open_detail() {
    if (file_count() <= 0) return;
    mode_ = Mode::Detail;
    if (hud_) lv_obj_add_flag(hud_, LV_OBJ_FLAG_HIDDEN);
    if (show_videos_) {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(files_mu_);
            if (sel_ >= 0 && sel_ < static_cast<int>(videos_.size())) path = videos_[static_cast<size_t>(sel_)];
        }
        reset_view();
        start_video(path);
    } else {
        stop_video();
        load_current_image();
    }
    draw_image();
}

void GalleryScreen::close_detail() {
    stop_video();
    mode_ = Mode::Grid;
    if (hud_ && chrome_on_) lv_obj_clear_flag(hud_, LV_OBJ_FLAG_HIDDEN);
    grid_dirty_ = true;
    draw_grid();
}

void GalleryScreen::show_library(bool videos) {
    if (show_videos_ == videos && mode_ == Mode::Grid) return;
    stop_video();
    show_videos_ = videos;
    sel_ = 0;
    scroll_row_ = 0;
    reset_view();
    if (mode_ == Mode::Detail) {
        mode_ = Mode::Grid;
        if (hud_ && chrome_on_) lv_obj_clear_flag(hud_, LV_OBJ_FLAG_HIDDEN);
    }
    error_.clear();
    grid_dirty_ = true;
    draw_grid();
}

void GalleryScreen::stop_video() {
    if (video_fp_) std::fclose(video_fp_);
    video_fp_ = nullptr;
    video_off_.clear();
    video_len_.clear();
    video_frame_ = 0;
    play_tick_ = 0;
}

bool GalleryScreen::start_video(const std::string& path) {
    stop_video();
    pixels_.clear();
    img_w_ = img_h_ = 0;
    if (path.empty() || !index_mjpeg(path, video_off_, video_len_)) {
        error_ = "cannot play this video";
        return false;
    }
    video_fp_ = std::fopen(path.c_str(), "rb");
    if (!video_fp_) {
        error_ = "cannot open video";
        video_off_.clear();
        video_len_.clear();
        return false;
    }
    error_.clear();
    return show_video_frame(0);
}

bool GalleryScreen::show_video_frame(int index) {
    if (!alive_.load() || !video_fp_ || index < 0 || index >= static_cast<int>(video_off_.size()) ||
        index >= static_cast<int>(video_len_.size())) {
        return false;
    }
    std::vector<uint8_t> jpeg(video_len_[static_cast<size_t>(index)]);
    if (std::fseek(video_fp_, static_cast<long>(video_off_[static_cast<size_t>(index)]), SEEK_SET) != 0 ||
        std::fread(jpeg.data(), 1, jpeg.size(), video_fp_) != jpeg.size()) {
        error_ = "video read failed";
        return false;
    }
    std::string err;
    if (!platform::decode_jpeg_rgb565(jpeg.data(), jpeg.size(), pixels_, img_w_, img_h_, err, 320 * 240)) {
        error_ = err.empty() ? "video decode failed" : err;
        return false;
    }
    video_frame_ = index;
    error_.clear();
    return true;
}

void GalleryScreen::reset_view() {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
}

bool GalleryScreen::zoomed() const {
    return zoom_ > 1.01f;
}

void GalleryScreen::clamp_pan() {
    if (!zoomed() || img_w_ <= 0 || img_h_ <= 0) {
        pan_x_ = 0.0f;
        pan_y_ = 0.0f;
        return;
    }
    const float fit = std::min(static_cast<float>(plot_w_) / static_cast<float>(img_w_),
                               static_cast<float>(plot_h_) / static_cast<float>(img_h_));
    const float scale = fit * zoom_;
    const float max_x = std::max(0.0f, (img_w_ * scale - static_cast<float>(plot_w_)) * 0.5f);
    const float max_y = std::max(0.0f, (img_h_ * scale - static_cast<float>(plot_h_)) * 0.5f);
    pan_x_ = std::clamp(pan_x_, -max_x, max_x);
    pan_y_ = std::clamp(pan_y_, -max_y, max_y);
}

void GalleryScreen::set_zoom(float zoom) {
    zoom_ = std::clamp(zoom, 1.0f, 8.0f);
    if (!zoomed()) {
        zoom_ = 1.0f;
        pan_x_ = 0.0f;
        pan_y_ = 0.0f;
    } else {
        clamp_pan();
    }
    draw_image();
}

void GalleryScreen::pan_by(float dx, float dy) {
    pan_x_ += dx;
    pan_y_ += dy;
    clamp_pan();
    draw_image();
}

void GalleryScreen::draw_grid() {
    if (!alive_.load() || !draw_ || !draw_->data || !canvas_) return;
    auto* dst = static_cast<uint8_t*>(draw_->data);
    std::memset(dst, 0, static_cast<size_t>(stride_ * plot_h_));

    const int hud_h = (hud_ && !lv_obj_has_flag(hud_, LV_OBJ_FLAG_HIDDEN)) ? 16 : 0;
    const int view_h = std::max(1, plot_h_ - hud_h);
    constexpr int kGap = 3;
    const int cell_w = std::max(8, (plot_w_ - kGap * (kCols + 1)) / kCols);
    // Split the free vertical space into as many rows as stay readable, so a
    // single strip does not leave the rest of the screen empty.
    int visible_rows = std::max(2, (view_h - kGap) / (32 + kGap));
    int cell_h = (view_h - kGap * (visible_rows + 1)) / std::max(1, visible_rows);
    while (visible_rows > 2 && cell_h < 30) {
        --visible_rows;
        cell_h = (view_h - kGap * (visible_rows + 1)) / visible_rows;
    }
    cell_h = std::max(8, cell_h);
    const int row_stride = cell_h + kGap;

    std::vector<Thumb> thumbs;
    int n = 0;
    int sel = 0;
    {
        std::lock_guard<std::mutex> lock(files_mu_);
        const auto& files = show_videos_ ? videos_ : photos_;
        n = static_cast<int>(files.size());
        sel = sel_;
        thumbs = show_videos_ ? video_thumbs_ : photo_thumbs_;
    }
    if (n <= 0) {
        lv_obj_invalidate(canvas_);
        return;
    }

    const int rows = (n + kCols - 1) / kCols;
    int row = sel / kCols;
    if (row < scroll_row_) scroll_row_ = row;
    if (row >= scroll_row_ + visible_rows) scroll_row_ = row - visible_rows + 1;
    scroll_row_ = std::clamp(scroll_row_, 0, std::max(0, rows - visible_rows));

    const uint16_t border = color_rgb565(tokens().accent);
    const uint16_t placeholder = pack_rgb565(32, 32, 32);

    for (int r = 0; r < visible_rows; ++r) {
        const int src_row = scroll_row_ + r;
        if (src_row >= rows) break;
        for (int c = 0; c < kCols; ++c) {
            const int idx = src_row * kCols + c;
            if (idx >= n) break;
            const int x = kGap + c * (cell_w + kGap);
            const int y = kGap + r * row_stride;
            fill_rect(dst, stride_, plot_w_, plot_h_, x, y, cell_w, cell_h, placeholder);
            if (idx < static_cast<int>(thumbs.size()) && thumbs[static_cast<size_t>(idx)].ready) {
                const auto& thumb = thumbs[static_cast<size_t>(idx)];
                blit_fit(dst, stride_, plot_w_, plot_h_, x, y, cell_w, cell_h, thumb.px.data(), thumb.w,
                         thumb.h);
            }
            if (idx == sel) {
                stroke_rect(dst, stride_, plot_w_, plot_h_, x, y, cell_w, cell_h, border);
                stroke_rect(dst, stride_, plot_w_, plot_h_, x + 1, y + 1, cell_w - 2, cell_h - 2, border);
            }
            if (show_videos_) {
                const int cx = x + cell_w / 2;
                const int cy = y + cell_h / 2;
                const int s = std::max(4, std::min(cell_w, cell_h) / 5);
                for (int row = -s; row <= s; ++row) {
                    const int half = (s - std::abs(row)) / 2;
                    fill_rect(dst, stride_, plot_w_, plot_h_, cx - 1, cy + row, half + 2, 1, 0xffff);
                }
            }
        }
    }
    lv_obj_invalidate(canvas_);
}

void GalleryScreen::draw_image() {
    if (!alive_.load() || !draw_ || !draw_->data || !canvas_ || pixels_.empty() || img_w_ <= 0 || img_h_ <= 0) return;
    auto* dst = static_cast<uint8_t*>(draw_->data);
    std::memset(dst, 0, static_cast<size_t>(stride_ * plot_h_));

    const float fit = std::min(static_cast<float>(plot_w_) / static_cast<float>(img_w_),
                               static_cast<float>(plot_h_) / static_cast<float>(img_h_));
    const float scale = fit * zoom_;
    const float draw_w = img_w_ * scale;
    const float draw_h = img_h_ * scale;
    const float ox = (plot_w_ - draw_w) * 0.5f + pan_x_;
    const float oy = (plot_h_ - draw_h) * 0.5f + pan_y_;

    for (int y = 0; y < plot_h_; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(dst + y * stride_);
        const float sy = (static_cast<float>(y) - oy) / scale;
        if (sy < 0.0f || sy >= static_cast<float>(img_h_)) continue;
        const int iy = static_cast<int>(sy);
        for (int x = 0; x < plot_w_; ++x) {
            const float sx = (static_cast<float>(x) - ox) / scale;
            if (sx < 0.0f || sx >= static_cast<float>(img_w_)) continue;
            const int ix = static_cast<int>(sx);
            row[x] = pixels_[static_cast<size_t>(iy * img_w_ + ix)];
        }
    }
    lv_obj_invalidate(canvas_);
}

void GalleryScreen::update_hud() {
    if (!status_lbl_ || !alive_.load()) return;
    char buf[180];
    {
        std::lock_guard<std::mutex> lock(files_mu_);
        const auto& files = show_videos_ ? videos_ : photos_;
        const int n = static_cast<int>(files.size());
        const char* kind = show_videos_ ? "Videos" : "Photos";
        if (n <= 0) {
            std::snprintf(buf, sizeof(buf), "%s",
                          error_.empty() ? (show_videos_ ? "No videos — P for photos" : "No photos — V for videos")
                                         : error_.c_str());
        } else if (mode_ == Mode::Detail) {
            const std::string name = std::filesystem::path(files[static_cast<size_t>(sel_)]).filename().string();
            std::snprintf(buf, sizeof(buf), "%s  %s  z%.1f%s%s", kind, name.c_str(), zoom_,
                          error_.empty() ? "" : "  ", error_.c_str());
        } else {
            const std::string name = std::filesystem::path(files[static_cast<size_t>(sel_)]).filename().string();
            std::snprintf(buf, sizeof(buf), "%s  %d/%d  %s", kind, sel_ + 1, n, name.c_str());
        }
    }
    if (status_lbl_ && alive_.load()) lv_label_set_text(status_lbl_, buf);
}

std::array<view::widgets::ActionSlot, 5> GalleryScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             if (mode_ == Mode::Detail) close_detail();
             else view_model().go_back();
         }},
        {view::ICON_CARET_UP,
         [this]() {
             if (mode_ == Mode::Detail) set_zoom(zoom_ / 1.25f);
             else step_linear(-1);
         }},
        {view::ICON_CHECK,
         [this]() {
             if (mode_ == Mode::Grid) open_detail();
         }},
        {view::ICON_CARET_DOWN,
         [this]() {
             if (mode_ == Mode::Detail) set_zoom(zoom_ * 1.25f);
             else step_linear(1);
         }},
        {view::ICON_ARROW_BACK,
         [this]() {
             if (mode_ == Mode::Detail) {
                 reset_view();
                 draw_image();
             }
         }},
    }};
}

bool GalleryScreen::handle_key(uint32_t key) {
    if (key == 'v' || key == 'V') {
        show_library(true);
        return true;
    }
    if (key == 'p' || key == 'P') {
        show_library(false);
        return true;
    }
    if (key == LV_KEY_ESC || key == '4') {
        if (mode_ == Mode::Detail) {
            close_detail();
            return true;
        }
        view_model().go_back();
        return true;
    }
    if (key == '1') {
        chrome_on_ = !chrome_on_;
        set_chrome_visible(chrome_on_);
        if (hud_) {
            if (chrome_on_) lv_obj_clear_flag(hud_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(hud_, LV_OBJ_FLAG_HIDDEN);
        }
        plot_h_ = chrome_on_ ? view::kContentHeight : view::kScreenHeight;
        if (canvas_) lv_obj_set_size(canvas_, plot_w_, plot_h_);
        if (mode_ == Mode::Detail) draw_image();
        else {
            grid_dirty_ = true;
            draw_grid();
        }
        return true;
    }

    // Arrow keys always move. The 4–8 bar keeps its own actions.
    if (mode_ == Mode::Grid) {
        if (key == LV_KEY_LEFT) {
            move_grid(-1, 0);
            return true;
        }
        if (key == LV_KEY_RIGHT) {
            move_grid(1, 0);
            return true;
        }
        if (key == LV_KEY_UP) {
            move_grid(0, -1);
            return true;
        }
        if (key == LV_KEY_DOWN) {
            move_grid(0, 1);
            return true;
        }
        if (key == '5') {
            step_linear(-1);
            return true;
        }
        if (key == '7') {
            step_linear(1);
            return true;
        }
        if (key == LV_KEY_ENTER || key == '\n' || key == ' ' || key == '6') {
            open_detail();
            return true;
        }
        if (key == '8') return true;
        return false;
    }

    // Zoomed: arrows pan inside the photo. Back at 1x they move between photos.
    if (zoomed()) {
        if (key == LV_KEY_LEFT) {
            pan_by(28.0f, 0.0f);
            return true;
        }
        if (key == LV_KEY_RIGHT) {
            pan_by(-28.0f, 0.0f);
            return true;
        }
        if (key == LV_KEY_UP) {
            pan_by(0.0f, 28.0f);
            return true;
        }
        if (key == LV_KEY_DOWN) {
            pan_by(0.0f, -28.0f);
            return true;
        }
    } else {
        if (key == LV_KEY_LEFT || key == LV_KEY_UP) {
            step_detail(-1);
            return true;
        }
        if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) {
            step_detail(1);
            return true;
        }
    }
    if (key == '5') {
        set_zoom(zoom_ / 1.25f);
        return true;
    }
    if (key == '7') {
        set_zoom(zoom_ * 1.25f);
        return true;
    }
    if (key == '8') {
        reset_view();
        draw_image();
        return true;
    }
    if (key == '6' || key == LV_KEY_ENTER || key == '\n' || key == ' ') {
        return true;
    }
    return false;
}

} // namespace screen

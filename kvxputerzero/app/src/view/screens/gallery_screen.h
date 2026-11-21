#pragma once

#include "kvx_screen.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class GalleryScreen final : public KvxScreen {
public:
    GalleryScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~GalleryScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Mode { Grid, Detail };

    struct Thumb {
        std::vector<uint16_t> px;
        int w{0};
        int h{0};
        bool ready{false};
    };

    void start_scan();
    void stop_scan();
    void on_timer();
    void open_detail();
    void close_detail();
    void load_current_image();
    void draw_image();
    void draw_grid();
    void reset_view();
    void update_hud();
    bool zoomed() const;
    void set_zoom(float zoom);
    void pan_by(float dx, float dy);
    void clamp_pan();
    void move_grid(int dcol, int drow);
    void step_linear(int delta);
    void step_detail(int delta);
    int file_count();
    void show_library(bool videos);
    void stop_video();
    bool start_video(const std::string& path);
    bool show_video_frame(int index);

    static void timer_cb(lv_timer_t* t);

    static constexpr int kCols = 4;

    Mode mode_{Mode::Grid};
    bool show_videos_{false};
    std::vector<std::string> photos_;
    std::vector<std::string> videos_;
    std::vector<Thumb> photo_thumbs_;
    std::vector<Thumb> video_thumbs_;
    FILE* video_fp_{nullptr};
    std::vector<uint32_t> video_off_;
    std::vector<uint32_t> video_len_;
    int video_frame_{0};
    int play_tick_{0};
    int sel_{0};
    int scroll_row_{0};
    bool chrome_on_{true};
    std::atomic<bool> grid_dirty_{true};

    std::vector<uint16_t> pixels_;
    int img_w_{0};
    int img_h_{0};
    float zoom_{1.0f};
    float pan_x_{0.0f};
    float pan_y_{0.0f};

    lv_obj_t* hud_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    int stride_{0};
    int plot_w_{320};
    int plot_h_{134};

    lv_timer_t* timer_{nullptr};
    std::thread scan_thread_;
    std::atomic<bool> alive_{true};
    std::atomic<bool> cancel_{false};
    std::atomic<int> thumb_epoch_{0};
    int drawn_epoch_{-1};
    std::mutex files_mu_;
    std::string error_;
};

} // namespace screen

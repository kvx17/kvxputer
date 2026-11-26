#pragma once

#include "camera_session.h"
#include "kvx_screen.h"

#include <string>
#include <vector>

namespace screen {

// Live QR scanner: camera viewfinder, decodes codes each few frames and shows
// the payload. (pocket-zero-tools Camera "Tab scans for a QR code".)
class QrScanScreen final : public KvxScreen {
public:
    QrScanScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~QrScanScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    static void timer_cb(lv_timer_t* t);
    void on_timer();
    void draw_preview(const platform::CameraFrame& frame);
    void try_decode(const platform::CameraFrame& frame);
    void stop_session();

    platform::CameraSession session_;
    platform::CameraFrame frame_;
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    lv_obj_t* result_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* timer_{nullptr};
    int stride_{0};
    int w_{0};
    int h_{0};
    int frame_count_{0};
    bool have_result_{false};
    std::string result_text_;
    std::vector<uint8_t> gray_;
};

} // namespace screen

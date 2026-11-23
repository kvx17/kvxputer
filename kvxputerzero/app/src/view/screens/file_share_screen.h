#pragma once

#include "file_share_server.h"
#include "kvx_screen.h"
#include "qr_lib.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace screen {

// QR + credentials while serving $HOME behind login (upload + download).
class FileShareScreen final : public KvxScreen {
public:
    FileShareScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~FileShareScreen() override;

    void on_shown() override;
    void poll_status();

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void paint_qr(const std::string& url);
    void show_error(const std::string& message);
    void refresh_labels();
    void ensure_qr_buffer(int w, int h);

    std::unique_ptr<tools::file_share::Server> server_;
    tools::file_share::Snapshot last_{};
    bool pwd_visible_{false};
    bool started_{false};

    lv_obj_t* qr_image_{nullptr};
    lv_obj_t* error_label_{nullptr};
    lv_obj_t* addr_label_{nullptr};
    lv_obj_t* user_label_{nullptr};
    lv_obj_t* pwd_label_{nullptr};
    lv_obj_t* hint_label_{nullptr};
    lv_timer_t* poll_timer_{nullptr};

    lv_image_dsc_t qr_dsc_{};
    std::vector<uint8_t> qr_pixels_;
    int qr_buf_w_{0};
    int qr_buf_h_{0};
    std::string painted_url_;
};

} // namespace screen

#include "qr_scan_screen.h"

#include "asset_manager.h"
#include "audio.h"
#include "qr_scan.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstring>

namespace screen {

QrScanScreen::QrScanScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

QrScanScreen::~QrScanScreen() {
    stop_session();
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void QrScanScreen::stop_session() {
    session_.stop();
}

void QrScanScreen::build() {
    auto* root = content();
    const auto tok = tokens();

    w_ = view::kScreenWidth;
    h_ = view::kContentHeight;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, w_, h_);
        lv_obj_align(canvas_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);
    }

    auto* font = assets().load_standard_font(11);
    result_ = lv_label_create(root);
    lv_obj_set_style_text_color(result_, tok.accent, 0);
    lv_obj_set_style_bg_color(result_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(result_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(result_, 3, 0);
    if (font) lv_obj_set_style_text_font(result_, font, 0);
    lv_obj_set_width(result_, w_);
    lv_label_set_long_mode(result_, LV_LABEL_LONG_WRAP);
    lv_obj_align(result_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(result_, "Scanning for QR...");

    hint_ = lv_label_create(root);
    lv_obj_set_style_text_color(hint_, tok.accent, 0);
    lv_obj_set_style_bg_color(hint_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hint_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(hint_, 3, 0);
    lv_obj_set_width(hint_, w_);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(hint_, font, 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(hint_, "Enter rescan   Esc back");

    std::string err;
    if (!session_.start(err)) {
        lv_label_set_text_fmt(result_, "Camera: %s", err.empty() ? "unavailable" : err.c_str());
    }
    timer_ = lv_timer_create(timer_cb, 80, this);
}

void QrScanScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<QrScanScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void QrScanScreen::on_timer() {
    if (!session_.copy_preview(frame_)) return;
    draw_preview(frame_);
    if (!have_result_ && (++frame_count_ % 3 == 0)) {
        try_decode(frame_);
    }
}

void QrScanScreen::draw_preview(const platform::CameraFrame& frame) {
    if (!draw_ || !draw_->data || frame.width <= 0 || frame.height <= 0) return;
    auto* data = static_cast<uint8_t*>(draw_->data);

    // Letterbox-fit the frame into the canvas, nearest-neighbor.
    const float sx = static_cast<float>(frame.width) / w_;
    const float sy = static_cast<float>(frame.height) / h_;
    const float s = std::max(sx, sy);
    const int dw = std::max(1, static_cast<int>(frame.width / s));
    const int dh = std::max(1, static_cast<int>(frame.height / s));
    const int ox = (w_ - dw) / 2;
    const int oy = (h_ - dh) / 2;

    for (int y = 0; y < h_; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(data + y * stride_);
        for (int x = 0; x < w_; ++x) {
            const int fx = x - ox;
            const int fy = y - oy;
            if (fx < 0 || fy < 0 || fx >= dw || fy >= dh) {
                row[x] = 0;
                continue;
            }
            const int srcx = std::min(frame.width - 1, static_cast<int>(fx * s));
            const int srcy = std::min(frame.height - 1, static_cast<int>(fy * s));
            row[x] = frame.rgb565[static_cast<size_t>(srcy) * frame.width + srcx];
        }
    }
    if (canvas_) lv_obj_invalidate(canvas_);
}

void QrScanScreen::try_decode(const platform::CameraFrame& frame) {
    const size_t n = static_cast<size_t>(frame.width) * frame.height;
    if (frame.rgb565.size() < n || n == 0) return;
    gray_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const uint16_t px = frame.rgb565[i];
        const int r = ((px >> 11) & 0x1F) << 3;
        const int g = ((px >> 5) & 0x3F) << 2;
        const int b = (px & 0x1F) << 3;
        gray_[i] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
    }
    const auto codes = tools::qr::decode_gray(gray_.data(), frame.width, frame.height);
    if (!codes.empty()) {
        have_result_ = true;
        result_text_ = codes.front();
        if (result_) lv_label_set_text_fmt(result_, "QR: %s", result_text_.c_str());
        std::string e;
        (void)platform::beep(e);
    }
}

bool QrScanScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_session();
        return KvxScreen::handle_key(key);
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        have_result_ = false;
        result_text_.clear();
        if (result_) lv_label_set_text(result_, "Scanning for QR...");
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> QrScanScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { stop_session(); view_model().go_back(); }},
        {nullptr, nullptr},
        {view::ICON_CHECK,
         [this]() {
             have_result_ = false;
             result_text_.clear();
             if (result_) lv_label_set_text(result_, "Scanning for QR...");
         }},
        {nullptr, nullptr},
        {view::ICON_HOUSE, [this]() { stop_session(); view_model().go_home(); }},
    }};
}

} // namespace screen

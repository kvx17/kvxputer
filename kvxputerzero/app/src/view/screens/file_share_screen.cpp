#include "file_share_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace screen {
namespace {

void poll_cb(lv_timer_t* t) {
    auto* self = static_cast<FileShareScreen*>(lv_timer_get_user_data(t));
    if (self) self->poll_status();
}

std::string home_dir() {
    if (const char* h = std::getenv("HOME")) {
        if (*h) return h;
    }
    return tools::config_dir();
}

std::string mask_pwd(const std::string& pwd, bool visible) {
    if (visible) return pwd;
    return std::string(pwd.size(), '*');
}

} // namespace

FileShareScreen::FileShareScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

FileShareScreen::~FileShareScreen() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (server_) server_->stop();
    server_.reset();
}

void FileShareScreen::build() {
    make_header(content(), "File Share");
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* small = assets().load_standard_font(10);
    if (!small) small = &lv_font_montserrat_12;

    constexpr int kTop = 22;
    constexpr int kQrSize = 108;
    constexpr int kLeftPad = 4;
    constexpr int kRightX = 120;

    qr_image_ = lv_image_create(content());
    lv_obj_set_size(qr_image_, kQrSize, kQrSize);
    lv_obj_align(qr_image_, LV_ALIGN_TOP_LEFT, kLeftPad, kTop);
    lv_obj_add_flag(qr_image_, LV_OBJ_FLAG_HIDDEN);

    error_label_ = lv_label_create(content());
    lv_obj_set_width(error_label_, kQrSize);
    lv_obj_align(error_label_, LV_ALIGN_TOP_LEFT, kLeftPad, kTop);
    lv_label_set_long_mode(error_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(error_label_, small, 0);
    lv_obj_set_style_text_color(error_label_, lv_color_hex(view::color::dark::kError), 0);
    lv_label_set_text(error_label_, "starting...");

    auto make_kv = [&](int y, const char* key) {
        auto* lab = lv_label_create(content());
        lv_obj_set_width(lab, view::kScreenWidth - kRightX - 4);
        lv_obj_align(lab, LV_ALIGN_TOP_LEFT, kRightX, y);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(lab, font, 0);
        lv_obj_set_style_text_color(lab, tok.ink, 0);
        lv_label_set_text(lab, key);
        return lab;
    };

    addr_label_ = make_kv(kTop, "URL: ...");
    user_label_ = make_kv(kTop + 28, "User: ...");
    pwd_label_ = make_kv(kTop + 56, "Pwd: ...");

    hint_label_ = lv_label_create(content());
    lv_obj_set_width(hint_label_, view::kScreenWidth - kRightX - 4);
    lv_obj_align(hint_label_, LV_ALIGN_TOP_LEFT, kRightX, kTop + 88);
    lv_label_set_long_mode(hint_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(hint_label_, small, 0);
    lv_obj_set_style_text_color(hint_label_, tok.ink_dim, 0);
    lv_label_set_text(hint_label_, "Space: show pwd");

    poll_timer_ = lv_timer_create(poll_cb, 150, this);
}

void FileShareScreen::on_shown() {
    if (started_) return;
    started_ = true;
    server_ = std::make_unique<tools::file_share::Server>(home_dir(), true, true);
    server_->start();
    poll_status();
}

void FileShareScreen::ensure_qr_buffer(int w, int h) {
    qr_buf_w_ = w;
    qr_buf_h_ = h;
    qr_pixels_.assign(static_cast<size_t>(w * h * 4), 0);
    std::memset(&qr_dsc_, 0, sizeof(qr_dsc_));
    qr_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    qr_dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
    qr_dsc_.header.w = static_cast<uint32_t>(w);
    qr_dsc_.header.h = static_cast<uint32_t>(h);
    qr_dsc_.header.stride = static_cast<uint32_t>(w * 4);
    qr_dsc_.data_size = static_cast<uint32_t>(qr_pixels_.size());
    qr_dsc_.data = qr_pixels_.data();
}

void FileShareScreen::paint_qr(const std::string& url) {
    if (url.empty() || url == painted_url_) return;
    tools::qr::Matrix matrix;
    std::string err;
    if (!tools::qr::encode_text(url, matrix, err) || matrix.size <= 0) {
        show_error(err.empty() ? "QR encode failed" : err);
        return;
    }

    constexpr int kQrSize = 108;
    ensure_qr_buffer(kQrSize, kQrSize);
    const uint32_t on = 0xFF000000u;
    const uint32_t off = 0xFFFFFFFFu;
    for (int y = 0; y < kQrSize; ++y) {
        for (int x = 0; x < kQrSize; ++x) {
            const lv_color32_t c = lv_color32_make((off >> 16) & 0xff, (off >> 8) & 0xff, off & 0xff, 0xff);
            uint8_t* p = qr_pixels_.data() + (static_cast<size_t>(y * kQrSize + x) * 4);
            std::memcpy(p, &c, 4);
        }
    }

    const int margin = 2;
    const int usable = kQrSize - margin * 2;
    const int scale = std::max(1, usable / matrix.size);
    const int drawn = scale * matrix.size;
    const int ox = (kQrSize - drawn) / 2;
    const int oy = (kQrSize - drawn) / 2;
    for (int y = 0; y < matrix.size; ++y) {
        for (int x = 0; x < matrix.size; ++x) {
            if (matrix.modules[static_cast<size_t>(y * matrix.size + x)] == 0) continue;
            const lv_color32_t c = lv_color32_make((on >> 16) & 0xff, (on >> 8) & 0xff, on & 0xff, 0xff);
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = ox + x * scale + dx;
                    const int py = oy + y * scale + dy;
                    if (px < 0 || py < 0 || px >= kQrSize || py >= kQrSize) continue;
                    uint8_t* p = qr_pixels_.data() + (static_cast<size_t>(py * kQrSize + px) * 4);
                    std::memcpy(p, &c, 4);
                }
            }
        }
    }

    if (qr_image_) {
        lv_image_set_src(qr_image_, &qr_dsc_);
        lv_obj_clear_flag(qr_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(qr_image_);
    }
    if (error_label_) lv_obj_add_flag(error_label_, LV_OBJ_FLAG_HIDDEN);
    painted_url_ = url;
}

void FileShareScreen::show_error(const std::string& message) {
    if (qr_image_) lv_obj_add_flag(qr_image_, LV_OBJ_FLAG_HIDDEN);
    if (error_label_) {
        lv_obj_clear_flag(error_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(error_label_, message.c_str());
    }
    painted_url_.clear();
}

void FileShareScreen::refresh_labels() {
    if (addr_label_) {
        const std::string url = last_.url.empty() ? "(no address)" : last_.url;
        lv_label_set_text(addr_label_, ("URL: " + url).c_str());
    }
    if (user_label_) {
        lv_label_set_text(user_label_, ("User: " + last_.auth.user).c_str());
    }
    if (pwd_label_) {
        lv_label_set_text(pwd_label_, ("Pwd: " + mask_pwd(last_.auth.password, pwd_visible_)).c_str());
    }
    if (hint_label_) {
        lv_label_set_text(hint_label_, pwd_visible_ ? "Space: hide pwd" : "Space: show pwd");
    }
}

void FileShareScreen::poll_status() {
    if (!server_) return;
    const auto snap = server_->snapshot();
    const bool changed = snap.running != last_.running || snap.port != last_.port || snap.url != last_.url ||
                         snap.error != last_.error || snap.occupant != last_.occupant ||
                         snap.auth.user != last_.auth.user || snap.auth.password != last_.auth.password;
    last_ = snap;
    if (!changed && !last_.url.empty() && !painted_url_.empty()) {
        refresh_labels();
        return;
    }

    if (!last_.error.empty() && last_.url.empty()) {
        std::string msg = "listen failed";
        if (last_.port) msg += " :" + std::to_string(last_.port);
        msg += "\n" + last_.error;
        if (!last_.occupant.empty()) msg += "\n" + last_.occupant;
        show_error(msg);
    } else if (!last_.url.empty()) {
        paint_qr(last_.url);
        if (!last_.error.empty() && hint_label_) {
            // Bound but no LAN IP warning kept in hint area briefly via addr.
        }
        if (!last_.occupant.empty() && hint_label_ && last_.port != 8080) {
            // Prefer Space hint; occupant already used to pick another port.
        }
    } else if (!last_.running) {
        show_error("starting...");
    }
    refresh_labels();
}

bool FileShareScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        if (server_) server_->stop();
        view_model().go_back();
        return true;
    }
    if (key == ' ') {
        pwd_visible_ = !pwd_visible_;
        refresh_labels();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> FileShareScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             if (server_) server_->stop();
             view_model().go_back();
         }},
        {"", {}},
        {"", {}},
        {"", {}},
        {view::ICON_HOUSE,
         [this]() {
             if (server_) server_->stop();
             view_model().go_home();
         }},
    }};
}

} // namespace screen

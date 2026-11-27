#include "radio_screen.h"

#include "asset_manager.h"
#include "ble_scan.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"
#include "wifi_scan.h"

#include <algorithm>
#include <cstdio>

namespace screen {
namespace {

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
}

} // namespace

RadioScreen::RadioScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

RadioScreen::~RadioScreen() {
    stop_worker();
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void RadioScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Radio");

    header_ = lv_label_create(root);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(header_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(header_, tok.ink_dim, 0);
    lv_obj_set_pos(header_, 4, 22);
    lv_obj_set_width(header_, 312);

    body_ = lv_label_create(root);
    auto* bfont = assets().load_standard_font(12);
    lv_obj_set_style_text_font(body_, bfont ? bfont : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body_, tok.ink, 0);
    lv_obj_set_pos(body_, 4, 38);
    lv_obj_set_width(body_, 312);
    lv_label_set_long_mode(body_, LV_LABEL_LONG_CLIP);

    w_ = view::kScreenWidth;
    h_ = 48;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 0, 50);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }

    hint_ = lv_label_create(root);
    lv_obj_set_style_text_font(hint_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_, tok.ink_dim, 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(hint_, "Up/Down select   Enter watch   Esc back");

    render();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 200, this);
}

void RadioScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    scan_cancel_ = false;
    worker_ = std::thread([this]() {
        while (!cancel_) {
            std::vector<Entry> merged;
            std::string err;

            std::vector<platform::WifiAp> aps = platform::wifi_scan(err, &scan_cancel_);
            for (const auto& a : aps) {
                Entry e;
                e.wifi = true;
                e.label = a.ssid.empty() ? "<hidden>" : a.ssid;
                e.key = a.bssid;
                e.rssi = a.rssi;
                merged.push_back(std::move(e));
            }
            if (cancel_) break;

            std::vector<platform::BleDevice> devs;
            platform::ble_scan_once(devs, err, 3, &scan_cancel_);
            for (const auto& d : devs) {
                Entry e;
                e.wifi = false;
                e.label = d.name.empty() ? d.address : d.name;
                e.key = d.address;
                e.rssi = d.rssi;
                merged.push_back(std::move(e));
            }

            std::sort(merged.begin(), merged.end(),
                      [](const Entry& a, const Entry& b) { return a.rssi > b.rssi; });

            int watched = -127;
            bool found = false;
            if (watching_) {
                for (const auto& e : merged) {
                    if (e.key == watch_key_) {
                        watched = e.rssi;
                        found = true;
                        break;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mu_);
                entries_ = std::move(merged);
                int wc = 0, bc = 0;
                for (const auto& e : entries_) (e.wifi ? wc : bc)++;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%d Wi-Fi  +  %d BLE", wc, bc);
                status_ = buf;
                if (watching_) watch_rssi_ = found ? watched : -127;
            }
            data_ready_ = true;
        }
    });
}

void RadioScreen::stop_worker() {
    cancel_ = true;
    scan_cancel_ = true;  // break out of a blocking wifi/ble scan
    if (worker_.joinable()) worker_.join();
}

void RadioScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RadioScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void RadioScreen::on_timer() {
    if (!data_ready_.exchange(false)) return;
    if (watching_) {
        int r;
        {
            std::lock_guard<std::mutex> lock(mu_);
            r = watch_rssi_;
        }
        trace_.push_back(r);
        while (trace_.size() > static_cast<size_t>(w_)) trace_.pop_front();
    }
    render();
}

void RadioScreen::render() {
    std::vector<Entry> entries;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries = entries_;
        status = status_;
    }

    if (watching_) {
        if (canvas_) lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
        if (header_) lv_label_set_text_fmt(header_, "Watching %s", watch_label_.c_str());
        if (body_) lv_label_set_text_fmt(body_, "%d dBm", watch_rssi_);
        if (hint_) lv_label_set_text(hint_, "Esc back to list");
        // Draw RSSI trace (-100..-30 dBm).
        if (draw_ && draw_->data) {
            auto* data = static_cast<uint8_t*>(draw_->data);
            const uint16_t bg = pack565(8, 10, 14);
            for (int y = 0; y < h_; ++y)
                for (int x = 0; x < w_; ++x) reinterpret_cast<uint16_t*>(data + y * stride_)[x] = bg;
            auto y_of = [&](int dbm) {
                float t = (static_cast<float>(dbm) + 100.f) / 70.f;
                t = std::clamp(t, 0.f, 1.f);
                return h_ - 1 - static_cast<int>(t * (h_ - 2));
            };
            const uint16_t line = pack565(80, 220, 255);
            int i = 0;
            for (int v : trace_) {
                put_px(data, stride_, w_, h_, i, y_of(v), line);
                if (i > 0) {
                    // simple fill to previous for continuity
                }
                ++i;
            }
            if (canvas_) lv_obj_invalidate(canvas_);
        }
        return;
    }

    if (canvas_) lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    if (header_) lv_label_set_text_fmt(header_, "%s", status.c_str());

    if (entries.empty()) {
        if (header_) lv_label_set_text(header_, "Nearby");
        if (body_) lv_label_set_text(body_, "Searching Wi-Fi + Bluetooth...");
        return;
    }
    sel_ = std::clamp(sel_, 0, static_cast<int>(entries.size()) - 1);

    const int rows = 8;
    int start = std::max(0, sel_ - rows / 2);
    if (start + rows > static_cast<int>(entries.size())) start = std::max(0, static_cast<int>(entries.size()) - rows);

    std::string text;
    for (int i = start; i < start + rows && i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[i];
        char line[80];
        std::string lbl = e.label.substr(0, 18);
        std::snprintf(line, sizeof(line), "%s %-18s %4d\n", i == sel_ ? ">" : " ", lbl.c_str(), e.rssi);
        text += (e.wifi ? "W" : "B");
        text += line;
    }
    if (body_) lv_label_set_text(body_, text.c_str());
}

void RadioScreen::enter_watch() {
    std::lock_guard<std::mutex> lock(mu_);
    if (sel_ < 0 || sel_ >= static_cast<int>(entries_.size())) return;
    watch_key_ = entries_[sel_].key;
    watch_label_ = entries_[sel_].label;
    watching_ = true;
    watch_rssi_ = entries_[sel_].rssi;
    trace_.clear();
}

bool RadioScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        if (watching_) {
            watching_ = false;
            render();
            return true;
        }
        stop_worker();
        return KvxScreen::handle_key(key);
    }
    if (watching_) return true;
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        if (sel_ > 0) --sel_;
        render();
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        ++sel_;
        render();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        enter_watch();
        render();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RadioScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { if (watching_) { watching_ = false; render(); } else { stop_worker(); view_model().go_back(); } }},
        {view::ICON_CARET_UP, [this]() { if (!watching_ && sel_ > 0) { --sel_; render(); } }},
        {view::ICON_CHECK, [this]() { if (!watching_) { enter_watch(); render(); } }},
        {view::ICON_CARET_DOWN, [this]() { if (!watching_) { ++sel_; render(); } }},
        {view::ICON_HOUSE, [this]() { stop_worker(); view_model().go_home(); }},
    }};
}

} // namespace screen

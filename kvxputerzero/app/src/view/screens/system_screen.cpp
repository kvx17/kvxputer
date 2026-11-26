#include "system_screen.h"

#include "asset_manager.h"
#include "audio.h"
#include "backlight.h"
#include "device_status.h"
#include "instrument.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/statvfs.h>

namespace screen {
namespace {

constexpr int kBarX = 96;
constexpr int kBarW = 150;

void read_mem(long& total_mb, long& avail_mb) {
    total_mb = avail_mb = -1;
    std::ifstream in("/proc/meminfo");
    std::string key, unit;
    long val;
    while (in >> key >> val >> unit) {
        if (key == "MemTotal:") total_mb = val / 1024;
        else if (key == "MemAvailable:") avail_mb = val / 1024;
        if (total_mb >= 0 && avail_mb >= 0) break;
    }
}

void read_disk(long& total_mb, long& free_mb) {
    total_mb = free_mb = -1;
    struct statvfs s;
    if (statvfs("/", &s) == 0) {
        const double unit = static_cast<double>(s.f_frsize);
        total_mb = static_cast<long>(static_cast<double>(s.f_blocks) * unit / (1024.0 * 1024.0));
        free_mb = static_cast<long>(static_cast<double>(s.f_bavail) * unit / (1024.0 * 1024.0));
    }
}

} // namespace

SystemScreen::SystemScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    int32_t b = -1;
    if (platform::read_brightness_percent(b)) brightness_ = b;
    platform::OutputLevel lvl;
    if (platform::adjust_output_volume(0, lvl)) {
        volume_ = lvl.percent;
        muted_ = lvl.muted;
    }
    init();
}

SystemScreen::~SystemScreen() {
    if (timer_) lv_timer_delete(timer_);
}

void SystemScreen::set_bar(lv_obj_t* fill, int pct) {
    if (!fill) return;
    pct = std::clamp(pct, 0, 100);
    lv_obj_set_width(fill, kBarW * pct / 100);
}

void SystemScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "System");

    auto* font = assets().load_standard_font(12);
    chip_ = view::widgets::make_status_chip(root, "LIVE", view::StatusPillKind::Ok, font, tok);

    struct Spec {
        const char* label;
        lv_obj_t** value;
        lv_obj_t** bar;      // nullptr => no bar (text row)
        bool adjustable;
    };
    Spec specs[] = {
        {"Battery", &batt_val_, &batt_bar_, false},
        {"Memory", &mem_val_, &mem_bar_, false},
        {"Disk", &disk_val_, &disk_bar_, false},
        {"Bright", &bright_val_, &bright_bar_, true},
        {"Volume", &vol_val_, &vol_bar_, true},
    };

    int y = 42;  // start below the top-right status chip so no value collides with it
    for (auto& sp : specs) {
        auto* lbl = lv_label_create(root);
        lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, tok.ink_dim, 0);
        lv_label_set_text(lbl, sp.label);
        lv_obj_set_pos(lbl, 8, y);

        if (sp.bar) {
            auto* track = lv_obj_create(root);
            lv_obj_remove_style_all(track);
            lv_obj_set_size(track, kBarW, 8);
            lv_obj_set_pos(track, kBarX, y + 3);
            lv_obj_set_style_radius(track, 4, 0);
            lv_obj_set_style_bg_color(track, tok.surface, 0);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

            auto* fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_size(fill, 0, 8);
            lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_radius(fill, 4, 0);
            lv_obj_set_style_bg_color(fill, sp.adjustable ? tok.accent : tok.main, 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            *sp.bar = fill;
        }

        auto* val = lv_label_create(root);
        lv_obj_set_style_text_font(val, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(val, sp.adjustable ? tok.accent : tok.ink, 0);
        lv_obj_set_width(val, 60);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, y);
        *sp.value = val;

        y += 18;
    }

    help_.build(root, assets(), tok);

    refresh();
    timer_ = lv_timer_create(timer_cb, 1000, this);
}

void SystemScreen::open_help() {
    if (help_.visible()) {
        help_.hide();
        return;
    }
    help_.show("System",
               "Up / Down   brightness\n"
               "[  /  ]      volume\n"
               "M           mute\n\n"
               "Bars show battery, memory and\n"
               "disk use; brightness and volume\n"
               "are adjusted in place.");
}

void SystemScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<SystemScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void SystemScreen::refresh() {
    const auto st = platform::read_device_status();
    const auto tok = tokens();

    // Network goes in the status chip (top-right), not a row, so nothing collides.
    if (chip_) {
        char net[48];
        if (st.wifi_connected) std::snprintf(net, sizeof(net), "Wi-Fi %d%%", st.wifi_strength_percent);
        else if (st.ethernet_connected) std::snprintf(net, sizeof(net), "Ethernet");
        else std::snprintf(net, sizeof(net), "Offline");
        const bool up = st.wifi_connected || st.ethernet_connected;
        view::set_status_pill(chip_, net, up ? view::StatusPillKind::Ok : view::StatusPillKind::Missing, tok);
    }

    if (batt_val_) {
        if (st.battery_present) {
            lv_label_set_text_fmt(batt_val_, "%d%%%s", st.battery_percent, st.battery_charging ? "+" : "");
            set_bar(batt_bar_, st.battery_percent);
        } else {
            lv_label_set_text(batt_val_, "USB");
            set_bar(batt_bar_, 100);
        }
    }

    long mt, ma, dt, df;
    read_mem(mt, ma);
    read_disk(dt, df);
    if (mem_val_ && mt > 0) {
        const int used = static_cast<int>((mt - ma) * 100 / mt);
        lv_label_set_text_fmt(mem_val_, "%d%%", used);
        set_bar(mem_bar_, used);
    }
    if (disk_val_ && dt > 0) {
        const int used = static_cast<int>((dt - df) * 100 / dt);
        lv_label_set_text_fmt(disk_val_, "%d%%", used);
        set_bar(disk_bar_, used);
    }

    if (bright_val_) {
        lv_label_set_text_fmt(bright_val_, "%d%%", brightness_ < 0 ? 0 : brightness_);
        set_bar(bright_bar_, brightness_ < 0 ? 0 : brightness_);
    }
    if (vol_val_) {
        if (muted_) lv_label_set_text(vol_val_, "mute");
        else lv_label_set_text_fmt(vol_val_, "%d%%", volume_ < 0 ? 0 : volume_);
        set_bar(vol_bar_, muted_ ? 0 : (volume_ < 0 ? 0 : volume_));
    }
}

void SystemScreen::adjust_brightness(int delta) {
    int32_t b = brightness_ < 0 ? 50 : brightness_;
    b = std::clamp(b + delta, 5, 100);
    if (platform::write_brightness_percent(b)) brightness_ = b;
    refresh();
}

void SystemScreen::adjust_volume(int delta) {
    platform::OutputLevel lvl;
    if (platform::adjust_output_volume(delta, lvl)) {
        volume_ = lvl.percent;
        muted_ = lvl.muted;
    }
    refresh();
}

void SystemScreen::toggle_mute() {
    platform::OutputLevel lvl;
    if (platform::toggle_output_mute(lvl)) {
        volume_ = lvl.percent;
        muted_ = lvl.muted;
    }
    refresh();
}

bool SystemScreen::handle_key(uint32_t key) {
    if (help_.visible()) return help_.handle_key(key);
    if (key == '?' || key == platform::kKeyHelp) {
        open_help();
        return true;
    }
    switch (key) {
        case LV_KEY_UP:
        case '+':
        case '=':
            adjust_brightness(10);
            return true;
        case LV_KEY_DOWN:
        case '-':
        case '_':
            adjust_brightness(-10);
            return true;
        case ']':
        case LV_KEY_RIGHT:
            adjust_volume(5);
            return true;
        case '[':
        case LV_KEY_LEFT:
            adjust_volume(-5);
            return true;
        case 'm':
        case 'M':
            toggle_mute();
            return true;
        case LV_KEY_ESC:
            return KvxScreen::handle_key(key);
        default:
            break;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> SystemScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { adjust_brightness(-10); }},
        {view::ICON_INFO, [this]() { open_help(); }},
        {view::ICON_PLUS, [this]() { adjust_brightness(10); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

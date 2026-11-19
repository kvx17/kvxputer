#include "rf_replay_screen.h"
#include "asset_manager.h"

#include "cap_detect.h"
#include "rf_decode.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace screen {
namespace {
namespace fs = std::filesystem;
}

RfReplayScreen::RfReplayScreen(viewmodel::BaseViewModel& vm, app::AssetManager& assets, bool library_mode)
    : KvxScreen(vm, assets), library_mode_(library_mode) {
    settings_ = platform::load_rf_settings();
    override_hz_ = settings_.freq_hz;
    init();
}

RfReplayScreen::~RfReplayScreen() {
    stop_tx();
    if (timer_) lv_timer_delete(timer_);
}

void RfReplayScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, library_mode_ ? "Protocol Library" : "Replay / Custom");

    label_ = lv_label_create(root);
    lv_obj_set_pos(label_, 4, 20);
    lv_obj_set_width(label_, 312);
    lv_obj_set_height(label_, 110);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_, tok.ink, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);

    reload();
    refresh();
    timer_ = lv_timer_create(timer_cb, 200, this);
}

void RfReplayScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RfReplayScreen*>(lv_timer_get_user_data(t));
    if (self) self->refresh();
}

void RfReplayScreen::reload() {
    names_.clear();
    meta_.clear();
    std::string err;
    platform::list_sub_files(platform::rf_dir(), names_, err);
    if (!filter_.empty()) {
        names_.erase(std::remove_if(names_.begin(), names_.end(),
                                    [&](const std::string& n) {
                                        return n.find(filter_) == std::string::npos;
                                    }),
                     names_.end());
    }
    for (const auto& n : names_) {
        platform::SubFileInfo info;
        std::string e;
        if (platform::parse_sub_file(platform::rf_dir() + "/" + n, info, e)) meta_.push_back(info);
        else {
            info.path = n;
            meta_.push_back(info);
        }
    }
    if (index_ >= static_cast<int>(names_.size())) index_ = std::max(0, static_cast<int>(names_.size()) - 1);
    status_ = names_.empty() ? "(no .sub in rf/)" : status_;
}

void RfReplayScreen::refresh() {
    if (!label_) return;
    char buf[512];
    if (editing_) {
        std::snprintf(buf, sizeof(buf), "Pulse editor: %zu pairs\nZ/C add mark/space  Del pop\nEnter save as custom.sub  Esc cancel\n%s",
                      edit_timings_.size(), status_.c_str());
        lv_label_set_text(label_, buf);
        return;
    }
    if (names_.empty()) {
        std::snprintf(buf, sizeof(buf), "%s\nN=new pulses  / filter\n%s", status_.c_str(),
                      library_mode_ ? "Enter=decode" : "Enter=replay");
        lv_label_set_text(label_, buf);
        return;
    }
    const auto& m = meta_[static_cast<size_t>(index_)];
    char meta_line[160];
    std::snprintf(meta_line, sizeof(meta_line), "%s  %s  %s  %zu edges",
                  platform::rf_format_mhz(m.freq_hz ? m.freq_hz : 0).c_str(),
                  m.protocol.empty() ? "?" : m.protocol.c_str(),
                  m.preset.empty() ? "-" : m.preset.c_str(), m.timings_us.size());
    std::snprintf(buf, sizeof(buf),
                  "[%d/%zu] %s\n%s\nrep %d delay %dms %s%s\nF/X select  Enter %s  D del  R rename\n%s",
                  index_ + 1, names_.size(), names_[static_cast<size_t>(index_)].c_str(), meta_line, repeats_,
                  delay_ms_, override_freq_ ? "ovr " : "",
                  override_freq_ ? platform::rf_format_mhz(override_hz_).c_str() : "",
                  library_mode_ ? "decode" : "TX", status_.c_str());
    lv_label_set_text(label_, buf);
}

void RfReplayScreen::select_delta(int d) {
    if (names_.empty()) return;
    index_ = (index_ + d + static_cast<int>(names_.size())) % static_cast<int>(names_.size());
    refresh();
}

void RfReplayScreen::stop_tx() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    tx_active_ = false;
}

void RfReplayScreen::play_selected() {
    if (names_.empty() || tx_active_) return;
    const auto& m = meta_[static_cast<size_t>(index_)];
    if (m.timings_us.empty()) {
        status_ = "No RAW_Data";
        return;
    }
    uint32_t hz = override_freq_ ? override_hz_ : (m.freq_hz ? m.freq_hz : settings_.freq_hz);
    std::string reason;
    if (!platform::rf_tx_allowed(settings_.region, hz, reason)) {
        status_ = reason;
        return;
    }
    // Rolling-code protocol files: refuse TX
    if (!m.protocol.empty() && m.protocol != "RAW") {
        const auto dec = platform::decode_ook_timings(m.timings_us);
        if (dec.rolling) {
            status_ = "Rolling-code: replay disabled";
            return;
        }
    }
    const auto cap = platform::detect_cc1101();
    if (!cap.present) {
        status_ = "Cap not present";
        return;
    }
    cancel_ = false;
    tx_active_ = true;
    status_ = "TX…";
    const auto timings = m.timings_us;
    const int reps = repeats_;
    const int delay = delay_ms_;
    const auto pa = platform::rf_clamp_pa(settings_.region, settings_.pa);
    worker_ = std::thread([this, hz, timings, reps, delay, pa]() {
        platform::Cc1101Session radio;
        std::string err;
        if (!radio.open({}, err)) {
            status_ = err;
            tx_active_ = false;
            return;
        }
        (void)radio.tx_raw_timings(hz, pa, timings, reps, delay, err, &cancel_);
        radio.close();
        status_ = cancel_ ? "Stopped" : "Sent";
        tx_active_ = false;
    });
}

void RfReplayScreen::delete_selected() {
    if (names_.empty()) return;
    const std::string path = platform::rf_dir() + "/" + names_[static_cast<size_t>(index_)];
    std::error_code ec;
    fs::remove(path, ec);
    status_ = ec ? "Delete failed" : "Deleted";
    reload();
}

void RfReplayScreen::rename_selected() {
    if (names_.empty()) return;
    // Simple rename: append _2 before extension
    const std::string& old = names_[static_cast<size_t>(index_)];
    std::string neu = old;
    const auto dot = neu.rfind('.');
    if (dot == std::string::npos) neu += "_2";
    else neu.insert(dot, "_2");
    std::error_code ec;
    fs::rename(platform::rf_dir() + "/" + old, platform::rf_dir() + "/" + neu, ec);
    status_ = ec ? "Rename failed" : ("Renamed " + neu);
    reload();
}

void RfReplayScreen::decode_selected() {
    if (names_.empty()) return;
    const auto& m = meta_[static_cast<size_t>(index_)];
    if (!m.timings_us.empty()) {
        const auto d = platform::decode_ook_timings(m.timings_us);
        status_ = d.summary;
        if (d.ok && library_mode_) {
            const std::string out =
                platform::rf_dir() + "/" + d.protocol + "_" + std::to_string(d.key) + ".sub";
            std::string err;
            const uint32_t hz = m.freq_hz ? m.freq_hz : settings_.freq_hz;
            if (platform::write_sub_protocol(out, hz, d, err)) status_ += " | saved";
            else status_ += " | " + err;
            reload();
        }
        return;
    }
    if (!m.protocol.empty() && m.protocol != "RAW") {
        status_ = m.protocol + " key=" + m.key_hex + " bits=" + std::to_string(m.bit_count);
        return;
    }
    status_ = "Nothing to decode";
}

bool RfReplayScreen::handle_key(uint32_t key) {
    if (tx_active_) {
        stop_tx();
        if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
        return true;
    }
    if (key == LV_KEY_ESC) {
        if (editing_) {
            editing_ = false;
            status_ = "Edit cancelled";
            return true;
        }
        return KvxScreen::handle_key(key);
    }
    if (editing_) {
        if (key == LV_KEY_LEFT || key == 'z' || key == 'Z') {
            edit_timings_.push_back(400);
            return true;
        }
        if (key == LV_KEY_RIGHT || key == 'c' || key == 'C') {
            edit_timings_.push_back(-400);
            return true;
        }
        if (key == LV_KEY_DEL || key == 127) {
            if (!edit_timings_.empty()) edit_timings_.pop_back();
            return true;
        }
        if (key == LV_KEY_ENTER) {
            std::string err;
            const std::string path = platform::rf_dir() + "/custom_" + std::to_string(std::time(nullptr)) + ".sub";
            if (platform::write_sub_raw(path, settings_.freq_hz, edit_timings_, err)) {
                status_ = "Saved custom";
                editing_ = false;
                reload();
            } else status_ = err;
            return true;
        }
        return true;
    }
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        select_delta(-1);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        select_delta(1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        if (library_mode_) decode_selected();
        else play_selected();
        return true;
    }
    if (key == 'd' || key == 'D') {
        delete_selected();
        return true;
    }
    if (key == 'r' || key == 'R') {
        rename_selected();
        return true;
    }
    if (key == 'n' || key == 'N') {
        editing_ = true;
        edit_timings_.clear();
        status_ = "Editing";
        return true;
    }
    if (key == 'o' || key == 'O') {
        override_freq_ = !override_freq_;
        return true;
    }
    if (key == 'z' || key == 'Z') {
        override_hz_ = override_hz_ > settings_.step_hz ? override_hz_ - settings_.step_hz : override_hz_;
        return true;
    }
    if (key == 'c' || key == 'C') {
        override_hz_ += settings_.step_hz;
        return true;
    }
    if (key == '+' || key == '=') {
        repeats_ = std::min(20, repeats_ + 1);
        return true;
    }
    if (key == '-' || key == '_') {
        repeats_ = std::max(1, repeats_ - 1);
        return true;
    }
    if (key == '[' ) {
        delay_ms_ = std::max(0, delay_ms_ - 10);
        return true;
    }
    if (key == ']') {
        delay_ms_ = std::min(2000, delay_ms_ + 10);
        return true;
    }
    if (key == 'g' || key == 'G') {
        settings_.region = settings_.region == platform::RfRegion::Eu ? platform::RfRegion::Us
                                                                     : platform::RfRegion::Eu;
        platform::save_rf_settings(settings_);
        status_ = settings_.region == platform::RfRegion::Us ? "Region US" : "Region EU";
        return true;
    }
    // Quick slots 1-3: store/play favourites paths via settings favourites freqs — play first matching file
    if (key >= '1' && key <= '3') {
        const size_t slot = static_cast<size_t>(key - '1');
        if (slot < settings_.favourites.size() && !names_.empty()) {
            // Pick file closest to favourite freq
            uint32_t want = settings_.favourites[slot];
            int best = 0;
            uint32_t best_d = UINT32_MAX;
            for (size_t i = 0; i < meta_.size(); ++i) {
                const uint32_t d = meta_[i].freq_hz > want ? meta_[i].freq_hz - want : want - meta_[i].freq_hz;
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(i);
                }
            }
            index_ = best;
            if (!library_mode_) play_selected();
            else decode_selected();
        }
        return true;
    }
    // Type-to-filter: letters append
    if (key >= 'a' && key <= 'z') {
        filter_.push_back(static_cast<char>(key));
        reload();
        return true;
    }
    if (key >= 'A' && key <= 'Z') {
        filter_.push_back(static_cast<char>(key - 'A' + 'a'));
        reload();
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == 8) {
        if (!filter_.empty()) filter_.pop_back();
        reload();
        return true;
    }
    if (key == '/') {
        filter_.clear();
        reload();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RfReplayScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { select_delta(-1); }},
        {view::ICON_PLAY,
         [this]() {
             if (library_mode_) decode_selected();
             else play_selected();
         }},
        {view::ICON_CARET_DOWN, [this]() { select_delta(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

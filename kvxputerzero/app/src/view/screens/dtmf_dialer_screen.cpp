#include "dtmf_dialer_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace screen {
namespace {

struct PadTone {
    char label;
    char key;
    float lo;
    float hi;
};

constexpr PadTone kPad[16] = {
    {'1', '1', 697, 1209}, {'2', '2', 697, 1336}, {'3', '3', 697, 1477}, {'A', 'a', 697, 1633},
    {'4', '4', 770, 1209}, {'5', '5', 770, 1336}, {'6', '6', 770, 1477}, {'B', 'b', 770, 1633},
    {'7', '7', 852, 1209}, {'8', '8', 852, 1336}, {'9', '9', 852, 1477}, {'C', 'c', 852, 1633},
    {'*', '*', 941, 1209}, {'0', '0', 941, 1336}, {'#', '#', 941, 1477}, {'D', 'd', 941, 1633},
};

constexpr int kToneMs = 200;
constexpr int kHist = 5;
constexpr int kRate = 22050;
constexpr double kPi = 3.14159265358979323846;

int index_for_key(uint32_t key) {
    // Bare C is the Right arrow outside text entry. Treat that arrow as C so the
    // key still dials. U sits under * and W under #, so those letters dial too.
    if (key == LV_KEY_RIGHT) key = 'c';
    char c = 0;
    if (key >= '0' && key <= '9') c = static_cast<char>(key);
    else if (key >= 'a' && key <= 'z') c = static_cast<char>(key);
    else if (key >= 'A' && key <= 'Z') c = static_cast<char>(key - 'A' + 'a');
    else if (key == '*' || key == '#') c = static_cast<char>(key);
    else return -1;
    if (c == 'u') c = '*';
    if (c == 'w') c = '#';
    for (int i = 0; i < 16; ++i) {
        if (kPad[i].key == c) return i;
    }
    return -1;
}

void format_freq(const PadTone& tone, char* buf, size_t n) {
    std::snprintf(buf, n, "%.0f + %.0f Hz", static_cast<double>(tone.lo), static_cast<double>(tone.hi));
}

lv_obj_t* centered_label(lv_obj_t* parent, const lv_font_t* font, int x, int y, int w) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_pos(label, x, y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_14, 0);
    lv_label_set_text(label, "");
    return label;
}

} // namespace

DtmfDialerScreen::DtmfDialerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
    hide_action_bar();
    player_ = std::thread([this]() { player_main(); });
}

DtmfDialerScreen::~DtmfDialerScreen() {
    platform::clear_screen_key_release_listener(on_key_up, this);
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        held_ = -1;
        queue_.clear();
    }
    cv_.notify_all();
    if (player_.joinable()) player_.join();
}

void DtmfDialerScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    const int body_h = view::kScreenHeight - view::kTitleBarHeight;
    lv_obj_set_size(root, view::kScreenWidth, body_h);

    constexpr int kListW = 156;
    const int right_x = kListW + 2;
    const int right_w = view::kScreenWidth - right_x;

    lv_font_t* loaded_list = assets().load_standard_font(12);
    const lv_font_t* list_font = loaded_list ? loaded_list : &lv_font_montserrat_12;
    const lv_font_t* hist_font = assets().load_standard_font(16);
    const lv_font_t* big_font = assets().load_standard_font(36);
    const lv_font_t* mid_font = assets().load_standard_font(20);
    const lv_font_t* loaded_freq = assets().load_standard_font(11);
    const lv_font_t* freq_font = loaded_freq ? loaded_freq : list_font;

    auto* list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kListW, body_h);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < 16; ++i) {
        auto* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 22);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, tok.surface, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        auto on_row = [](lv_event_t* e) {
            auto* self = static_cast<DtmfDialerScreen*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            if (!self || !target) return;
            const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
            if (lv_event_get_code(e) == LV_EVENT_PRESSED) self->begin_hold(index);
            else self->release_index(index);
        };
        lv_obj_add_event_cb(row, on_row, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_PRESS_LOST, this);

        char text[24];
        const char* alias = "";
        if (kPad[i].label == '*') alias = " U";
        else if (kPad[i].label == '#') alias = " W";
        std::snprintf(text, sizeof(text), "%c%s  %.0f+%.0f", kPad[i].label, alias, static_cast<double>(kPad[i].lo),
                      static_cast<double>(kPad[i].hi));
        auto* label = lv_label_create(row);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, list_font, 0);
        lv_obj_set_style_text_color(label, tok.ink, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 4, 0);

        rows_[static_cast<size_t>(i)] = row;
        row_labels_[static_cast<size_t>(i)] = label;
    }

    auto* rule = lv_obj_create(root);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 1, body_h);
    lv_obj_set_pos(rule, kListW, 0);
    lv_obj_set_style_bg_color(rule, tok.rule, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    const int slot_w = right_w / kHist;
    for (int i = 0; i < kHist; ++i) {
        hist_[static_cast<size_t>(i)] =
            centered_label(root, hist_font ? hist_font : list_font, right_x + i * slot_w, 4, slot_w);
    }

    current_ch_ = centered_label(root, big_font ? big_font : list_font, right_x, 36, right_w);
    current_hz_ = centered_label(root, freq_font, right_x, 78, right_w);
    previous_ch_ = centered_label(root, mid_font ? mid_font : list_font, right_x, 102, right_w);
    previous_hz_ = centered_label(root, freq_font, right_x, 126, right_w);

    refresh();
    timer_ = lv_timer_create(tick_cb, 100, this);
}

void DtmfDialerScreen::on_shown() {
    // Bare C is an arrow on menus. Text entry lets that key arrive as C.
    platform::set_text_entry(true);
    platform::set_screen_key_release_listener(on_key_up, this);
}

void DtmfDialerScreen::on_key_up(uint32_t key, const char*, void* user) {
    auto* self = static_cast<DtmfDialerScreen*>(user);
    if (!self) return;
    const int index = index_for_key(key);
    if (index >= 0) self->release_index(index);
}

void DtmfDialerScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<DtmfDialerScreen*>(lv_timer_get_user_data(timer));
    if (!self || !self->current_hz_) return;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(self->mu_);
        if (self->error_.empty()) return;
        err.swap(self->error_);
    }
    lv_label_set_text(self->current_hz_, err.c_str());
    lv_obj_set_style_text_color(self->current_hz_, self->tokens().ink, 0);
}

const char* DtmfDialerScreen::typing_hint() const {
    // Non-null so the screen is treated as text entry (C stays C). The bar is hidden.
    return "";
}

void DtmfDialerScreen::refresh() {
    const auto tok = tokens();
    for (int i = 0; i < 16; ++i) {
        const bool on = i == current_;
        auto* row = rows_[static_cast<size_t>(i)];
        auto* label = row_labels_[static_cast<size_t>(i)];
        if (row) lv_obj_set_style_bg_color(row, on ? tok.accent : tok.surface, 0);
        if (label) lv_obj_set_style_text_color(label, on ? tok.bg : tok.ink, 0);
    }

    for (int i = 0; i < kHist; ++i) {
        auto* label = hist_[static_cast<size_t>(i)];
        if (!label) continue;
        if (i >= hist_count_) {
            lv_label_set_text(label, "");
            continue;
        }
        const int index = history_[static_cast<size_t>(i)];
        char text[2] = {kPad[index].label, '\0'};
        lv_label_set_text(label, text);
        const bool newest = i == hist_count_ - 1;
        lv_obj_set_style_text_color(label, newest ? tok.accent : tok.main, 0);
    }

    auto show = [&](lv_obj_t* glyph, lv_obj_t* freq, int index, lv_color_t color) {
        if (!glyph || !freq) return;
        if (index < 0) {
            lv_label_set_text(glyph, "");
            lv_label_set_text(freq, "");
            return;
        }
        char text[2] = {kPad[index].label, '\0'};
        char hz[32];
        format_freq(kPad[index], hz, sizeof(hz));
        lv_label_set_text(glyph, text);
        lv_label_set_text(freq, hz);
        lv_obj_set_style_text_color(glyph, color, 0);
        lv_obj_set_style_text_color(freq, color, 0);
    };
    show(current_ch_, current_hz_, current_, tok.accent);
    show(previous_ch_, previous_hz_, previous_, tok.main);
}

void DtmfDialerScreen::begin_hold(int index) {
    if (index < 0 || index >= 16) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (held_ == index) return;
        held_ = index;
        if ((queue_.empty() || queue_.back() != index) && queue_.size() < 24) queue_.push_back(index);
    }
    cv_.notify_all();
    remember(index);
}

void DtmfDialerScreen::release_index(int index) {
    if (index < 0) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (held_ != index) return;
        held_ = -1;
    }
    cv_.notify_all();
}

void DtmfDialerScreen::remember(int index) {
    if (current_ >= 0) previous_ = current_;
    current_ = index;
    if (hist_count_ < kHist) {
        history_[static_cast<size_t>(hist_count_++)] = index;
    } else {
        for (int i = 0; i < kHist - 1; ++i) history_[static_cast<size_t>(i)] = history_[static_cast<size_t>(i + 1)];
        history_[kHist - 1] = index;
    }
    refresh();
    if (rows_[static_cast<size_t>(index)]) lv_obj_scroll_to_view(rows_[static_cast<size_t>(index)], LV_ANIM_OFF);
}

void DtmfDialerScreen::note_error(std::string message) {
    std::lock_guard<std::mutex> lock(mu_);
    if (error_.empty()) error_ = std::move(message);
}

void DtmfDialerScreen::player_main() {
    while (true) {
        int index = -1;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&]() { return stop_ || !queue_.empty(); });
            if (stop_) return;
            index = queue_.front();
            queue_.pop_front();
        }
        play_held(index);
    }
}

void DtmfDialerScreen::play_held(int index) {
    if (index < 0 || index >= 16) return;
    const PadTone& tone = kPad[index];
    FILE* pipe = popen("aplay -q -t raw -f S16_LE -r 22050 -c 1 2>/dev/null", "w");
    if (!pipe) {
        note_error("audio playback unavailable");
        return;
    }
    setvbuf(pipe, nullptr, _IONBF, 0);

    const int min_samples = kRate * kToneMs / 1000;
    constexpr int kChunk = kRate / 25; // 40 ms
    const double step1 = 2.0 * kPi * static_cast<double>(tone.lo) / kRate;
    const double step2 = 2.0 * kPi * static_cast<double>(tone.hi) / kRate;
    double phase1 = 0.0;
    double phase2 = 0.0;
    int written = 0;
    std::vector<int16_t> samples(static_cast<size_t>(kChunk));
    bool failed = false;
    const auto started = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_) break;
            // The short beep always finishes. After that, the tone lasts only while
            // this key is still held.
            if (written >= min_samples && held_ != index) break;
        }
        // Stay a little ahead of playback so a release does not leave a long tail.
        const auto played = std::chrono::milliseconds(written * 1000 / kRate);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        if (played > elapsed + std::chrono::milliseconds(80)) {
            std::this_thread::sleep_for(played - elapsed - std::chrono::milliseconds(40));
        }
        for (int i = 0; i < kChunk; ++i) {
            const float sample = 0.5f * (static_cast<float>(std::sin(phase1)) + static_cast<float>(std::sin(phase2)));
            samples[static_cast<size_t>(i)] = static_cast<int16_t>(16000.0f * sample);
            phase1 += step1;
            phase2 += step2;
            if (phase1 > 2.0 * kPi) phase1 -= 2.0 * kPi;
            if (phase2 > 2.0 * kPi) phase2 -= 2.0 * kPi;
        }
        if (fwrite(samples.data(), sizeof(int16_t), samples.size(), pipe) != samples.size()) {
            failed = true;
            break;
        }
        written += kChunk;
    }
    pclose(pipe);
    if (failed) note_error("audio playback failed");
}

void DtmfDialerScreen::backspace() {
    if (hist_count_ <= 0) return;
    --hist_count_;
    current_ = hist_count_ > 0 ? history_[static_cast<size_t>(hist_count_ - 1)] : -1;
    previous_ = hist_count_ > 1 ? history_[static_cast<size_t>(hist_count_ - 2)] : -1;
    refresh();
    if (current_ >= 0 && rows_[static_cast<size_t>(current_)]) {
        lv_obj_scroll_to_view(rows_[static_cast<size_t>(current_)], LV_ANIM_OFF);
    }
}

bool DtmfDialerScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (key == LV_KEY_DEL || key == LV_KEY_BACKSPACE) {
        backspace();
        return true;
    }
    const int index = index_for_key(key);
    if (index < 0) return false;
    begin_hold(index);
    return true;
}

} // namespace screen

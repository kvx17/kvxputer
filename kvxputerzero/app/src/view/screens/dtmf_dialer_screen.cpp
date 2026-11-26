#include "dtmf_dialer_screen.h"

#include "asset_manager.h"
#include "audio.h"
#include "theme.h"
#include "ui_const.h"

#include <chrono>
#include <cstdint>

namespace screen {
namespace {

struct PadTone {
    char label;
    char key; // match key, lower case for letters
    float lo;
    float hi;
};

// Same pairs as the listen decoder: row 697/770/852/941, column 1209/1336/1477/1633.
constexpr PadTone kPad[16] = {
    {'1', '1', 697, 1209}, {'2', '2', 697, 1336}, {'3', '3', 697, 1477}, {'A', 'a', 697, 1633},
    {'4', '4', 770, 1209}, {'5', '5', 770, 1336}, {'6', '6', 770, 1477}, {'B', 'b', 770, 1633},
    {'7', '7', 852, 1209}, {'8', '8', 852, 1336}, {'9', '9', 852, 1477}, {'C', 'c', 852, 1633},
    {'*', '*', 941, 1209}, {'0', '0', 941, 1336}, {'#', '#', 941, 1477}, {'D', 'd', 941, 1633},
};

constexpr int kToneMs = 180;
constexpr int kGapMs = 40;
constexpr int kLitMs = 180;
constexpr int kQueueMax = 32;

int index_for_key(uint32_t key) {
    char c = 0;
    if (key >= '0' && key <= '9') c = static_cast<char>(key);
    else if (key >= 'a' && key <= 'd') c = static_cast<char>(key);
    else if (key >= 'A' && key <= 'D') c = static_cast<char>(key - 'A' + 'a');
    else if (key == '*' || key == '#') c = static_cast<char>(key);
    else return -1;
    for (int i = 0; i < 16; ++i) {
        if (kPad[i].key == c) return i;
    }
    return -1;
}

} // namespace

DtmfDialerScreen::DtmfDialerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
    player_ = std::thread([this]() { player_main(); });
}

DtmfDialerScreen::~DtmfDialerScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (player_.joinable()) player_.join();
}

void DtmfDialerScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Dialer");

    lv_font_t* loaded_dial = assets().load_standard_font(14);
    lv_font_t* loaded_key = assets().load_standard_font(16);
    const lv_font_t* dial_font = loaded_dial ? loaded_dial : &lv_font_montserrat_14;
    const lv_font_t* key_font = loaded_key ? loaded_key : dial_font;

    dial_ = lv_label_create(root);
    lv_obj_set_width(dial_, view::kScreenWidth - 8);
    lv_label_set_long_mode(dial_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(dial_, dial_font, 0);
    lv_obj_set_style_text_align(dial_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dial_, LV_ALIGN_TOP_MID, 0, 20);

    constexpr int kTop = 38;
    constexpr int kMargin = 4;
    constexpr int kGap = 3;
    const int bottom = view::kContentHeight - 2;
    const int col_w = (view::kScreenWidth - kMargin * 2 - kGap * 3) / 4;
    const int row_h = (bottom - kTop - kGap * 3) / 4;

    for (int i = 0; i < 16; ++i) {
        const int col = i % 4;
        const int row = i / 4;
        auto* box = lv_obj_create(root);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, col_w, row_h);
        lv_obj_set_pos(box, kMargin + col * (col_w + kGap), kTop + row * (row_h + kGap));
        lv_obj_set_style_radius(box, 3, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(box, tok.surface, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, tok.rule, 0);
        lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(box, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(
            box,
            [](lv_event_t* e) {
                auto* self = static_cast<DtmfDialerScreen*>(lv_event_get_user_data(e));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                if (!self || !target) return;
                const int index =
                    static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
                self->press_index(index);
            },
            LV_EVENT_CLICKED,
            this);

        char text[2] = {kPad[i].label, '\0'};
        auto* label = lv_label_create(box);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, key_font, 0);
        lv_obj_set_style_text_color(label, tok.ink, 0);
        lv_obj_center(label);

        boxes_[static_cast<size_t>(i)] = box;
        labels_[static_cast<size_t>(i)] = label;
    }

    refresh_dial();
    timer_ = lv_timer_create(tick_cb, 40, this);
}

void DtmfDialerScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<DtmfDialerScreen*>(lv_timer_get_user_data(timer));
    if (!self) return;
    if (self->lit_ >= 0 && lv_tick_elaps(self->lit_at_) > kLitMs) self->set_lit(-1);
    self->show_pending_error();
}

void DtmfDialerScreen::press_index(int index) {
    if (index < 0 || index >= 16) return;
    dialed_.push_back(kPad[index].label);
    if (dialed_.size() > 48) dialed_.erase(0, dialed_.size() - 48);
    refresh_dial();
    set_lit(index);
    enqueue(index);
}

void DtmfDialerScreen::backspace() {
    if (dialed_.empty()) return;
    dialed_.pop_back();
    refresh_dial();
}

void DtmfDialerScreen::refresh_dial() {
    if (!dial_) return;
    const auto tok = tokens();
    if (dialed_.empty()) {
        lv_label_set_text(dial_, "press 1 or A");
        lv_obj_set_style_text_color(dial_, tok.ink_dim, 0);
        return;
    }
    std::string show = dialed_;
    if (show.size() > 24) show = show.substr(show.size() - 24);
    lv_label_set_text(dial_, show.c_str());
    lv_obj_set_style_text_color(dial_, tok.ink, 0);
}

void DtmfDialerScreen::set_lit(int index) {
    auto paint = [&](int i, bool on) {
        if (i < 0 || i >= 16) return;
        const auto tok = tokens();
        auto* box = boxes_[static_cast<size_t>(i)];
        auto* label = labels_[static_cast<size_t>(i)];
        if (box) lv_obj_set_style_bg_color(box, on ? tok.accent : tok.surface, 0);
        if (label) lv_obj_set_style_text_color(label, on ? tok.bg : tok.ink, 0);
    };
    if (lit_ != index) {
        paint(lit_, false);
        paint(index, true);
        lit_ = index;
    }
    lit_at_ = lv_tick_get();
}

void DtmfDialerScreen::enqueue(int index) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (static_cast<int>(queue_.size()) >= kQueueMax) return;
        queue_.push_back(index);
    }
    cv_.notify_one();
}

void DtmfDialerScreen::show_pending_error() {
    std::string err;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (error_.empty()) return;
        err.swap(error_);
    }
    if (dial_) {
        lv_label_set_text(dial_, err.c_str());
        lv_obj_set_style_text_color(dial_, tokens().ink, 0);
    }
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
        if (index < 0 || index >= 16) continue;
        std::string err;
        const PadTone& tone = kPad[index];
        if (!platform::play_dual_tone(tone.lo, tone.hi, kToneMs, err)) {
            std::lock_guard<std::mutex> lock(mu_);
            if (error_.empty()) error_ = err.empty() ? "audio failed" : err;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kGapMs));
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
    press_index(index);
    return true;
}

const char* DtmfDialerScreen::typing_hint() const {
    return "1-9 A-D * # play   Del erases";
}

std::array<view::widgets::ActionSlot, 5> DtmfDialerScreen::nav_actions() {
    return {};
}

std::array<view::widgets::HintIcon, 2> DtmfDialerScreen::hint_icons() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

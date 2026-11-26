#include "counter_screen.h"

#include "asset_manager.h"
#include "instrument.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"

#include <cstdlib>
#include <fstream>

namespace screen {

CounterScreen::CounterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    load();
    init();
}

CounterScreen::~CounterScreen() {
    save();
}

void CounterScreen::load() {
    std::ifstream in(tools::config_file("counter.txt"));
    if (in) in >> count_;
}

void CounterScreen::save() {
    std::ofstream out(tools::config_file("counter.txt"), std::ios::trunc);
    if (out) out << count_ << "\n";
}

void CounterScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Counter");

    auto* font = assets().load_standard_font(12);
    auto* big_font = assets().load_standard_font(48);

    chip_ = view::widgets::make_status_chip(root, "READY", view::StatusPillKind::Idle, font, tok);

    big_ = view::widgets::make_readout(root, big_font, tok);
    lv_obj_align(big_, LV_ALIGN_CENTER, 0, -10);

    caption_ = view::widgets::make_caption(root, font, tok);
    lv_obj_align(caption_, LV_ALIGN_CENTER, 0, 34);

    hint_ = view::widgets::make_hint_bar(root, "Enter +1   Up +10   Down -10   Del reset", font, tok);

    refresh();
}

void CounterScreen::refresh() {
    if (big_) lv_label_set_text_fmt(big_, "%ld", count_);
    if (caption_) {
        if (last_delta_ == 0) lv_label_set_text(caption_, "ready");
        else lv_label_set_text_fmt(caption_, "last %+d", last_delta_);
    }
}

void CounterScreen::add(int delta) {
    count_ += delta;
    last_delta_ = delta;
    refresh();
    const auto tok = tokens();
    if (chip_) view::set_status_pill(chip_, "SAVED", view::StatusPillKind::Ok, tok);
    save();
}

void CounterScreen::reset() {
    count_ = 0;
    last_delta_ = 0;
    refresh();
    const auto tok = tokens();
    if (chip_) view::set_status_pill(chip_, "RESET", view::StatusPillKind::Idle, tok);
    save();
}

bool CounterScreen::handle_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ENTER:
        case ' ':
        case LV_KEY_RIGHT:
        case '+':
            add(1);
            return true;
        case LV_KEY_LEFT:
        case '-':
            add(-1);
            return true;
        case LV_KEY_UP:
        case ']':
            add(10);
            return true;
        case LV_KEY_DOWN:
        case '[':
            add(-10);
            return true;
        case LV_KEY_DEL:
        case 'r':
        case 'R':
            reset();
            return true;
        case LV_KEY_ESC:
            return KvxScreen::handle_key(key);
        default:
            break;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> CounterScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_MINUS, [this]() { add(-1); }},
        {view::ICON_CHECK, [this]() { add(1); }},
        {view::ICON_PLUS, [this]() { add(10); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

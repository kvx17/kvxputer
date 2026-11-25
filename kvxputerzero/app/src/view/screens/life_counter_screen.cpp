#include "life_counter_screen.h"

#include "asset_manager.h"
#include "eng_calc_help.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <string>

namespace screen {
namespace {

int clamp_score(int value, int lo, int hi) {
    return std::clamp(value, lo, hi);
}

std::string truco_chant(int score, int target) {
    if (score >= target) return "GANO";
    if (target <= 15) return std::to_string(score);
    if (score <= 15) return std::to_string(score) + " malas";
    return std::to_string(score - 15) + " buenas";
}

} // namespace

LifeCounterScreen::LifeCounterScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     LifeGame game)
    : KvxScreen(view_model, assets), game_(game) {
    if (game_ != LifeGame::Yugioh && game_ != LifeGame::Magic && game_ != LifeGame::Truco) {
        game_ = LifeGame::Yugioh;
    }
    reset_scores();
    init();
}

const char* LifeCounterScreen::title() const {
    switch (game_) {
        case LifeGame::Magic: return "Magic";
        case LifeGame::Truco: return "Truco";
        case LifeGame::Yugioh: break;
    }
    return "Yu-Gi-Oh";
}

const char* LifeCounterScreen::side_name(int index) const {
    if (game_ == LifeGame::Truco) return index == 0 ? "Nos" : "Ellos";
    return index == 0 ? "You" : "Opp";
}

void LifeCounterScreen::reset_scores() {
    poison_[0] = poison_[1] = 0;
    poison_mode_ = false;
    side_ = 0;
    if (game_ == LifeGame::Yugioh) {
        origin_ = 8000;
        step_ = 1000;
        scores_[0] = scores_[1] = 8000;
    } else if (game_ == LifeGame::Magic) {
        origin_ = origin_ == 40 ? 40 : 20;
        step_ = 1;
        scores_[0] = scores_[1] = origin_;
    } else {
        target_ = target_ == 15 ? 15 : 30;
        step_ = 1;
        scores_[0] = scores_[1] = 0;
    }
}

void LifeCounterScreen::set_step(int step) {
    if (step < 1) step = 1;
    step_ = step;
    poison_mode_ = false;
    refresh();
}

void LifeCounterScreen::add_to_side(int delta) {
    if (side_ < 0 || side_ > 1) side_ = 0;
    if (game_ == LifeGame::Magic && poison_mode_) {
        poison_[side_] = clamp_score(poison_[side_] + delta, 0, 10);
    } else if (game_ == LifeGame::Truco) {
        scores_[side_] = clamp_score(scores_[side_] + delta, 0, target_);
    } else if (game_ == LifeGame::Yugioh) {
        scores_[side_] = clamp_score(scores_[side_] + delta, 0, 99999);
    } else {
        scores_[side_] = std::max(0, scores_[side_] + delta);
    }
    refresh();
}

void LifeCounterScreen::build() {
    make_header(content(), title());
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;
    const lv_font_t* big = assets().load_standard_font(28);
    if (!big) big = &lv_font_montserrat_28;
    const lv_font_t* small = assets().load_standard_font(11);
    if (!small) small = font;

    for (int i = 0; i < 2; ++i) {
        const int x = i == 0 ? 8 : 164;
        name_[i] = lv_label_create(content());
        lv_label_set_text(name_[i], side_name(i));
        lv_obj_set_pos(name_[i], x, 24);
        lv_obj_set_style_text_font(name_[i], font, 0);

        score_[i] = lv_label_create(content());
        lv_label_set_text(score_[i], "0");
        lv_obj_set_pos(score_[i], x, 42);
        lv_obj_set_style_text_font(score_[i], big, 0);

        sub_[i] = lv_label_create(content());
        lv_label_set_text(sub_[i], "");
        lv_obj_set_pos(sub_[i], x, 78);
        lv_obj_set_width(sub_[i], 148);
        lv_label_set_long_mode(sub_[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(sub_[i], small, 0);
        lv_obj_set_style_text_color(sub_[i], tok.ink_dim, 0);
    }

    status_ = lv_label_create(content());
    lv_obj_set_pos(status_, 8, 100);
    lv_obj_set_width(status_, view::kScreenWidth - 16);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(status_, small, 0);
    lv_obj_set_style_text_color(status_, tok.ink_dim, 0);
    help_.build(root(), assets(), tok);
    refresh();
}

void LifeCounterScreen::refresh() {
    const auto tok = tokens();
    for (int i = 0; i < 2; ++i) {
        const bool selected = i == side_;
        const lv_color_t ink = selected ? tok.accent : tok.ink;
        if (name_[i]) {
            lv_label_set_text(name_[i], side_name(i));
            lv_obj_set_style_text_color(name_[i], ink, 0);
        }
        if (score_[i]) {
            lv_label_set_text(score_[i], std::to_string(scores_[i]).c_str());
            const bool winner = game_ == LifeGame::Truco && scores_[i] >= target_ && scores_[i] > 0;
            lv_obj_set_style_text_color(score_[i], winner ? tok.main : ink, 0);
        }
        if (!sub_[i]) continue;
        std::string sub;
        if (game_ == LifeGame::Truco) {
            sub = truco_chant(scores_[i], target_);
        } else if (game_ == LifeGame::Magic) {
            sub = "poison " + std::to_string(poison_[i]);
        } else if (scores_[i] == 0) {
            sub = "out";
        }
        lv_label_set_text(sub_[i], sub.c_str());
        lv_obj_set_style_text_color(sub_[i], selected ? tok.accent : tok.ink_dim, 0);
    }
    if (!status_) return;
    std::string line;
    if (game_ == LifeGame::Yugioh) {
        line = "step " + std::to_string(step_) + "    1=100  2=500  3=1000  H half  R reset";
    } else if (game_ == LifeGame::Magic) {
        line = poison_mode_ ? "POISON    F/X +/-    P life"
                            : "step " + std::to_string(step_) + "   start " + std::to_string(origin_) +
                                  "    1/5 step  S 20/40  P poison";
    } else {
        const int left = std::max(0, target_ - std::max(scores_[0], scores_[1]));
        line = "a " + std::to_string(target_) + "    faltan " + std::to_string(left) +
               "    1/2/3 suma  X -1  T 15/30";
    }
    lv_label_set_text(status_, line.c_str());
}

const char* LifeCounterScreen::typing_hint() const {
    return "Tab help | Enter run | F/X fields | Esc";
}

std::array<view::widgets::ActionSlot, 5> LifeCounterScreen::nav_actions() {
    return {};
}

std::array<view::widgets::HintIcon, 2> LifeCounterScreen::hint_icons() {
    return {{
        {view::ICON_INFO, [this]() { open_help(); }},
        {},
    }};
}

void LifeCounterScreen::open_help() {
    if (help_.visible()) {
        help_.hide();
        return;
    }
    help_.show(title(), tools::engcalc::life_help_text(static_cast<int>(game_)));
}

bool LifeCounterScreen::handle_key(uint32_t key) {
    if (help_.visible()) return help_.handle_key(key);
    if (key == platform::kKeyTab) {
        open_help();
        return true;
    }
    if (key == LV_KEY_ESC) {
        help_.hide();
        view_model().go_back();
        return true;
    }

    char letter = 0;
    if (key >= 'A' && key <= 'Z') letter = static_cast<char>(key - 'A' + 'a');
    else if (key >= 'a' && key <= 'z') letter = static_cast<char>(key);

    if (key == LV_KEY_LEFT || letter == 'z') {
        side_ = 0;
        refresh();
        return true;
    }
    if (key == LV_KEY_RIGHT || letter == 'c') {
        side_ = 1;
        refresh();
        return true;
    }

    const bool add = key == LV_KEY_UP || letter == 'f' || key == LV_KEY_ENTER || key == '+';
    const bool sub = key == LV_KEY_DOWN || letter == 'x' || key == LV_KEY_BACKSPACE || key == '-';

    if (game_ == LifeGame::Truco) {
        if (key == '1') {
            add_to_side(1);
            return true;
        }
        if (key == '2') {
            add_to_side(2);
            return true;
        }
        if (key == '3') {
            add_to_side(3);
            return true;
        }
        if (add) {
            add_to_side(1);
            return true;
        }
        if (sub) {
            add_to_side(-1);
            return true;
        }
        if (letter == 't') {
            target_ = target_ == 30 ? 15 : 30;
            scores_[0] = std::min(scores_[0], target_);
            scores_[1] = std::min(scores_[1], target_);
            refresh();
            return true;
        }
        if (letter == 'r') {
            scores_[0] = scores_[1] = 0;
            refresh();
            return true;
        }
        return true;
    }

    if (add) {
        add_to_side(poison_mode_ ? 1 : step_);
        return true;
    }
    if (sub) {
        add_to_side(poison_mode_ ? -1 : -step_);
        return true;
    }
    if (letter == 'r') {
        reset_scores();
        refresh();
        return true;
    }

    if (game_ == LifeGame::Yugioh) {
        if (key == '1') {
            set_step(100);
            return true;
        }
        if (key == '2') {
            set_step(500);
            return true;
        }
        if (key == '3') {
            set_step(1000);
            return true;
        }
        if (key == '4') {
            set_step(50);
            return true;
        }
        if (letter == 'h') {
            scores_[side_] /= 2;
            refresh();
            return true;
        }
        return true;
    }

    if (key == '1') {
        set_step(1);
        return true;
    }
    if (key == '5') {
        set_step(5);
        return true;
    }
    if (letter == 's') {
        origin_ = origin_ == 20 ? 40 : 20;
        scores_[0] = scores_[1] = origin_;
        poison_[0] = poison_[1] = 0;
        poison_mode_ = false;
        step_ = 1;
        refresh();
        return true;
    }
    if (letter == 'p') {
        poison_mode_ = !poison_mode_;
        refresh();
        return true;
    }
    return true;
}

} // namespace screen

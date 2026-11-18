#include "doc_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <sstream>

namespace screen {
namespace {

constexpr int kLineScroll = 14;
constexpr int kPageScroll = 56;

} // namespace

DocScreen::DocScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                     std::string body)
    : KvxScreen(view_model, assets), title_(std::move(title)), body_(std::move(body)) {
    init();
}

DocScreen::DocScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                     std::vector<std::string> lines)
    : KvxScreen(view_model, assets), title_(std::move(title)) {
    std::ostringstream oss;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) oss << '\n';
        oss << lines[i];
    }
    body_ = oss.str();
    init();
}

std::string DocScreen::render_markup(const std::string& raw) {
    std::ostringstream out;
    std::istringstream in(raw);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!first) out << '\n';
        first = false;
        if (line.rfind("# ", 0) == 0) {
            out << line.substr(2);
            continue;
        }
        if (line.rfind("- ", 0) == 0) {
            out << "• " << line.substr(2);
            continue;
        }
        if (line.size() >= 2 && line.front() == '`' && line.back() == '`') {
            out << "  " << line.substr(1, line.size() - 2);
            continue;
        }
        out << line;
    }
    return out.str();
}

void DocScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;

    panel_ = view::make_panel(content(), 18, tok);
    body_label_ = view::make_doc_body(panel_, font, tok);
    lv_label_set_text(body_label_, render_markup(body_).c_str());

    scroll_hint_ = lv_label_create(content());
    lv_label_set_text(scroll_hint_, "");
    lv_obj_set_style_text_font(scroll_hint_, font, 0);
    lv_obj_set_style_text_color(scroll_hint_, tok.ink_dim, 0);
    lv_obj_align(scroll_hint_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    update_scroll_hint();
}

void DocScreen::update_scroll_hint() {
    if (!panel_ || !scroll_hint_) return;
    const int32_t scroll_y = lv_obj_get_scroll_y(panel_);
    const int32_t bottom = lv_obj_get_scroll_bottom(panel_);
    if (bottom <= 2 && scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "");
        return;
    }
    if (bottom <= 2) {
        lv_label_set_text(scroll_hint_, "^");
    } else if (scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "v");
    } else {
        lv_label_set_text(scroll_hint_, "^v");
    }
}

void DocScreen::scroll_by(int px) {
    if (!panel_) return;
    const int32_t y = lv_obj_get_scroll_y(panel_);
    lv_obj_scroll_to_y(panel_, y + px, LV_ANIM_OFF);
    update_scroll_hint();
}

bool DocScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_DOWN) {
        scroll_by(kLineScroll);
        return true;
    }
    if (key == LV_KEY_UP) {
        scroll_by(-kLineScroll);
        return true;
    }
    // Bare , / . are Sym punctuation on Zero — page scroll.
    if (key == '.') {
        scroll_by(kPageScroll);
        return true;
    }
    if (key == ',') {
        scroll_by(-kPageScroll);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> DocScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { scroll_by(-kLineScroll); }},
        {"", {}},
        {view::ICON_CARET_DOWN, [this]() { scroll_by(kLineScroll); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

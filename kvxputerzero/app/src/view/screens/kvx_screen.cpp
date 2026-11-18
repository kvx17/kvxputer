#include "kvx_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"

namespace screen {

KvxScreen::KvxScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

KvxScreen::~KvxScreen() {
    platform::clear_screen_key_listener(on_global_key, this);
    action_bar_.reset();
    title_bar_.reset();
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
        root_ = nullptr;
        content_ = nullptr;
    }
}

void KvxScreen::init() {
    if (root_) return;

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root_, bg_color(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    title_bar_ = std::make_unique<view::widgets::TitleBar>(root_, view_model_, assets_);
    title_bar_->build();

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_, view::kScreenWidth, view::kContentHeight);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

    build();
    action_bar_ = std::make_unique<view::widgets::ActionBar>(root_,
                                                            assets_,
                                                            pri_color(),
                                                            sec_color(),
                                                            nav_actions(),
                                                            typing_hint());
    bind_escape_back();
}

std::array<view::widgets::ActionSlot, 5> KvxScreen::nav_actions() {
    return {};
}

const char* KvxScreen::typing_hint() const {
    return nullptr;
}

lv_obj_t* KvxScreen::root() const {
    return root_;
}

viewmodel::BaseViewModel& KvxScreen::view_model() {
    return view_model_;
}

app::AssetManager& KvxScreen::assets() {
    return assets_;
}

lv_obj_t* KvxScreen::content() const {
    return content_;
}

lv_obj_t* KvxScreen::make_header(lv_obj_t* parent, const char* title) {
    auto* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, view::kScreenWidth, 18);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, pri_color(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    auto* label = lv_label_create(bar);
    lv_label_set_text(label, title ? title : "");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    auto* font = assets_.load_standard_font(12);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);

    auto* esc = lv_label_create(bar);
    lv_label_set_text(esc, "ESC");
    lv_obj_set_style_text_color(esc, sec_color(), 0);
    lv_obj_set_style_text_font(esc, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_align(esc, LV_ALIGN_RIGHT_MID, -6, 0);
    return bar;
}

void KvxScreen::bind_escape_back() {
    platform::set_screen_key_listener(on_global_key, this);
}

bool KvxScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        view_model_.go_back();
        return true;
    }
    return false;
}

bool KvxScreen::on_global_key(uint32_t key, const char* /*key_name*/, bool /*long_pressed*/, void* user_data) {
    auto* self = static_cast<KvxScreen*>(user_data);
    if (!self) return false;
    return self->handle_key(key);
}

lv_color_t KvxScreen::pri_color() const {
    return lv_color_hex(view_model_.pri_color());
}

lv_color_t KvxScreen::sec_color() const {
    return lv_color_hex(view_model_.sec_color());
}

lv_color_t KvxScreen::bg_color() const {
    return view_model_.is_dark_mode() ? lv_color_hex(0x000000) : lv_color_hex(0x101018);
}

} // namespace screen

#pragma once

#include "action_bar.h"
#include "base_viewmodel.h"
#include "kvx_style.h"
#include "lvgl.h"
#include "titlebar.h"

#include <array>
#include <memory>
#include <string>

namespace app {
class AssetManager;
}

namespace screen {

// Root with Cardputer Zero status TitleBar + content + compact ActionBar.
class KvxScreen {
public:
    KvxScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    virtual ~KvxScreen();

    KvxScreen(const KvxScreen&) = delete;
    KvxScreen& operator=(const KvxScreen&) = delete;

    void init();
    lv_obj_t* root() const;

protected:
    virtual void build() = 0;

    viewmodel::BaseViewModel& view_model();
    app::AssetManager& assets();
    lv_obj_t* content() const;

    lv_obj_t* make_header(lv_obj_t* parent, const char* title);
    void bind_escape_back();

    // Keys 4-8. Empty slots stay visible but do nothing.
    virtual std::array<view::widgets::ActionSlot, 5> nav_actions();
    // Shown instead of the 4-8 icons when every slot is empty (text entry).
    virtual const char* typing_hint() const;

    // Return true if handled. Esc is always handled by base unless overridden returns and calls go_back.
    virtual bool handle_key(uint32_t key);

    lv_color_t pri_color() const;
    lv_color_t sec_color() const;
    lv_color_t bg_color() const;
    view::KvxTokens tokens() const;

private:
    static bool on_global_key(uint32_t key, const char* key_name, bool long_pressed, void* user_data);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::ActionBar> action_bar_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* content_{nullptr};
};

} // namespace screen

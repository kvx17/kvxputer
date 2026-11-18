#pragma once

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "kvx_screen.h"
#include "lvgl.h"

#include <memory>

namespace app {

class ScreenManager {
public:
    ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    void start();
    void request_page(model::AppPage page);
    void flush_requested_page();
    lv_obj_t* current_screen() const;

private:
    void load_screen(std::unique_ptr<screen::KvxScreen> next_screen);
    std::unique_ptr<screen::KvxScreen> make_screen(model::AppPage page);

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    std::unique_ptr<screen::KvxScreen> current_screen_;
    lv_observer_t* page_observer_ = nullptr;
    model::AppPage requested_page_ = model::AppPage::ChannelGrid;
    model::AppPage loaded_page_ = model::AppPage::ChannelGrid;
    bool has_loaded_page_ = false;
    bool page_switch_scheduled_ = false;
    // When a bounce (A→B→A) coalesces into one async flush, still rebuild A.
    bool force_reload_ = false;
};

} // namespace app

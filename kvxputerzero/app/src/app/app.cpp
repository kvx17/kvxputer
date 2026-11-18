/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "boot_splash.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "pda_lib.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "toast.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "kvxputerzero.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "kvxputerzero" / "kvxputerzero.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "kvxputerzero" / "kvxputerzero.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class AlarmWatcher {
public:
    explicit AlarmWatcher(AssetManager& assets) : assets_(assets) {
        timer_ = lv_timer_create(on_tick, 15000, this);
        poll();
    }

    ~AlarmWatcher() {
        dismiss();
        if (timer_) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
    }

    bool consuming_keys() const {
        return modal_ != nullptr;
    }

    bool handle_key(uint32_t /*key*/) {
        if (!modal_) return false;
        dismiss_and_mark();
        return true;
    }

private:
    static void on_tick(lv_timer_t* timer) {
        auto* self = static_cast<AlarmWatcher*>(lv_timer_get_user_data(timer));
        if (self) self->poll();
    }

    void poll() {
        if (modal_) return;
        tools::pda::Alarm due;
        size_t index = 0;
        std::string error;
        if (!tools::pda::find_due_alarm(due, index, error)) return;
        pending_index_ = index;
        tools::pda::play_alarm_beep();
        show_modal(due);
    }

    void show_modal(const tools::pda::Alarm& due) {
        modal_ = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(modal_);
        lv_obj_set_size(modal_, view::kScreenWidth, view::kScreenHeight);
        lv_obj_set_style_bg_color(modal_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(modal_, LV_OPA_80, 0);

        auto* box = lv_obj_create(modal_);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, view::kScreenWidth - 24, 70);
        lv_obj_center(box);
        lv_obj_set_style_radius(box, 8, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x9800C0), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

        auto* font = assets_.load_standard_font(14);
        auto* title = lv_label_create(box);
        lv_label_set_text(title, "ALARM");
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(title, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

        auto* body = lv_label_create(box);
        lv_label_set_text(body, tools::pda::alarm_label(due).c_str());
        lv_obj_set_style_text_color(body, lv_color_hex(0x00FC00), 0);
        lv_obj_set_style_text_font(body, font ? font : &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(body, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(body, view::kScreenWidth - 40);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, 4);

        auto* hint = lv_label_create(box);
        lv_label_set_text(hint, "Any key dismisses");
        lv_obj_set_style_text_color(hint, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(hint, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    void dismiss_and_mark() {
        std::string error;
        tools::pda::mark_alarm_triggered(pending_index_, error);
        dismiss();
    }

    void dismiss() {
        if (modal_ && lv_obj_is_valid(modal_)) {
            lv_obj_delete(modal_);
        }
        modal_ = nullptr;
    }

    AssetManager& assets_;
    lv_timer_t* timer_{nullptr};
    lv_obj_t* modal_{nullptr};
    size_t pending_index_{0};
};

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets, AlarmWatcher& alarms)
        : view_model_(view_model),
          alarms_(alarms),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (alarms_.consuming_keys()) {
            return alarms_.handle_key(key);
        }
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                }
                else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        // Long-hold Esc on the channel home quits; short Esc is handled by screens.
        if (key != LV_KEY_ESC) {
            return false;
        }

        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
                return true;
            }
            return false;
        }

        exit_key_pressed_ = key;
        exit_armed_ = view_model_.current_page() == model::AppPage::ChannelGrid;
        if (exit_armed_) {
            toast_->show_persistent_highlighted("Hold ",
                                                "ESC",
                                                " to exit",
                                                view::widgets::ToastTone::Warning);
            return true; // suppress short Esc quit on home; hold required
        }
        return false;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    AlarmWatcher& alarms_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("kvxputerzero");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        LOG_INFO("loaded config: {} (dark_mode={})",
                 loaded_config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    // Cardputer intro GIF; any key skips (same idea as firmware boot_screen_anim).
    play_boot_splash(assets);

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
#if USE_DESKTOP && LV_USE_PERF_MONITOR
    // Keep the debug FPS readout off the 4-8 action bar.
    if (auto* sys = lv_display_get_layer_sys(display)) {
        const uint32_t child_count = lv_obj_get_child_count(sys);
        for (uint32_t i = 0; i < child_count; ++i) {
            lv_obj_align(lv_obj_get_child(sys, i), LV_ALIGN_TOP_RIGHT, 0, 0);
        }
    }
#endif
    AlarmWatcher alarm_watcher(assets);
    ShortcutController shortcuts(view_model, assets, alarm_watcher);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));

    const char* auto_shot = std::getenv("KVX_SCREENSHOT_PATH");
    int auto_shot_frames = auto_shot ? 12 : -1;

    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        if (auto_shot_frames > 0) {
            --auto_shot_frames;
            if (auto_shot_frames == 0) {
                const auto shot = platform::screenshot::capture_active_screen();
                if (shot.success) {
                    std::error_code ec;
                    std::filesystem::create_directories(
                        std::filesystem::path(auto_shot).parent_path(), ec);
                    std::filesystem::copy_file(shot.path, auto_shot,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    LOG_INFO("auto screenshot: {} -> {} ({})", shot.path, auto_shot,
                             ec ? ec.message() : "ok");
                } else {
                    LOG_WARN("auto screenshot failed: {}", shot.error);
                }
                running = false;
                continue;
            }
        }
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

} // namespace app

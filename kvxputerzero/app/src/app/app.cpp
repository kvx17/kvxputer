/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "backlight.h"
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
#include "tool_registry.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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
        lv_obj_set_style_bg_color(box, lv_color_hex(model::kPriDefault), 0);
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
        // The matrix rain exits on any key, including help and screenshot.
        if (view_model_.current_page() == model::AppPage::Matrix) {
            return false;
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

        if (!long_pressed && handle_fn_action(key)) {
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

    bool handle_fn_action(uint32_t key) {
        if (key == platform::kKeyBrightnessDown || key == platform::kKeyBrightnessUp) {
            int32_t pct = 50;
            if (!platform::read_brightness_percent(pct)) {
                pct = 50;
            }
            const int step = key == platform::kKeyBrightnessUp ? 10 : -10;
            pct = std::clamp(pct + step, int32_t{5}, int32_t{100});
            platform::write_brightness_percent(pct);
            toast_->show("Brightness " + std::to_string(pct) + "%",
                         view::widgets::ToastTone::Default,
                         1200);
            return true;
        }
        if (key == platform::kKeyVolumeDown || key == platform::kKeyVolumeUp) {
            auto& model = view_model_.model();
            const int step = key == platform::kKeyVolumeUp ? 10 : -10;
            const int vol = std::clamp(model.sound_volume() + step, 0, 100);
            model.set_sound_volume(vol);
            model.set_sound_enabled(vol > 0);
            model.save_user_config();
            toast_->show("Volume " + std::to_string(vol) + "%",
                         view::widgets::ToastTone::Default,
                         1200);
            return true;
        }
        if (key == platform::kKeyMute) {
            auto& model = view_model_.model();
            const bool on = !model.sound_enabled();
            model.set_sound_enabled(on);
            model.save_user_config();
            toast_->show(on ? "Sound on" : "Muted", view::widgets::ToastTone::Default, 1200);
            return true;
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

#if USE_DESKTOP
// Deterministic key/shot/theme automation driven by KVX_UI_SCRIPT.
class UiScriptRunner {
public:
    UiScriptRunner(viewmodel::BaseViewModel& vm, DesktopSimulatorFrame& frame, bool& running)
        : vm_(vm), frame_(frame), running_(running) {}

    bool load(const char* path) {
        std::ifstream in(path);
        if (!in) {
            LOG_ERROR("KVX_UI_SCRIPT open failed: {}", path);
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i >= line.size() || line[i] == '#') continue;
            lines_.push_back(line.substr(i));
        }
        LOG_INFO("KVX_UI_SCRIPT loaded {} cmds from {}", lines_.size(), path);
        return true;
    }

    // Call once per main-loop tick. Advances wait timers and runs ready commands.
    void tick() {
        if (done_ || index_ >= lines_.size()) {
            if (!done_ && !lines_.empty()) {
                done_ = true;
                LOG_INFO("KVX_UI_SCRIPT finished");
                running_ = false;
            }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < wait_until_) return;

        const std::string& raw = lines_[index_++];
        std::istringstream iss(raw);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) return;

        if (cmd == "wait") {
            int ms = 0;
            iss >> ms;
            if (ms < 0) ms = 0;
            wait_until_ = now + std::chrono::milliseconds(ms);
            return;
        }
        if (cmd == "key") {
            std::string name;
            iss >> name;
            if (!frame_.inject_named_key(name)) {
                LOG_WARN("ui script key failed: {}", name);
            }
            // Let LVGL process the key before the next command.
            wait_until_ = now + std::chrono::milliseconds(40);
            return;
        }
        if (cmd == "type") {
            std::string rest;
            std::getline(iss, rest);
            size_t i = 0;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
            frame_.inject_text(rest.substr(i));
            wait_until_ = now + std::chrono::milliseconds(40);
            return;
        }
        if (cmd == "shot") {
            std::string name;
            iss >> name;
            if (name.empty()) name = "shot";
            take_shot(name);
            wait_until_ = now + std::chrono::milliseconds(20);
            return;
        }
        if (cmd == "theme") {
            std::string mode;
            iss >> mode;
            const bool dark = (mode == "dark" || mode == "1" || mode == "on");
            vm_.set_dark_mode(dark);
            // Explicit palette: KvxScreens paint from bg/pri/sec, not only dark_mode.
            if (dark) {
                vm_.set_bg_color(model::kBgDefault);
                vm_.set_pri_color(model::kPriDefault);
            } else {
                vm_.set_bg_color(0xF0F0F0);
                vm_.set_pri_color(0x202020);
            }
            vm_.persist_theme();
            // Bounce so ScreenManager rebuilds tiles with the new tokens.
            vm_.show_page(model::AppPage::ConfigMenu);
            vm_.go_home();
            wait_until_ = now + std::chrono::milliseconds(150);
            return;
        }
        if (cmd == "open") {
            std::string key;
            iss >> key;
            open_tool_key(key);
            wait_until_ = now + std::chrono::milliseconds(80);
            return;
        }
        if (cmd == "page") {
            std::string name;
            iss >> name;
            open_page_name(name);
            wait_until_ = now + std::chrono::milliseconds(100);
            return;
        }
        if (cmd == "home") {
            vm_.go_home();
            wait_until_ = now + std::chrono::milliseconds(60);
            return;
        }
        if (cmd == "quit") {
            done_ = true;
            running_ = false;
            return;
        }
        LOG_WARN("ui script unknown cmd: {}", raw);
    }

    bool done() const { return done_; }

private:
    void open_page_name(const std::string& name) {
        if (name.empty()) return;
        auto lower = name;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (lower == "calculator" || lower == "calc") {
            vm_.open_calculator();
        } else if (lower == "clock") {
            vm_.show_page(model::AppPage::ClockFace);
        } else if (lower == "clock_menu" || lower == "clock_sub") {
            vm_.show_page(model::AppPage::ClockSubMenu);
        } else if (lower == "charge") {
            vm_.show_page(model::AppPage::ChargeFace);
        } else if (lower == "config") {
            vm_.open_config_menu();
        } else if (lower == "about") {
            vm_.open_about();
        } else if (lower == "brightness") {
            vm_.open_brightness();
        } else if (lower == "dim" || lower == "dim_time") {
            vm_.open_dim_time();
        } else if (lower == "sound") {
            vm_.open_sound_menu();
        } else if (lower == "power") {
            vm_.open_power_menu();
        } else if (lower == "pda" || lower == "pda_hub") {
            vm_.open_pda_hub();
        } else if (lower == "pda_notes") {
            vm_.open_pda_list(model::PdaKind::Notes);
        } else if (lower == "pda_memos") {
            vm_.open_pda_list(model::PdaKind::Memos);
        } else if (lower == "pda_todo") {
            vm_.open_pda_list(model::PdaKind::Todo);
        } else if (lower == "pda_contacts") {
            vm_.open_pda_list(model::PdaKind::Contacts);
        } else if (lower == "pda_alarms") {
            vm_.open_pda_list(model::PdaKind::Alarms);
        } else if (lower == "pda_calendar") {
            vm_.open_pda_calendar();
        } else if (lower == "pda_world" || lower == "pda_world_clock") {
            vm_.open_pda_world_clock();
        } else if (lower == "timer") {
            vm_.open_timer();
        } else if (lower == "passgen") {
            vm_.open_passgen();
        } else if (lower == "totp" || lower == "totp_menu") {
            vm_.open_totp_menu();
        } else if (lower == "tools" || lower == "others") {
            // ToolsFill channel menu (Calculator, PDA, Media, …).
            for (size_t i = 0; i < model::kChannelCount; ++i) {
                if (std::strcmp(model::kChannels[i].id, "others") == 0) {
                    vm_.set_channel_index(static_cast<int>(i));
                    vm_.show_page(model::AppPage::ChannelMenu);
                    break;
                }
            }
        } else {
            LOG_WARN("ui script page: unknown '{}'", name);
            return;
        }
        LOG_INFO("ui script page: {}", name);
    }

    void take_shot(const std::string& name) {
        const auto shot = platform::screenshot::capture_active_screen();
        if (!shot.success) {
            LOG_WARN("ui script shot failed: {}", shot.error);
            return;
        }
        const char* out_root = std::getenv("KVX_AUDIT_DIR");
        const std::filesystem::path dir =
            out_root && out_root[0] ? std::filesystem::path(out_root)
                                   : std::filesystem::path("screenshot/audit");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string file = name;
        if (file.size() < 4 || file.substr(file.size() - 4) != ".png") file += ".png";
        const auto dest = dir / file;
        std::filesystem::copy_file(shot.path, dest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_WARN("ui script shot copy {} -> {}: {}", shot.path, dest.string(), ec.message());
        } else {
            LOG_INFO("ui script shot: {}", dest.string());
        }
    }

    void open_tool_key(const std::string& key_stage) {
        if (key_stage.empty()) return;
        std::string key = key_stage;
        std::string stage;
        const auto colon = key_stage.find(':');
        if (colon != std::string::npos) {
            key = key_stage.substr(0, colon);
            stage = key_stage.substr(colon + 1);
        }
        const auto* desc = model::find_tool_by_key(key.c_str());
        if (!desc) {
            LOG_WARN("ui script open: unknown tool '{}'", key);
            return;
        }
        vm_.open_tool(desc->id, stage);
        LOG_INFO("ui script open: {} ({})", key, stage.empty() ? "-" : stage.c_str());
    }

    viewmodel::BaseViewModel& vm_;
    DesktopSimulatorFrame& frame_;
    bool& running_;
    std::vector<std::string> lines_;
    size_t index_{0};
    bool done_{false};
    std::chrono::steady_clock::time_point wait_until_{};
};
#endif

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

    // Theme colors are already loaded by BaseModel. Do not call set_dark_mode()
    // here: ApplicationConfig defaults dark_mode to false, and set_dark_mode(false)
    // rewrites a black background to light gray, then saves over the user's colors.
    LOG_INFO("theme: pri={:#08x} sec={:#08x} bg={:#08x} dark_mode={}",
             view_model.pri_color(),
             view_model.sec_color(),
             view_model.bg_color(),
             view_model.is_dark_mode() ? "yes" : "no");
    if (!std::filesystem::is_regular_file(loaded_config_path)) {
        LOG_WARN("no config at {}; using defaults", loaded_config_path);
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

    // Matrix rain + KVXPUTER wordmark. Any key skips.
#if USE_DESKTOP
    set_boot_event_pump([](void* user_data) {
                            return static_cast<DesktopSimulatorFrame*>(user_data)->process_events();
                        },
                        &simulator_frame);
#endif
    play_boot_splash(assets);
#if USE_DESKTOP
    set_boot_event_pump(nullptr, nullptr);
#endif

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

#if USE_DESKTOP
    // Direct tool open for deep-stage automation (navigation preferred for audits).
    if (const char* open_tool = std::getenv("KVX_OPEN_TOOL")) {
        std::string key = open_tool;
        std::string stage;
        const auto colon = key.find(':');
        if (colon != std::string::npos) {
            stage = key.substr(colon + 1);
            key = key.substr(0, colon);
        }
        if (const auto* desc = model::find_tool_by_key(key.c_str())) {
            view_model.open_tool(desc->id, stage);
            LOG_INFO("KVX_OPEN_TOOL: {} ({})", key, stage.empty() ? "-" : stage.c_str());
        } else {
            LOG_WARN("KVX_OPEN_TOOL: unknown tool '{}'", key);
        }
    }

    std::unique_ptr<UiScriptRunner> ui_script;
    if (const char* script_path = std::getenv("KVX_UI_SCRIPT")) {
        ui_script = std::make_unique<UiScriptRunner>(view_model, simulator_frame, running);
        if (!ui_script->load(script_path)) {
            ui_script.reset();
            running = false;
        }
    }
#endif

    const char* auto_shot = std::getenv("KVX_SCREENSHOT_PATH");
    int auto_shot_frames = auto_shot ? 12 : -1;

    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
#if USE_DESKTOP
        if (ui_script) ui_script->tick();
#endif
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

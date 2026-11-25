/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "audio.h"
#include "asset_manager.h"
#include "backlight.h"
#include "boot_splash.h"
#include "help_popup.h"
#include "logger.h"
#include "launch_catalog.h"
#include "linux_input.h"
#include "pda_lib.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "kvx_style.h"
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

class HoldMeter {
public:
    HoldMeter(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model), assets_(assets) {}

    ~HoldMeter() { hide(); }

    void show(const std::string& label) {
        ensure();
        if (!root_ || !label_ || !fill_) return;
        lv_label_set_text(label_, label.c_str());
        apply_theme();
        set_ratio(0.f);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }

    void set_ratio(float ratio) {
        if (!fill_) return;
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > 1.f) ratio = 1.f;
        lv_obj_set_width(fill_, static_cast<int>(kTrackW * ratio));
    }

    void hide() {
        if (root_ && lv_obj_is_valid(root_)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }

private:
    static constexpr int kTrackW = 200;

    void ensure() {
        if (root_ && lv_obj_is_valid(root_)) return;
        root_ = nullptr;
        auto* parent = lv_layer_top();
        if (!parent) return;
        const auto tok = view::make_tokens(view_model_.pri_color(), view_model_.sec_color(), view_model_.bg_color());
        root_ = lv_obj_create(parent);
        lv_obj_remove_style_all(root_);
        lv_obj_set_size(root_, 240, 44);
        lv_obj_align(root_, LV_ALIGN_CENTER, 0, 8);
        lv_obj_set_style_radius(root_, 8, 0);
        lv_obj_set_style_bg_color(root_, tok.surface, 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(root_, 1, 0);
        lv_obj_set_style_border_color(root_, tok.main, 0);
        lv_obj_set_style_border_opa(root_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_CLICKABLE);

        label_ = lv_label_create(root_);
        auto* font = assets_.load_standard_font(12);
        lv_obj_set_style_text_font(label_, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label_, tok.ink, 0);
        lv_obj_align(label_, LV_ALIGN_TOP_MID, 0, 4);

        auto* track = lv_obj_create(root_);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, kTrackW, 8);
        lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_radius(track, 3, 0);
        lv_obj_set_style_bg_color(track, tok.rule, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

        fill_ = lv_obj_create(track);
        lv_obj_remove_style_all(fill_);
        lv_obj_set_size(fill_, 0, 8);
        lv_obj_align(fill_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(fill_, 3, 0);
        lv_obj_set_style_bg_color(fill_, tok.accent, 0);
        lv_obj_set_style_bg_opa(fill_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(fill_, LV_OBJ_FLAG_SCROLLABLE);
    }

    void apply_theme() {
        if (!root_) return;
        const auto tok = view::make_tokens(view_model_.pri_color(), view_model_.sec_color(), view_model_.bg_color());
        lv_obj_set_style_bg_color(root_, tok.surface, 0);
        lv_obj_set_style_border_color(root_, tok.main, 0);
        if (label_) lv_obj_set_style_text_color(label_, tok.ink, 0);
        if (fill_) lv_obj_set_style_bg_color(fill_, tok.accent, 0);
    }

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* label_{nullptr};
    lv_obj_t* fill_{nullptr};
};

// Full-screen blackout plus the "Hold Enter to unlock" panel. The progress bar
// lives on that panel and is hidden until Enter has been held for a second.
class PocketLockUi {
public:
    PocketLockUi(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model), assets_(assets) {}

    void blank() {
        ensure();
        if (!cover_) return;
        lv_obj_remove_flag(cover_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cover_);
        if (prompt_) lv_obj_add_flag(prompt_, LV_OBJ_FLAG_HIDDEN);
        set_bar_visible(false);
    }

    void show_prompt() {
        ensure();
        if (cover_) lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
        if (!prompt_) return;
        lv_obj_remove_flag(prompt_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(prompt_);
        set_bar_visible(false);
    }

    void hide() {
        if (cover_ && lv_obj_is_valid(cover_)) lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);
        if (prompt_ && lv_obj_is_valid(prompt_)) lv_obj_add_flag(prompt_, LV_OBJ_FLAG_HIDDEN);
        set_bar_visible(false);
    }

    void set_bar_visible(bool visible) {
        if (!track_ || !lv_obj_is_valid(track_)) return;
        if (visible) lv_obj_remove_flag(track_, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);
            set_ratio(0.f);
        }
    }

    void set_ratio(float ratio) {
        if (!fill_ || !lv_obj_is_valid(fill_)) return;
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > 1.f) ratio = 1.f;
        lv_obj_set_width(fill_, static_cast<int>(kTrackW * ratio));
    }

private:
    static constexpr int kTrackW = 200;

    void ensure() {
        if (cover_ && lv_obj_is_valid(cover_)) return;
        cover_ = nullptr;
        prompt_ = nullptr;
        track_ = nullptr;
        fill_ = nullptr;
        auto* parent = lv_layer_top();
        if (!parent) return;
        const auto tok = view::make_tokens(view_model_.pri_color(), view_model_.sec_color(), view_model_.bg_color());

        cover_ = lv_obj_create(parent);
        lv_obj_remove_style_all(cover_);
        lv_obj_set_size(cover_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(cover_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(cover_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(cover_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(cover_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(cover_, LV_OBJ_FLAG_HIDDEN);

        prompt_ = lv_obj_create(parent);
        lv_obj_remove_style_all(prompt_);
        lv_obj_set_size(prompt_, 248, 62);
        lv_obj_align(prompt_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(prompt_, 8, 0);
        lv_obj_set_style_bg_color(prompt_, tok.surface, 0);
        lv_obj_set_style_bg_opa(prompt_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(prompt_, 1, 0);
        lv_obj_set_style_border_color(prompt_, tok.main, 0);
        lv_obj_set_style_border_opa(prompt_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(prompt_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(prompt_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(prompt_, LV_OBJ_FLAG_HIDDEN);

        auto* label = lv_label_create(prompt_);
        auto* font = assets_.load_standard_font(12);
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, tok.ink, 0);
        lv_label_set_text(label, "Hold Enter to unlock");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);

        track_ = lv_obj_create(prompt_);
        lv_obj_remove_style_all(track_);
        lv_obj_set_size(track_, kTrackW, 8);
        lv_obj_align(track_, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_radius(track_, 3, 0);
        lv_obj_set_style_bg_color(track_, tok.rule, 0);
        lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(track_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);

        fill_ = lv_obj_create(track_);
        lv_obj_remove_style_all(fill_);
        lv_obj_set_size(fill_, 0, 8);
        lv_obj_align(fill_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(fill_, 3, 0);
        lv_obj_set_style_bg_color(fill_, tok.accent, 0);
        lv_obj_set_style_bg_opa(fill_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(fill_, LV_OBJ_FLAG_SCROLLABLE);
    }

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    lv_obj_t* cover_{nullptr};
    lv_obj_t* prompt_{nullptr};
    lv_obj_t* track_{nullptr};
    lv_obj_t* fill_{nullptr};
};

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets, AlarmWatcher& alarms)
        : view_model_(view_model),
          alarms_(alarms),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)),
          meter_(view_model, assets),
          pocket_ui_(view_model, assets) {
        toast_->build();
        help_popup_->build();
        hold_timer_ = lv_timer_create(hold_timer_cb, 40, this);
        if (hold_timer_) lv_timer_pause(hold_timer_);
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        if (pocket_ != Pocket::Off || clear_capture_on_release_) {
            platform::write_brightness_percent(saved_brightness_);
            platform::set_key_captured(false);
        }
        if (hold_timer_) {
            lv_timer_delete(hold_timer_);
            hold_timer_ = nullptr;
        }
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        // The unlocking Enter is still down after the hold completes. Swallow it
        // until release so it cannot activate whatever is on screen.
        if (clear_capture_on_release_) return true;
        if (pocket_ != Pocket::Off) return handle_pocket_key(key, long_pressed);
        if (alarms_.consuming_keys()) {
            return alarms_.handle_key(key);
        }
        // The matrix rain exits on any key, including help and screenshot.
        // Esc and Tab still arm the hold meter so a 3s hold can leave or lock.
        if (view_model_.current_page() == model::AppPage::Matrix && key != LV_KEY_ESC &&
            key != platform::kKeyTab) {
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

        // The 700ms long-press event must not restart a 3s hold or launch twice.
        if (long_pressed) {
            if (key == hold_key_ || !view_model_.model().shortcut_target(key, false).empty() ||
                !view_model_.model().shortcut_target(key, true).empty()) {
                return true;
            }
            return false;
        }

        if (key == LV_KEY_ESC) {
            // Hold-to-quit only on the home grid. Everywhere else a tap must leave
            // the current screen immediately (no release-gated back navigation).
            if (view_model_.current_page() == model::AppPage::ChannelGrid) {
                begin_hold(key, HoldKind::Exit, {}, {});
                return true;
            }
            return false;
        }
        if (key == platform::kKeyTab) {
            begin_hold(key, HoldKind::Lock, {}, {});
            return true;
        }

        if (view_model_.current_page() == model::AppPage::ChannelGrid) {
            const auto hold_id = view_model_.model().shortcut_target(key, true);
            const auto tap_id = view_model_.model().shortcut_target(key, false);
            if (!hold_id.empty()) {
                begin_hold(key, HoldKind::Launch, hold_id, tap_id);
                return true;
            }
            if (!tap_id.empty()) {
                view_model_.launch_target_id(tap_id);
                return true;
            }
        }
        return false;
    }

    void remember_output(const platform::OutputLevel& level) {
        auto& model = view_model_.model();
        model.set_sound_volume(level.percent);
        model.set_sound_enabled(!level.muted && level.percent > 0);
        model.save_user_config();
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
            const int step = key == platform::kKeyVolumeUp ? 10 : -10;
            platform::OutputLevel level;
            if (platform::adjust_output_volume(step, level)) {
                if (level.percent >= 0) remember_output(level);
                const std::string label = level.percent < 0
                                              ? std::string("Volume")
                                              : level.muted
                                                    ? "Volume " + std::to_string(level.percent) + "% muted"
                                                    : "Volume " + std::to_string(level.percent) + "%";
                toast_->show(label, view::widgets::ToastTone::Default, 1200);
                return true;
            }
            auto& model = view_model_.model();
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
            platform::OutputLevel level;
            if (platform::toggle_output_mute(level)) {
                if (level.percent >= 0) remember_output(level);
                toast_->show(level.muted ? "Muted" : "Sound on",
                             view::widgets::ToastTone::Default,
                             1200);
                return true;
            }
            auto& model = view_model_.model();
            const bool on = !model.sound_enabled();
            model.set_sound_enabled(on);
            model.save_user_config();
            toast_->show(on ? "Sound on" : "Muted", view::widgets::ToastTone::Default, 1200);
            return true;
        }
        if (key == platform::kKeyMediaPlay || key == platform::kKeyMediaPrev ||
            key == platform::kKeyMediaNext) {
            const char* action = "play-pause";
            const char* label = "Play / Pause";
            if (key == platform::kKeyMediaNext) {
                action = "next";
                label = "Next track";
            } else if (key == platform::kKeyMediaPrev) {
                action = "previous";
                label = "Previous track";
            }
            std::string error;
            if (platform::media_transport(action, error)) {
                toast_->show(label, view::widgets::ToastTone::Default, 1200);
            } else {
                toast_->show("No media player", view::widgets::ToastTone::Warning, 1400);
            }
            return true;
        }
        return false;
    }

    enum class HoldKind { None, Exit, Lock, Launch, Unlock };
    enum class Pocket { Off, Dark, Prompt };

    static constexpr uint32_t kHoldMs = 3000;
    static constexpr uint32_t kHoldShowMs = 1000;

    bool handle_pocket_key(uint32_t key, bool long_pressed) {
        if (pocket_ != Pocket::Prompt || long_pressed) return true;
        const bool enter = key == LV_KEY_ENTER || key == '\r';
        if (enter) {
            begin_hold(key, HoldKind::Unlock, {}, {});
            return true;
        }
        if (hold_kind_ == HoldKind::Unlock) cancel_hold();
        return true;
    }

    void cancel_hold() {
        hold_kind_ = HoldKind::None;
        hold_key_ = 0;
        hold_done_ = false;
        pending_tap_id_.clear();
        hold_launch_id_.clear();
        meter_.hide();
        meter_visible_ = false;
        pocket_ui_.set_bar_visible(false);
        if (hold_timer_) lv_timer_pause(hold_timer_);
    }

    void begin_hold(uint32_t key, HoldKind kind, std::string launch_id, std::string tap_id) {
        hold_key_ = key;
        hold_kind_ = kind;
        hold_launch_id_ = std::move(launch_id);
        pending_tap_id_ = std::move(tap_id);
        hold_started_ = lv_tick_get();
        hold_done_ = false;
        std::string label = "Exit";
        if (kind == HoldKind::Lock) {
            label = "Lock";
        } else if (kind == HoldKind::Unlock) {
            label = "Unlock";
        } else if (kind == HoldKind::Launch) {
            if (const auto* target = model::find_launch_target(hold_launch_id_)) label = target->label;
            else label = "Open";
        }
        hold_label_ = std::move(label);
        meter_.hide();
        meter_visible_ = false;
        if (hold_timer_) {
            lv_timer_resume(hold_timer_);
            lv_timer_reset(hold_timer_);
        }
    }

    void tick_hold() {
        if (hold_kind_ == HoldKind::None || hold_done_) return;
        const uint32_t elapsed = lv_tick_elaps(hold_started_);
        const bool unlock = hold_kind_ == HoldKind::Unlock;
        if (elapsed >= kHoldMs) {
            if (unlock) {
                pocket_ui_.set_bar_visible(true);
                pocket_ui_.set_ratio(1.f);
            } else {
                reveal_meter();
                meter_.set_ratio(1.f);
            }
            complete_hold();
            return;
        }
        // A tap must not flash the bar. It appears once the hold passes one second,
        // then fills across the remaining time until the action fires.
        if (elapsed < kHoldShowMs) return;
        const uint32_t span = kHoldMs - kHoldShowMs;
        const float ratio = span == 0 ? 1.f
                                       : static_cast<float>(elapsed - kHoldShowMs) / static_cast<float>(span);
        if (unlock) {
            pocket_ui_.set_bar_visible(true);
            pocket_ui_.set_ratio(ratio);
            return;
        }
        reveal_meter();
        meter_.set_ratio(ratio);
    }

    void reveal_meter() {
        if (meter_visible_) return;
        meter_.show(hold_label_);
        meter_visible_ = true;
    }

    void complete_hold() {
        const auto kind = hold_kind_;
        const auto id = hold_launch_id_;
        hold_done_ = true;
        meter_.hide();
        if (hold_timer_) lv_timer_pause(hold_timer_);
        if (kind == HoldKind::Exit) {
            view_model_.request_quit();
        } else if (kind == HoldKind::Lock) {
            engage_pocket();
        } else if (kind == HoldKind::Unlock) {
            release_pocket();
        } else if (kind == HoldKind::Launch) {
            view_model_.launch_target_id(id);
        }
    }

    void engage_pocket() {
        int32_t current = 0;
        if (platform::read_brightness_percent(current) && current > 0) saved_brightness_ = current;
        else saved_brightness_ = 70;
        platform::write_brightness_percent(0);
        if (help_popup_) help_popup_->hide();
        if (toast_) toast_->hide();
        meter_.hide();
        meter_visible_ = false;
        pocket_ui_.blank();
        pocket_ = Pocket::Dark;
        wake_keys_ = 0;
        pocket_armed_ = false;
        platform::set_key_captured(true);
    }

    void show_unlock_prompt() {
        platform::write_brightness_percent(saved_brightness_);
        pocket_ui_.show_prompt();
        pocket_ = Pocket::Prompt;
    }

    void release_pocket() {
        pocket_ = Pocket::Off;
        wake_keys_ = 0;
        pocket_armed_ = false;
        // Stay captured until the key that finished the hold is released.
        clear_capture_on_release_ = true;
        pocket_ui_.hide();
        meter_.hide();
        meter_visible_ = false;
    }

    void handle_release(uint32_t key) {
        if (pocket_ == Pocket::Dark) {
            if (hold_kind_ != HoldKind::None && key == hold_key_) cancel_hold();
            // The Tab that engaged the lock is still down. Its release must not
            // count as one of the two wake keys.
            if (!pocket_armed_) {
                pocket_armed_ = true;
                return;
            }
            if (++wake_keys_ >= 2) show_unlock_prompt();
            return;
        }
        if (pocket_ == Pocket::Prompt) {
            if (hold_kind_ == HoldKind::Unlock && key == hold_key_) {
                const bool done = hold_done_;
                cancel_hold();
                if (done) release_pocket();
            }
            return;
        }
        if (clear_capture_on_release_) {
            clear_capture_on_release_ = false;
            platform::set_key_captured(false);
        }
        if (key != hold_key_ || hold_kind_ == HoldKind::None) return;
        const bool done = hold_done_;
        const auto kind = hold_kind_;
        const auto tap = pending_tap_id_;
        hold_kind_ = HoldKind::None;
        hold_key_ = 0;
        hold_done_ = false;
        pending_tap_id_.clear();
        hold_launch_id_.clear();
        meter_.hide();
        meter_visible_ = false;
        if (hold_timer_) lv_timer_pause(hold_timer_);
        if (done) return;
        if (kind == HoldKind::Exit) {
            if (view_model_.current_page() != model::AppPage::ChannelGrid) {
                platform::deliver_screen_key(LV_KEY_ESC);
            }
        } else if (kind == HoldKind::Lock) {
            platform::deliver_screen_key(platform::kKeyTab);
        } else if (kind == HoldKind::Launch && !tap.empty()) {
            view_model_.launch_target_id(tap);
        }
    }

    static void hold_timer_cb(lv_timer_t* timer) {
        auto* self = static_cast<ShortcutController*>(lv_timer_get_user_data(timer));
        if (self) self->tick_hold();
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
    HoldMeter meter_;
    PocketLockUi pocket_ui_;
    lv_timer_t* hold_timer_{nullptr};
    Pocket pocket_{Pocket::Off};
    int wake_keys_{0};
    bool pocket_armed_{false};
    bool clear_capture_on_release_{false};
    int32_t saved_brightness_{70};
    HoldKind hold_kind_{HoldKind::None};
    uint32_t hold_key_{0};
    uint32_t hold_started_{0};
    bool hold_done_{false};
    bool meter_visible_{false};
    std::string hold_label_;
    std::string hold_launch_id_;
    std::string pending_tap_id_;
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
        if (cmd == "down" || cmd == "up") {
            std::string name;
            iss >> name;
            if (!frame_.inject_named_key_state(name, cmd == "down")) {
                LOG_WARN("ui script {} failed: {}", cmd, name);
            }
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
        } else if (lower == "discovery") {
            vm_.open_discovery();
        } else if (lower == "search") {
            vm_.open_search();
        } else if (lower == "shortcuts") {
            vm_.open_shortcut_menu();
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
        } else if (lower == "mac_vendor" || lower == "vendor_mac" || lower == "oui") {
            vm_.launch_target_id("page:mac_vendor");
        } else if (lower == "engcalc" || lower == "calculators") {
            vm_.open_eng_calc_menu();
        } else if (lower.rfind("engcalc_", 0) == 0) {
            // engcalc_<op_index> opens a calculator work screen directly.
            const int op = std::atoi(lower.c_str() + 8);
            vm_.open_standalone_eng_calc(op);
        } else if (lower.rfind("life_", 0) == 0) {
            const int game = std::atoi(lower.c_str() + 5);
            vm_.open_life_counter(game);
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

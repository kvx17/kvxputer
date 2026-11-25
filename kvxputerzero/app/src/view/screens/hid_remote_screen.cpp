#include "hid_remote_screen.h"

#include "asset_manager.h"
#include "elevate.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace screen {
namespace {

using namespace tools::hid;
using platform::hid::LinkState;

constexpr int kStatusY = 18;
constexpr int kStatusH = 14;
constexpr int kBodyY = kStatusY + kStatusH;
constexpr int kBodyH = view::kContentHeight - kBodyY;
constexpr uint32_t kFlashMs = 120;

bool is_arrow(uint32_t k) {
    return k == LV_KEY_UP || k == LV_KEY_DOWN || k == LV_KEY_LEFT || k == LV_KEY_RIGHT;
}

bool is_enter(uint32_t k) {
    return k == LV_KEY_ENTER || k == '\r';
}

uint8_t arrow_usage(uint32_t k) {
    switch (k) {
    case LV_KEY_UP: return key::Up;
    case LV_KEY_DOWN: return key::Down;
    case LV_KEY_LEFT: return key::Left;
    case LV_KEY_RIGHT: return key::Right;
    default: return 0;
    }
}

// Device-only keys that stay with the Zero (help, screenshot, backlight).
bool device_key(uint32_t k) {
    return k == platform::kKeyHelp || k == platform::kKeyPrintScreen || k == platform::kKeyBrightnessDown ||
           k == platform::kKeyBrightnessUp;
}

// Fn media legends (Fn+Q/W/E, Fn+A/S/D) → consumer usage for the host.
uint16_t media_usage(uint32_t k) {
    switch (k) {
    case platform::kKeyMediaPlay: return consumer::PlayPause;
    case platform::kKeyMediaPrev: return consumer::PrevTrack;
    case platform::kKeyMediaNext: return consumer::NextTrack;
    case platform::kKeyMute: return consumer::Mute;
    case platform::kKeyVolumeDown: return consumer::VolumeDown;
    case platform::kKeyVolumeUp: return consumer::VolumeUp;
    default: return 0;
    }
}

// Non-printable LV / platform keys → keyboard usage.
uint8_t special_usage(uint32_t k) {
    switch (k) {
    case LV_KEY_ENTER: // == '\n'
    case '\r': return key::Enter;
    case LV_KEY_BACKSPACE: return key::Backspace;
    case LV_KEY_DEL: return key::Delete;
    case LV_KEY_UP: return key::Up;
    case LV_KEY_DOWN: return key::Down;
    case LV_KEY_LEFT: return key::Left;
    case LV_KEY_RIGHT: return key::Right;
    case LV_KEY_HOME: return key::Home;
    case LV_KEY_END: return key::End;
    case LV_KEY_PREV: return key::PageUp;
    case LV_KEY_NEXT: return key::PageDown;
    case LV_KEY_ESC: return key::Esc;
    case platform::kKeyTab: return key::Tab;
    case platform::kKeyInsert: return key::Insert;
    default: break;
    }
    if (k >= platform::kKeyF1 && k <= platform::kKeyF12) {
        return key::function(static_cast<int>(k - platform::kKeyF1) + 1);
    }
    return 0;
}

std::string key_name(uint32_t k, const char* arrow = "Arrow") {
    if (k == 0) return arrow;
    if (k == ' ') return "SPC";
    return platform::describe_key(k);
}

int clampi(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

} // namespace

HidRemoteScreen::HidRemoteScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets), settings_(Settings::load()) {
    sel_by_phase_[static_cast<int>(Phase::Transport)] = static_cast<int>(settings_.transport);
    sel_by_phase_[static_cast<int>(Phase::Menu)] = settings_.last_mode;
    sel_by_phase_[static_cast<int>(Phase::UsbWait)] = 2;
    init();
}

HidRemoteScreen::~HidRemoteScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    platform::clear_early_key_listener(on_early_key, this);
    platform::clear_screen_key_release_listener(on_key_up, this);
    platform::set_text_entry(false);
    platform::set_alt_is_modifier(false);
    stop_transport();
}

void HidRemoteScreen::on_shown() {
    platform::set_early_key_listener(on_early_key, this);
    platform::set_screen_key_release_listener(on_key_up, this);
    platform::set_text_entry(phase_ == Phase::Edit || (phase_ == Phase::Mode && mode_ == Mode::Keyboard));
}

void HidRemoteScreen::build() {
    make_header(content(), kAppName);
    const auto tok = tokens();
    const lv_font_t* f10 = assets().load_standard_font(10);
    if (!f10) f10 = &lv_font_montserrat_12;

    status_lbl_ = lv_label_create(content());
    lv_obj_set_style_text_font(status_lbl_, f10, 0);
    lv_obj_set_style_text_color(status_lbl_, tok.ink_dim, 0);
    lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status_lbl_, view::kScreenWidth - 110);
    lv_obj_set_pos(status_lbl_, 6, kStatusY + 1);

    pill_ = view::make_status_pill(content(), "Idle", view::StatusPillKind::Idle, f10, tok);
    lv_obj_align(pill_, LV_ALIGN_TOP_RIGHT, -4, kStatusY);

    body_ = lv_obj_create(content());
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, view::kScreenWidth, kBodyH);
    lv_obj_set_pos(body_, 0, kBodyY);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);

    timer_ = lv_timer_create(timer_cb, 10, this);
    rebuild_body();
    refresh_status();
}

// --- chrome -------------------------------------------------------------------------

void HidRemoteScreen::set_phase(Phase p) {
    if (phase_ != Phase::Mode && phase_ != Phase::Edit) {
        sel_by_phase_[static_cast<int>(phase_)] = sel_;
    }
    phase_ = p;
    forget_all_armed_ = false;
    sel_ = sel_by_phase_[static_cast<int>(p)];
    platform::set_text_entry(p == Phase::Edit || (p == Phase::Mode && mode_ == Mode::Keyboard));
    rebuild_body();
    rebuild_action_bar();
    refresh_status();
}

void HidRemoteScreen::rebuild_body() {
    if (!body_) return;
    lv_obj_clean(body_);
    list_ = nullptr;
    list_rows_.clear();
    slot_tiles_.clear();
    cells_.clear();
    edit_lbl_ = nullptr;
    mode_text_ = nullptr;
    mode_hint_ = nullptr;
    mode_state_ = nullptr;

    switch (phase_) {
    case Phase::Hosts: build_hosts_grid(); break;
    case Phase::UsbWait: build_usb_checklist(); break;
    case Phase::Mode: build_mode_ui(); break;
    case Phase::Edit: {
        const auto tok = tokens();
        const lv_font_t* f12 = assets().load_standard_font(12);
        const lv_font_t* f10 = assets().load_standard_font(10);
        auto* title = lv_label_create(body_);
        lv_label_set_text(title, edit_title_.c_str());
        lv_obj_set_style_text_font(title, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, tok.ink, 0);
        lv_obj_set_pos(title, 8, 6);
        auto* box = lv_obj_create(body_);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, view::kScreenWidth - 16, 26);
        lv_obj_set_pos(box, 8, 28);
        lv_obj_set_style_radius(box, 5, 0);
        lv_obj_set_style_bg_color(box, tok.surface_sel, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, tok.accent, 0);
        edit_lbl_ = lv_label_create(box);
        lv_obj_set_style_text_font(edit_lbl_, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(edit_lbl_, tok.ink, 0);
        lv_obj_align(edit_lbl_, LV_ALIGN_LEFT_MID, 6, 0);
        auto* hint = lv_label_create(body_);
        lv_label_set_text(hint, edit_secret_ ? "Type your device administrator password.\nEnter confirms.  Esc cancels."
                                            : "Enter save   Esc cancel   Bksp delete");
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, view::kScreenWidth - 16);
        lv_obj_set_style_text_font(hint, f10 ? f10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hint, tok.ink_dim, 0);
        lv_obj_set_pos(hint, 8, 62);
        refresh_edit();
        break;
    }
    default: build_list(rows_for_phase(), sel_); break;
    }
}

void HidRemoteScreen::show_message(const std::string& text, bool error) {
    message_ = text;
    message_error_ = error;
    // Errors stay up long enough to read; the next action replaces them.
    message_until_ = lv_tick_get() + (error ? 15000 : 2600);
    refresh_status();
}

void HidRemoteScreen::refresh_status() {
    if (!status_lbl_ || !pill_) return;
    const auto tok = tokens();
    const LinkState st = link_state();
    std::string left;
    if (!message_.empty() && lv_tick_get() < message_until_) {
        left = message_;
        lv_obj_set_style_text_color(status_lbl_, message_error_ ? lv_color_hex(0xE05050) : tok.accent, 0);
    } else {
        message_.clear();
        lv_obj_set_style_text_color(status_lbl_, tok.ink_dim, 0);
        if (phase_ == Phase::Mode) {
            left = mode_label(mode_);
            const std::string host = session_ ? session_->transport().host_label() : "";
            if (!host.empty()) left += "  |  " + host;
        } else if (session_) {
            left = session_->transport().detail();
        } else {
            left = "Pick how to reach the host";
        }
    }
    lv_label_set_text(status_lbl_, left.c_str());

    std::string pill = session_ ? std::string(session_->transport().name()) + " " : std::string();
    view::StatusPillKind kind = view::StatusPillKind::Idle;
    switch (st) {
    case LinkState::Ready:
        pill += "linked";
        kind = view::StatusPillKind::Ok;
        break;
    case LinkState::Handshake:
        pill += "pairing";
        kind = view::StatusPillKind::Busy;
        break;
    case LinkState::Waiting:
        pill += (session_ && session_->transport().pairing()) ? "open" : "waiting";
        kind = view::StatusPillKind::Busy;
        break;
    default: pill = "offline"; break;
    }
    view::set_status_pill(pill_, pill.c_str(), kind, tok);
    lv_obj_align(pill_, LV_ALIGN_TOP_RIGHT, -4, kStatusY);
}

// --- lists --------------------------------------------------------------------------

void HidRemoteScreen::build_list(const std::vector<Row>& rows, int sel) {
    rows_ = rows;
    const auto tok = tokens();
    const lv_font_t* f12 = assets().load_standard_font(12);
    list_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, view::kScreenWidth, kBodyH);
    lv_obj_set_pos(list_, 0, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 2, 0);
    lv_obj_set_style_pad_top(list_, 2, 0);
    lv_obj_set_style_pad_bottom(list_, 2, 0);
    lv_obj_set_style_pad_left(list_, 6, 0);
    lv_obj_set_style_pad_right(list_, 6, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    for (const auto& r : rows_) {
        list_rows_.push_back(view::make_list_row(list_, r.text.c_str(), nullptr, f12, nullptr, tok));
    }
    sel_ = rows_.empty() ? 0 : clampi(sel, 0, static_cast<int>(rows_.size()) - 1);
    for (size_t i = 0; i < list_rows_.size(); ++i) {
        view::apply_row_state(list_rows_[i], static_cast<int>(i) == sel_, tok);
    }
    if (!list_rows_.empty()) lv_obj_scroll_to_view(list_rows_[static_cast<size_t>(sel_)], LV_ANIM_OFF);
}

void HidRemoteScreen::list_move(int delta) {
    if (list_rows_.empty()) return;
    const int n = static_cast<int>(list_rows_.size());
    const int next = (sel_ + delta + n) % n;
    const auto tok = tokens();
    view::apply_row_state(list_rows_[static_cast<size_t>(sel_)], false, tok);
    sel_ = next;
    view::apply_row_state(list_rows_[static_cast<size_t>(sel_)], true, tok);
    lv_obj_scroll_to_view(list_rows_[static_cast<size_t>(sel_)], LV_ANIM_OFF);
}

void HidRemoteScreen::list_enter() {
    if (sel_ < 0 || sel_ >= static_cast<int>(rows_.size())) return;
    const Phase before = phase_;
    auto fn = rows_[static_cast<size_t>(sel_)].enter;
    if (fn) fn();
    // Settings rows change their own label; rebuild in place.
    if (phase_ == before && (phase_ == Phase::Settings || phase_ == Phase::HostMenu)) {
        rebuild_body();
    }
}

void HidRemoteScreen::list_adjust(int dir) {
    if (sel_ < 0 || sel_ >= static_cast<int>(rows_.size())) return;
    auto fn = rows_[static_cast<size_t>(sel_)].adjust;
    if (!fn) return;
    fn(dir);
    settings_.clamp();
    settings_.save();
    rebuild_body();
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::rows_for_phase() {
    switch (phase_) {
    case Phase::Transport: return transport_rows();
    case Phase::Menu: return menu_rows();
    case Phase::Settings: return settings_rows();
    case Phase::Shortcuts: return shortcut_rows();
    case Phase::HostMenu: return host_menu_rows();
    default: return {};
    }
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::transport_rows() {
    std::vector<Row> rows = {
        {"USB cable  (switch on Slave)", [this]() { start_transport(TransportPref::Usb); }, nullptr},
        {"Bluetooth LE", [this]() { start_transport(TransportPref::Ble); }, nullptr},
    };
#if USE_DESKTOP
    rows.push_back({"Simulator (logs reports)", [this]() { start_transport(TransportPref::Sim); }, nullptr});
#endif
    rows.push_back({"Settings", [this]() { set_phase(Phase::Settings); }, nullptr});
    return rows;
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::menu_rows() {
    std::vector<Row> rows;
    for (int i = 0; i < kModeCount; ++i) {
        rows.push_back({mode_label(static_cast<Mode>(i)), [this, i]() { enter_mode(static_cast<Mode>(i)); }, nullptr});
    }
    if (session_ && session_->transport().has_hosts()) {
        rows.push_back({"Hosts...", [this]() {
                            auto_advance_ = false; // managing hosts: stay until Enter / Esc
                            set_phase(Phase::Hosts);
                        },
                        nullptr});
    }
    rows.push_back({"Settings", [this]() { set_phase(Phase::Settings); }, nullptr});
    rows.push_back({"Change transport", [this]() {
                        stop_transport();
                        set_phase(Phase::Transport);
                    },
                    nullptr});
    return rows;
}

void HidRemoteScreen::adjust_setting(int& field, int delta, int lo, int hi) {
    field = clampi(field + delta, lo, hi);
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::settings_rows() {
    auto& s = settings_;
    std::vector<Row> rows;
    auto num = [](const char* label, int v, const char* unit) {
        return std::string(label) + ": " + std::to_string(v) + unit;
    };
    auto save = [this]() {
        settings_.clamp();
        settings_.save();
    };
    rows.push_back({"BLE name: " + s.ble_name,
                    [this, save]() {
                        begin_edit("Bluetooth name", settings_.ble_name, 20,
                                   [this, save](const std::string& v) {
                                       if (!v.empty()) settings_.ble_name = v;
                                       save();
                                       show_message("Applies next time BLE starts");
                                   },
                                   Phase::Settings);
                    },
                    nullptr});
    rows.push_back({num("Mouse sensitivity", s.mouse_sensitivity, ""),
                    [this, save]() {
                        settings_.mouse_sensitivity = settings_.mouse_sensitivity % 10 + 1;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.mouse_sensitivity, d, 1, 10); }});
    rows.push_back({num("Jiggler interval", s.jiggler_interval, "s"),
                    [this, save]() {
                        settings_.jiggler_interval += 5;
                        if (settings_.jiggler_interval > 600) settings_.jiggler_interval = 5;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.jiggler_interval, d * 5, 5, 600); }});
    rows.push_back({num("Jiggler amount", s.jiggler_amount, "px"),
                    [this, save]() {
                        settings_.jiggler_amount = settings_.jiggler_amount % 20 + 1;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.jiggler_amount, d, 1, 50); }});
    rows.push_back({std::string("Jiggler direction: ") + jiggler_dir_label(s.jiggler_dir),
                    [this, save]() {
                        settings_.jiggler_dir = (settings_.jiggler_dir + 1) % 3;
                        save();
                    },
                    [this](int d) { settings_.jiggler_dir = (settings_.jiggler_dir + d + 3) % 3; }});
    rows.push_back({num("Stealth min", s.stealth_min, "s"),
                    [this, save]() {
                        settings_.stealth_min += 5;
                        if (settings_.stealth_min > 890) settings_.stealth_min = 10;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.stealth_min, d * 5, 10, 890); }});
    rows.push_back({num("Stealth max", s.stealth_max, "s"),
                    [this, save]() {
                        settings_.stealth_max += 10;
                        if (settings_.stealth_max > 900) settings_.stealth_max = settings_.stealth_min + 10;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.stealth_max, d * 10, settings_.stealth_min + 10, 900); }});
    rows.push_back({num("Clicker delay", s.clicker_delay, "ms"),
                    [this, save]() {
                        settings_.clicker_delay += 50;
                        if (settings_.clicker_delay > 10000) settings_.clicker_delay = 10;
                        save();
                    },
                    [this](int d) { adjust_setting(settings_.clicker_delay, d * 50, 10, 10000); }});
    rows.push_back({std::string("Clicker button: ") + clicker_button_label(s.clicker_button),
                    [this, save]() {
                        settings_.clicker_button = (settings_.clicker_button + 1) % 3;
                        save();
                    },
                    [this](int d) { settings_.clicker_button = (settings_.clicker_button + d + 3) % 3; }});
    rows.push_back({std::string("PTT key: ") + ptt_preset(s.ptt_preset).label,
                    [this, save]() {
                        settings_.ptt_preset = (settings_.ptt_preset + 1) % kPttPresetCount;
                        save();
                    },
                    [this](int d) {
                        settings_.ptt_preset = (settings_.ptt_preset + d + kPttPresetCount) % kPttPresetCount;
                    }});
    rows.push_back({"Shorts keys: " + key_name(s.shorts_up, "F") + " / " + key_name(s.shorts_down, "X") + "  (Enter resets)",
                    [this, save]() {
                        settings_.shorts_up = 0;
                        settings_.shorts_down = 0;
                        save();
                    },
                    nullptr});
    rows.push_back({std::string("Alt key: ") + (s.alt_is_modifier ? "sends Alt to host" : "Sym layer (default)"),
                    [this, save]() {
                        settings_.alt_is_modifier = !settings_.alt_is_modifier;
                        save();
                        show_message(settings_.alt_is_modifier ? "If Sym now sends Alt, switch back"
                                                               : "Alt acts as Sym again");
                    },
                    nullptr});
    rows.push_back({forget_all_armed_ ? "Forget ALL BLE hosts? Enter again" : "Forget all BLE hosts",
                    [this]() {
                        if (!forget_all_armed_) {
                            forget_all_armed_ = true;
                            return;
                        }
                        forget_all_armed_ = false;
                        bool any = false;
                        if (session_ && session_->transport().has_hosts()) any = session_->transport().forget_all();
                        for (auto& slot : settings_.host_slots) slot.clear();
                        settings_.preferred_host.clear();
                        settings_.save();
                        sync_known_hosts();
                        show_message(any ? "Forgot all hosts" : "Host slots cleared");
                    },
                    nullptr});
    rows.push_back({"Back", [this]() { set_phase(session_ ? Phase::Menu : Phase::Transport); }, nullptr});
    return rows;
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::shortcut_rows() {
    std::vector<Row> rows;
    const auto& list = shortcuts();
    for (size_t i = 0; i < list.size(); ++i) {
        rows.push_back({list[i].label,
                        [this, i]() {
                            const auto& sc = shortcuts()[i];
                            if (session_) session_->tap(sc.mods, sc.usage, 40);
                            show_message(std::string("Sent ") + sc.label);
                        },
                        nullptr});
    }
    rows.push_back({"Back", [this]() {
                        if (shortcuts_back_ == Phase::Mode) set_phase(Phase::Mode);
                        else set_phase(Phase::Menu);
                    },
                    nullptr});
    return rows;
}

std::vector<HidRemoteScreen::Row> HidRemoteScreen::host_menu_rows() {
    const int slot = host_menu_slot_;
    const std::string addr = (slot >= 1 && slot <= kHostSlotCount) ? settings_.host_slots[slot - 1] : "";
    const bool live = !addr.empty() && connected_addr() == addr;
    return {
        {"Slot " + std::to_string(slot) + ": " + slot_name(slot) + (live ? "  (connected)" : ""), nullptr, nullptr},
        {"Connect / switch", [this, slot]() { select_slot(slot); }, nullptr},
        {"Rename",
         [this, addr]() {
             begin_edit("Host name", slot_name(host_menu_slot_), 24,
                        [this, addr](const std::string& v) {
                            if (!v.empty() && session_) session_->transport().rename_host(addr, v);
                        },
                        Phase::HostMenu);
         },
         nullptr},
        {"Disconnect",
         [this, live]() {
             if (!live) {
                 show_message("Not connected to this host");
                 return;
             }
             session_->transport().set_selected_host("");
             session_->transport().disconnect_host();
             show_message("Disconnected");
         },
         nullptr},
        {"Forget this host",
         [this, slot, addr]() {
             if (session_) session_->transport().forget_host(addr);
             settings_.host_slots[slot - 1].clear();
             if (settings_.preferred_host == addr) settings_.preferred_host.clear();
             settings_.save();
             sync_known_hosts();
             show_message("Forgot slot " + std::to_string(slot));
             set_phase(Phase::Hosts);
         },
         nullptr},
        {"Back", [this]() { set_phase(Phase::Hosts); }, nullptr},
    };
}

// --- transport ----------------------------------------------------------------------

platform::hid::LinkState HidRemoteScreen::link_state() {
    return session_ ? session_->transport().state() : LinkState::Down;
}

void HidRemoteScreen::start_transport(TransportPref pref) {
#if !USE_DESKTOP
    if ((pref == TransportPref::Usb || pref == TransportPref::Ble) && !platform::root_session_ready()) {
        begin_edit(
            "Enter administrator password", "", 128,
            [this, pref](const std::string& pw) {
                std::string err;
                if (!platform::accept_root_password(pw, err)) {
                    show_message(err.empty() ? "Password rejected" : err, true);
                    start_transport(pref);
                    return;
                }
                start_transport(pref);
            },
            Phase::Transport, true);
        return;
    }
#endif
    stop_transport();
    std::unique_ptr<platform::hid::Transport> t;
    switch (pref) {
    case TransportPref::Usb: t = platform::hid::make_usb_transport(); break;
    case TransportPref::Ble: t = platform::hid::make_ble_transport(settings_.ble_name); break;
    case TransportPref::Sim: t = platform::hid::make_sim_transport(); break;
    }
    if (pref == TransportPref::Ble) {
        // Unblock the radio, start bluetoothd, grant D-Bus access: no shell needed.
        show_message("Preparing Bluetooth...");
        lv_refr_now(nullptr);
        std::string note;
        if (!platform::hid::prepare_bluetooth(note)) {
            show_message(note, true);
            return;
        }
    }
    show_message(pref == TransportPref::Usb ? "Starting USB gadget..." : "Starting...");
    lv_refr_now(nullptr);
    session_ = std::make_unique<platform::hid::Session>(std::move(t));
    std::string err;
    if (!session_->start(err)) {
        session_.reset();
        if (pref == TransportPref::Usb) {
            // Usually the switch is on Host: show the checklist and retry.
            usb_error_ = err.empty() ? "USB gadget failed" : err;
            last_usb_retry_ = lv_tick_get();
            settings_.transport = pref;
            settings_.save();
            message_.clear();
            set_phase(Phase::UsbWait);
            return;
        }
        show_message(err.empty() ? "Could not start" : err, true);
        return;
    }
    usb_error_.clear();
    settings_.transport = pref;
    settings_.save();
    message_.clear();
    if (pref == TransportPref::Usb) {
        set_phase(link_state() == LinkState::Ready ? Phase::Menu : Phase::UsbWait);
    } else if (pref == TransportPref::Ble) {
        sync_known_hosts();
        session_->transport().set_selected_host(settings_.preferred_host);
        auto_advance_ = true; // startup: continue to modes once a host links
        set_phase(Phase::Hosts);
        // First use: nothing paired yet, so open pairing right away instead of
        // waiting for the user to discover the slot keys.
        bool any = false;
        for (const auto& a : settings_.host_slots) any = any || !a.empty();
        if (!any) pair_into_slot(1);
    } else {
        set_phase(Phase::Menu);
    }
}

// The Host/Slave switch is physical, so the app cannot flip it: show where the
// user is in the three steps and keep retrying in the background (tick_usb_wait).
std::vector<HidRemoteScreen::Check> HidRemoteScreen::usb_checklist() {
    std::vector<Check> out;
    const bool host_side = platform::hid::usb_switch_on_host();
    out.push_back({host_side ? CheckState::Todo : CheckState::Ok,
                   host_side ? "Slide the USB switch on the left side UP, to Slave"
                             : "USB switch is on Slave"});
    const LinkState st = link_state();
    out.push_back({st == LinkState::Ready || st == LinkState::Handshake ? CheckState::Ok : CheckState::Wait,
                   "Cable from the RIGHT USB-C port (not USB-A) to the computer"});
    if (!session_) {
        out.push_back({usb_error_.empty() || host_side ? CheckState::Wait : CheckState::Fail,
                       usb_error_.empty() ? std::string("Starting USB...") : usb_error_});
    } else if (st == LinkState::Ready) {
        out.push_back({CheckState::Ok, "Computer sees the keyboard"});
    } else if (st == LinkState::Handshake) {
        out.push_back({CheckState::Wait, "Computer is setting up the keyboard..."});
    } else {
        out.push_back({CheckState::Wait, "Waiting for the computer"});
    }
    return out;
}

void HidRemoteScreen::build_usb_checklist() {
    const auto tok = tokens();
    const lv_font_t* f12 = assets().load_standard_font(12);
    const lv_font_t* f10 = assets().load_standard_font(10);
    int y = 4;
    int n = 1;
    for (const auto& c : usb_checklist()) {
        auto* mark = lv_label_create(body_);
        const char* text = c.state == CheckState::Ok ? "OK" : c.state == CheckState::Fail ? "!!" : c.state == CheckState::Todo ? "->" : "..";
        lv_label_set_text(mark, text);
        lv_obj_set_style_text_font(mark, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_color_t col = tok.ink_dim;
        if (c.state == CheckState::Ok) col = tok.accent;
        else if (c.state == CheckState::Fail) col = lv_color_hex(0xE05050);
        else if (c.state == CheckState::Todo) col = lv_color_hex(0xF0A020);
        lv_obj_set_style_text_color(mark, col, 0);
        lv_obj_set_pos(mark, 8, y);

        auto* label = lv_label_create(body_);
        lv_label_set_text_fmt(label, "%d. %s", n++, c.text.c_str());
        lv_obj_set_style_text_font(label, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, c.state == CheckState::Ok ? tok.ink_dim : tok.ink, 0);
        lv_obj_set_width(label, view::kScreenWidth - 44);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(label, 34, y);
        lv_obj_update_layout(label);
        y += std::max(16, static_cast<int>(lv_obj_get_height(label))) + 6;
    }
    auto* hint = lv_label_create(body_);
    lv_label_set_text(hint, session_ ? "Enter: continue without waiting   Esc: back" : "Retrying automatically   Esc: back");
    lv_obj_set_style_text_font(hint, f10 ? f10 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, tok.ink_dim, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 8, -1);
}

void HidRemoteScreen::tick_usb_wait(uint32_t now) {
    if (session_ && link_state() == LinkState::Ready) {
        set_phase(Phase::Menu);
        return;
    }
    // Collect a finished background attempt.
    if (usb_retry_.valid() &&
        usb_retry_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [s, err] = usb_retry_.get();
        last_usb_retry_ = now;
        if (s) {
            session_ = std::move(s);
            usb_error_.clear();
        } else if (!err.empty()) {
            usb_error_ = err;
        }
    }
    // The helper (modprobe, configfs) can take a moment; never block the UI on it.
    if (!session_ && !usb_retry_.valid() && now - last_usb_retry_ >= 2000) {
        usb_retry_ = std::async(std::launch::async, []() {
            auto s = std::make_unique<platform::hid::Session>(platform::hid::make_usb_transport());
            std::string err;
            if (!s->start(err)) s.reset();
            return std::make_pair(std::move(s), err);
        });
    }
    // Redraw the checklist only when a line actually changes.
    std::string sig = session_ ? "S" : "-";
    for (const auto& c : usb_checklist()) sig += std::to_string(static_cast<int>(c.state)) + c.text + "\n";
    if (sig != usb_rows_sig_) {
        usb_rows_sig_ = sig;
        rebuild_body();
    }
}

void HidRemoteScreen::stop_transport() {
    // A background USB attempt may still finish; take it and shut it down.
    if (usb_retry_.valid()) {
        auto result = usb_retry_.get();
        if (result.first) result.first->stop();
    }
    if (session_) {
        ptt_set(false);
        session_->stop();
        session_.reset();
    }
    pairing_slot_ = 0;
    waiting_slot_ = 0;
}

std::string HidRemoteScreen::connected_addr() {
    if (!session_ || !session_->transport().has_hosts()) return {};
    for (const auto& h : session_->transport().hosts()) {
        if (h.connected) return h.addr;
    }
    return {};
}

void HidRemoteScreen::sync_known_hosts() {
    if (!session_) return;
    std::vector<std::string> addrs;
    for (const auto& a : settings_.host_slots) {
        if (!a.empty()) addrs.push_back(a);
    }
    platform::hid::set_ble_known_hosts(session_->transport(), addrs);
}

std::string HidRemoteScreen::slot_name(int slot) {
    if (slot < 1 || slot > kHostSlotCount) return "";
    const std::string& addr = settings_.host_slots[slot - 1];
    if (addr.empty()) return "empty";
    if (session_) {
        for (const auto& h : session_->transport().hosts()) {
            if (h.addr == addr && !h.name.empty()) return h.name;
        }
    }
    return addr;
}

void HidRemoteScreen::build_hosts_grid() {
    const auto tok = tokens();
    const lv_font_t* f12 = assets().load_standard_font(12);
    const lv_font_t* f10 = assets().load_standard_font(10);
    constexpr int cols = 2, rows = 3, gap = 3, pad = 6, hint_h = 12;
    const int w = (view::kScreenWidth - pad * 2 - gap) / cols;
    const int h = (kBodyH - hint_h - gap * (rows - 1) - 2) / rows;
    const std::string live = connected_addr();
    for (int i = 0; i < kHostSlotCount; ++i) {
        const int slot = i + 1;
        const std::string& addr = settings_.host_slots[i];
        auto* tile = lv_obj_create(body_);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, w, h);
        lv_obj_set_pos(tile, pad + (i % cols) * (w + gap), (i / cols) * (h + gap));
        lv_obj_set_style_radius(tile, 5, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, tok.surface, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_color_t border = tok.rule;
        if (!addr.empty()) border = tok.main;
        if (!addr.empty() && addr == live) border = tok.accent;
        if (slot == pairing_slot_ || slot == waiting_slot_) border = lv_color_hex(0xF0A020);
        lv_obj_set_style_border_color(tile, border, 0);
        if (!addr.empty() && addr == live) lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        auto* num = lv_label_create(tile);
        lv_label_set_text_fmt(num, "%d", slot);
        lv_obj_set_style_text_font(num, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(num, tok.accent, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 6, 0);

        auto* name = lv_label_create(tile);
        std::string text = slot_name(slot);
        if (slot == pairing_slot_) text = "pairing...";
        else if (slot == waiting_slot_) text = "waiting: " + text;
        lv_label_set_text(name, text.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(name, w - 26);
        lv_obj_set_style_text_font(name, f10 ? f10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, addr.empty() ? tok.ink_dim : tok.ink, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 20, 0);
        slot_tiles_.push_back(tile);
    }
    auto* hint = lv_label_create(body_);
    lv_label_set_text(hint, "1-6 pick  hold=options  Enter go  S settings  Esc back");
    lv_obj_set_style_text_font(hint, f10 ? f10 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, tok.ink_dim, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -1);
}

void HidRemoteScreen::select_slot(int slot) {
    if (!session_ || slot < 1 || slot > kHostSlotCount) return;
    const std::string addr = settings_.host_slots[slot - 1];
    if (addr.empty()) {
        pair_into_slot(slot);
        return;
    }
    auto& t = session_->transport();
    t.set_pairing(false);
    pairing_slot_ = 0;
    t.set_selected_host(addr); // drops any other host that is linked or races in
    settings_.preferred_host = addr;
    settings_.save();
    if (connected_addr() == addr && link_state() == LinkState::Ready) {
        waiting_slot_ = 0;
        set_phase(Phase::Menu);
        return;
    }
    waiting_slot_ = slot;
    auto_advance_ = true;
    show_message("Waiting for " + slot_name(slot) + " - open its Bluetooth menu");
    if (phase_ != Phase::Hosts) set_phase(Phase::Hosts);
    else rebuild_body();
}

void HidRemoteScreen::pair_into_slot(int slot) {
    if (!session_) return;
    auto& t = session_->transport();
    waiting_slot_ = 0;
    pairing_slot_ = slot;
    t.set_selected_host("*new*"); // only a host that is not already bonded
    t.set_pairing(true);
    show_message("Pair \"" + settings_.ble_name + "\" from the host");
    if (phase_ != Phase::Hosts) set_phase(Phase::Hosts);
    else rebuild_body();
}

void HidRemoteScreen::hosts_enter() {
    if (!session_) return;
    if (link_state() == LinkState::Ready) {
        set_phase(Phase::Menu);
        return;
    }
    int slot = 0;
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (!settings_.preferred_host.empty() && settings_.host_slots[i] == settings_.preferred_host) slot = i + 1;
    }
    for (int i = 0; slot == 0 && i < kHostSlotCount; ++i) {
        if (!settings_.host_slots[i].empty()) slot = i + 1;
    }
    for (int i = 0; slot == 0 && i < kHostSlotCount; ++i) {
        if (settings_.host_slots[i].empty()) slot = i + 1;
    }
    if (slot > 0) select_slot(slot);
}

void HidRemoteScreen::poll_hosts() {
    if (!session_ || !session_->transport().has_hosts()) return;
    auto& t = session_->transport();
    if (pairing_slot_ > 0) {
        for (const auto& h : t.hosts()) {
            if (!h.connected || !h.paired) continue;
            bool known = false;
            for (const auto& a : settings_.host_slots) known = known || a == h.addr;
            if (known) continue;
            settings_.host_slots[pairing_slot_ - 1] = h.addr;
            settings_.preferred_host = h.addr;
            settings_.save();
            sync_known_hosts();
            t.set_pairing(false);
            t.set_selected_host(h.addr);
            show_message("Slot " + std::to_string(pairing_slot_) + ": " + h.name);
            pairing_slot_ = 0;
            auto_advance_ = true;
            if (phase_ == Phase::Hosts) rebuild_body();
            break;
        }
    }
    if (phase_ == Phase::Hosts && auto_advance_ && link_state() == LinkState::Ready && pairing_slot_ == 0) {
        const std::string live = connected_addr();
        if (!live.empty() && (waiting_slot_ == 0 || settings_.host_slots[waiting_slot_ - 1] == live)) {
            waiting_slot_ = 0;
            auto_advance_ = false;
            set_phase(Phase::Menu);
        }
    }
    // Keep slot borders (live / waiting) current while the grid is up.
    if (phase_ == Phase::Hosts) {
        const std::string live = connected_addr();
        if (live != last_live_addr_) {
            last_live_addr_ = live;
            rebuild_body();
        }
    }
}

// --- text entry ---------------------------------------------------------------------

void HidRemoteScreen::begin_edit(const std::string& title, const std::string& initial, size_t max_len,
                                 std::function<void(const std::string&)> done, Phase back, bool secret) {
    edit_title_ = title;
    edit_buf_ = initial;
    edit_max_ = max_len;
    edit_done_ = std::move(done);
    edit_back_ = back;
    edit_secret_ = secret;
    set_phase(Phase::Edit);
}

void HidRemoteScreen::refresh_edit() {
    if (!edit_lbl_) return;
    std::string shown;
    if (edit_secret_ && edit_buf_.empty()) shown = "type password_";
    else shown = (edit_secret_ ? std::string(edit_buf_.size(), '*') : edit_buf_) + "_";
    lv_label_set_text(edit_lbl_, shown.c_str());
    if (edit_lbl_) {
        const auto tok = tokens();
        lv_obj_set_style_text_color(edit_lbl_, (edit_secret_ && edit_buf_.empty()) ? tok.ink_dim : tok.ink, 0);
    }
}

bool HidRemoteScreen::handle_edit_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        set_phase(edit_back_);
        return true;
    }
    if (is_enter(key)) {
        std::string v = edit_buf_;
        if (!edit_secret_) {
            while (!v.empty() && v.back() == ' ') v.pop_back();
        }
        edit_buf_.clear();
        auto done = edit_done_;
        set_phase(edit_back_);
        if (done) done(v);
        if (phase_ == Phase::Settings || phase_ == Phase::HostMenu) rebuild_body();
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (!edit_buf_.empty()) edit_buf_.pop_back();
        refresh_edit();
        return true;
    }
    if (key >= 32 && key <= 126 && edit_buf_.size() < edit_max_) {
        edit_buf_ += static_cast<char>(key);
        refresh_edit();
    }
    return true;
}

// --- modes --------------------------------------------------------------------------

void HidRemoteScreen::enter_mode(Mode m) {
    if (!session_) return;
    mode_ = m;
    settings_.last_mode = static_cast<int>(m);
    settings_.save();
    if (m == Mode::Shortcuts) {
        shortcuts_back_ = Phase::Menu;
        set_phase(Phase::Shortcuts);
        return;
    }
    mirror_.clear();
    running_ = false;
    clicks_ = 0;
    moves_ = 0;
    shorts_remap_ = 0;
    esc_ = {};
    tab_ = {};
    platform::set_alt_is_modifier(m == Mode::Keyboard && settings_.alt_is_modifier);
    set_phase(Phase::Mode);
}

void HidRemoteScreen::leave_mode() {
    running_ = false;
    ptt_latched_ = false;
    ptt_set(false);
    if (session_) {
        session_->flush();
        session_->release_all();
    }
    platform::set_alt_is_modifier(false);
    set_phase(Phase::Menu);
}

lv_obj_t* HidRemoteScreen::make_cell(int id, int x, int y, int w, int h, const char* key, const char* desc) {
    const auto tok = tokens();
    const lv_font_t* f12 = assets().load_standard_font(12);
    const lv_font_t* f10 = assets().load_standard_font(10);
    auto* box = lv_obj_create(body_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_radius(box, 5, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, tok.surface, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, tok.rule, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    const bool two_line = desc && desc[0] && h >= 26;
    auto* k = lv_label_create(box);
    lv_label_set_text(k, key ? key : "");
    lv_obj_set_style_text_font(k, f12 ? f12 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(k, tok.accent, 0);
    if (desc && desc[0]) {
        auto* d = lv_label_create(box);
        lv_label_set_text(d, desc);
        lv_obj_set_style_text_font(d, f10 ? f10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(d, tok.ink_dim, 0);
        if (two_line) {
            lv_obj_align(k, LV_ALIGN_TOP_MID, 0, 1);
            lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -1);
        } else {
            lv_obj_align(k, LV_ALIGN_LEFT_MID, 4, 0);
            lv_obj_align(d, LV_ALIGN_RIGHT_MID, -4, 0);
        }
    } else {
        lv_obj_center(k);
    }
    cells_.push_back({box, id});
    return box;
}

void HidRemoteScreen::flash(int id) {
    const auto tok = tokens();
    for (auto& c : cells_) {
        const bool on = c.id == id;
        lv_obj_set_style_border_color(c.box, on ? tok.accent : tok.rule, 0);
        lv_obj_set_style_border_width(c.box, on ? 2 : 1, 0);
        lv_obj_set_style_bg_color(c.box, on ? tok.surface_sel : tok.surface, 0);
    }
    flash_until_ = lv_tick_get() + kFlashMs;
}

void HidRemoteScreen::unflash_all() {
    flash(-1);
    flash_until_ = 0;
}

void HidRemoteScreen::build_mode_ui() {
    const auto tok = tokens();
    const lv_font_t* f10 = assets().load_standard_font(10);
    const lv_font_t* f12 = assets().load_standard_font(12);
    auto hint = [&](const char* text) {
        mode_hint_ = lv_label_create(body_);
        lv_label_set_text(mode_hint_, text);
        lv_obj_set_style_text_font(mode_hint_, f10 ? f10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mode_hint_, tok.ink_dim, 0);
        lv_obj_set_width(mode_hint_, view::kScreenWidth - 12);
        lv_label_set_long_mode(mode_hint_, LV_LABEL_LONG_CLIP);
        lv_obj_align(mode_hint_, LV_ALIGN_BOTTOM_LEFT, 6, -1);
    };
    auto state_label = [&](int x, int y) {
        mode_state_ = lv_label_create(body_);
        lv_obj_set_style_text_font(mode_state_, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_pos(mode_state_, x, y);
    };
    auto text_block = [&](int x, int y, int w) {
        mode_text_ = lv_label_create(body_);
        lv_obj_set_style_text_font(mode_text_, f10 ? f10 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mode_text_, tok.ink, 0);
        lv_obj_set_width(mode_text_, w);
        lv_label_set_long_mode(mode_text_, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(mode_text_, x, y);
    };

    // D-pad helper: 3x3 grid, corners empty. ids 0 up, 1 down, 2 left, 3 right, 4 centre.
    auto dpad = [&](int ox, int oy, int cw, int ch, const char* const labels[5]) {
        constexpr int g = 2;
        make_cell(0, ox + cw + g, oy, cw, ch, labels[0], nullptr);
        make_cell(1, ox + cw + g, oy + 2 * (ch + g), cw, ch, labels[1], nullptr);
        make_cell(2, ox, oy + ch + g, cw, ch, labels[2], nullptr);
        make_cell(3, ox + 2 * (cw + g), oy + ch + g, cw, ch, labels[3], nullptr);
        if (labels[4]) make_cell(4, ox + cw + g, oy + ch + g, cw, ch, labels[4], nullptr);
    };

    switch (mode_) {
    case Mode::Presenter:
    case Mode::PresenterVertical: {
        const bool v = mode_ == Mode::PresenterVertical;
        // Vertical: device turned 90° clockwise, so the keys rotate with it.
        static const char* const kH[5] = {"F  Up", "X  Down", "Z  Left", "C  Right", "P"};
        static const char* const kV[5] = {"C  Up", "Z  Down", "F  Left", "X  Right", "P"};
        dpad(6, 2, 52, 22, v ? kV : kH);
        const int x0 = 172, w = 70, h = 22;
        make_cell(5, x0, 2, w, h, "B", "PgUp");
        make_cell(6, x0 + w + 2, 2, w, h, "N", "PgDn");
        make_cell(7, x0, 26, w, h, "H", "Home");
        make_cell(8, x0 + w + 2, 26, w, h, "E", "End");
        make_cell(9, x0, 50, w, h, "Spc", "Space");
        make_cell(10, x0 + w + 2, 50, w, h, "5", "F5");
        hint("Enter next  Bksp prev  P play  7 Esc  Esc back");
        break;
    }
    case Mode::Keyboard: {
        auto* panel = lv_obj_create(body_);
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, view::kScreenWidth - 12, kBodyH - 16);
        lv_obj_set_pos(panel, 6, 1);
        lv_obj_set_style_radius(panel, 5, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(panel, tok.surface, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_border_color(panel, tok.rule, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        mode_text_ = lv_label_create(panel);
        lv_obj_set_style_text_font(mode_text_, f12 ? f12 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mode_text_, tok.accent, 0);
        lv_obj_set_width(mode_text_, view::kScreenWidth - 24);
        lv_label_set_long_mode(mode_text_, LV_LABEL_LONG_WRAP);
        lv_obj_align(mode_text_, LV_ALIGN_BOTTOM_LEFT, 4, -2);
        hint(settings_.alt_is_modifier ? "Ctrl/Alt chords  hold Tab: shortcuts  Fn+Esc: back"
                                       : "Ctrl chords   hold Tab: shortcuts   Fn+Esc: back");
        break;
    }
    case Mode::Media:
    case Mode::AppleMusic:
    case Mode::Movie: {
        struct C {
            int id;
            const char* k;
            const char* d;
        };
        static const C cells[] = {
            {13, "SPC", "Play"}, {0, "F", "Vol+"},  {1, "X", "Vol-"},  {2, "M", "Mute"},
            {3, "Z", "Prev"},    {4, "C", "Next"},  {5, "S", "Stop"},  {6, "K", "Mic"},
            {7, "V", "Mix"},     {8, "A", "Act"},   {9, "D", "Disp"}, {10, "W", "Desk"},
            {11, "B", "-5s"},    {12, "N", "+5s"},  {14, "O", "F11"},
        };
        const int cols = 4, gap = 2, w = (view::kScreenWidth - 12 - gap * (cols - 1)) / cols, h = 19;
        for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); ++i) {
            make_cell(cells[i].id, 6 + static_cast<int>(i % cols) * (w + gap), 1 + static_cast<int>(i / cols) * (h + gap),
                      w, h, cells[i].k, cells[i].d);
        }
        hint("Fn media keys go to the host too   Esc back");
        break;
    }
    case Mode::Mouse: {
        static const char* const kLabels[5] = {"F", "X", "Z", "C", nullptr};
        dpad(6, 2, 40, 22, kLabels);
        const int x0 = 138, w = 57, h = 30;
        make_cell(5, x0, 2, w, h, "L", "Left");
        make_cell(6, x0 + w + 2, 2, w, h, "M", "Middle");
        make_cell(7, x0 + 2 * (w + 2), 2, w, h, "R", "Right");
        make_cell(8, x0, 36, w, h, "W", "Wheel up");
        make_cell(9, x0 + w + 2, 36, w, h, "S", "Wheel dn");
        state_label(x0 + 2 * (w + 2) + 4, 44);
        hint("Enter click  1/2 speed  hold an arrow to go faster");
        break;
    }
    case Mode::Shorts: {
        const int w = (view::kScreenWidth - 12 - 4) / 3, h = 34;
        make_cell(0, 6, 4, w, h, key_name(settings_.shorts_up, "F").c_str(), "Up");
        make_cell(2, 6 + w + 2, 4, w, h, "SPC", "Play");
        make_cell(1, 6 + 2 * (w + 2), 4, w, h, key_name(settings_.shorts_down, "X").c_str(), "Down");
        make_cell(3, 6, 44, view::kScreenWidth - 12, 22, "Enter", "Remap up/down keys");
        state_label(6, 70);
        hint("Esc back");
        break;
    }
    case Mode::Clicker:
    case Mode::Jiggler:
    case Mode::StealthJiggler: {
        text_block(8, 2, view::kScreenWidth - 16);
        state_label(8, 64);
        if (mode_ == Mode::Clicker) hint("Space start/stop  Z/C delay  B button  Esc back");
        else if (mode_ == Mode::Jiggler) hint("Space start/stop  Z/C interval  F/X amount  D dir  Esc back");
        else hint("Space start/stop  Z/C min  U/I max  F/X amount  D dir  Esc back");
        break;
    }
    case Mode::PushToTalk: {
        state_label(0, 18);
        text_block(8, 52, view::kScreenWidth - 16);
        hint("Hold Space to talk  Enter latch  P preset  Esc back");
        break;
    }
    default: break;
    }
    refresh_mode_labels();
}

void HidRemoteScreen::refresh_mode_labels() {
    if (phase_ != Phase::Mode) return;
    const auto tok = tokens();
    char buf[200];
    switch (mode_) {
    case Mode::Keyboard:
        if (mode_text_) lv_label_set_text(mode_text_, mirror_.empty() ? "_" : (mirror_ + "_").c_str());
        break;
    case Mode::Mouse:
        if (mode_state_) {
            std::snprintf(buf, sizeof(buf), "Speed %d", settings_.mouse_sensitivity);
            lv_label_set_text(mode_state_, buf);
            lv_obj_set_style_text_color(mode_state_, tok.ink_dim, 0);
        }
        break;
    case Mode::Shorts:
        if (mode_state_) {
            const char* p = shorts_remap_ == 1 ? "Press the key for UP"
                            : shorts_remap_ == 2 ? "Press the key for DOWN"
                                                 : "";
            lv_label_set_text(mode_state_, p);
            lv_obj_set_style_text_color(mode_state_, tok.accent, 0);
        }
        break;
    case Mode::Clicker:
        if (mode_text_) {
            std::snprintf(buf, sizeof(buf), "Delay      %d ms\nButton     %s\nClicks     %lu",
                          settings_.clicker_delay, clicker_button_label(settings_.clicker_button), clicks_);
            lv_label_set_text(mode_text_, buf);
        }
        break;
    case Mode::Jiggler:
    case Mode::StealthJiggler:
        if (mode_text_) {
            const uint32_t now = lv_tick_get();
            const int next_s = running_ && next_action_ms_ > now ? static_cast<int>((next_action_ms_ - now) / 1000) : 0;
            if (mode_ == Mode::Jiggler) {
                std::snprintf(buf, sizeof(buf), "Interval   %d s\nAmount     %d px\nDirection  %s\nMoves %lu   next in %d s",
                              settings_.jiggler_interval, settings_.jiggler_amount,
                              jiggler_dir_label(settings_.jiggler_dir), moves_, next_s);
            } else {
                std::snprintf(buf, sizeof(buf), "Random     %d - %d s\nAmount     %d px\nDirection  %s\nMoves %lu   next in %d s",
                              settings_.stealth_min, settings_.stealth_max, settings_.jiggler_amount,
                              jiggler_dir_label(settings_.jiggler_dir), moves_, next_s);
            }
            lv_label_set_text(mode_text_, buf);
        }
        break;
    case Mode::PushToTalk:
        if (mode_text_) {
            std::snprintf(buf, sizeof(buf), "Key: %s%s", ptt_preset(settings_.ptt_preset).label,
                          ptt_latched_ ? "\nLatched (Enter to release)" : "");
            lv_label_set_text(mode_text_, buf);
        }
        break;
    default: break;
    }
    if (mode_state_ && (mode_ == Mode::Clicker || mode_ == Mode::Jiggler || mode_ == Mode::StealthJiggler)) {
        const char* t = mode_ == Mode::Clicker ? (running_ ? "RUNNING" : "STOPPED") : (running_ ? "ACTIVE" : "PAUSED");
        lv_label_set_text(mode_state_, t);
        lv_obj_set_style_text_color(mode_state_, running_ ? tok.accent : tok.ink_dim, 0);
    }
    if (mode_state_ && mode_ == Mode::PushToTalk) {
        lv_label_set_text(mode_state_, talking_ ? "TALKING" : "MUTED");
        lv_obj_set_width(mode_state_, view::kScreenWidth);
        lv_obj_set_style_text_align(mode_state_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(mode_state_, talking_ ? tok.accent : lv_color_hex(0xE05050), 0);
    }
}

void HidRemoteScreen::mirror_append(const std::string& s) {
    if (s == "\b") {
        if (!mirror_.empty()) mirror_.pop_back();
    } else {
        mirror_ += s;
    }
    if (mirror_.size() > 240) mirror_.erase(0, mirror_.size() - 240);
}

void HidRemoteScreen::send_arrow(uint8_t usage) {
    if (session_) session_->tap(0, usage, 25);
}

bool HidRemoteScreen::forward_media(uint32_t key) {
    const uint16_t u = media_usage(key);
    if (u == 0 || !session_) return false;
    session_->consumer(u);
    return true;
}

// Keyboard mode: the resolved Zero key (Shift / Sym / Fn layers applied) plus the
// Ctrl / Alt state captured at press time becomes one host keystroke.
void HidRemoteScreen::keyboard_send(uint32_t key, const platform::KeyMods& m) {
    if (!session_) return;
    if (forward_media(key)) {
        mirror_append("[" + std::string(platform::describe_key(key)) + "]");
        return;
    }
    uint8_t mods = 0;
    if (m.ctrl) mods |= kModCtrl;
    if (m.alt) mods |= kModAlt;

    Stroke s;
    bool have = false;
    const char base = m.base;
    // A Ctrl/Alt chord targets the keycap, not its Sym punctuation. The desktop
    // keypad also turns Ctrl+letter into a control code; the keycap fixes that.
    const bool base_alnum = (base >= 'a' && base <= 'z') || (base >= '0' && base <= '9');
    if (mods && base_alnum && !(m.fn && special_usage(key) != 0)) {
        s.usage = base_usage(base);
        s.mods = static_cast<uint8_t>(mods | (m.shift ? kModShift : 0));
        have = true;
    } else if (const uint8_t u = special_usage(key)) {
        s.usage = u;
        s.mods = mods;
        have = true;
    } else if (key < 128 && ascii_stroke(static_cast<char>(key), s)) {
        s.mods |= mods;
        have = true;
    }
    if (!have) return;

    const bool fkey = s.usage >= key::F1 && s.usage <= key::function(12);
    session_->tap(s.mods, s.usage, fkey ? 160 : 25);

    if (s.mods & (kModCtrl | kModAlt | kModGui)) mirror_append(stroke_tag(s));
    else if (s.usage == key::Backspace) mirror_append("\b");
    else if (s.usage == key::Enter) mirror_append("\n");
    else if (key >= 32 && key <= 126) mirror_append(std::string(1, static_cast<char>(key)));
    else mirror_append(stroke_tag(s));
}

void HidRemoteScreen::mouse_arrow(uint32_t key) {
    const uint32_t now = lv_tick_get();
    if (key == last_arrow_key_ && now - last_arrow_ms_ < 180) arrow_streak_ = std::min(arrow_streak_ + 1, 24);
    else arrow_streak_ = 0;
    last_arrow_key_ = key;
    last_arrow_ms_ = now;
    // Hold to accelerate: x1 for the first taps, up to x5 after ~1.3 s of repeat.
    const int step = settings_.mouse_sensitivity * 2 * (1 + arrow_streak_ / 6);
    int dx = 0, dy = 0, id = 0;
    if (key == LV_KEY_UP) dy = -step, id = 0;
    else if (key == LV_KEY_DOWN) dy = step, id = 1;
    else if (key == LV_KEY_LEFT) dx = -step, id = 2;
    else dx = step, id = 3;
    session_->mouse_move(dx, dy);
    flash(id);
}

void HidRemoteScreen::ptt_set(bool talking) {
    if (talking == talking_) return;
    talking_ = talking;
    if (session_) {
        const auto& p = ptt_preset(settings_.ptt_preset);
        if (talking) session_->key_down(p.mods, p.usage);
        else session_->key_up();
    }
    refresh_mode_labels();
}

void HidRemoteScreen::jiggler_schedule() {
    const uint32_t now = lv_tick_get();
    if (mode_ == Mode::StealthJiggler) {
        const int lo = settings_.stealth_min;
        const int hi = std::max(lo, settings_.stealth_max);
        std::uniform_int_distribution<int> d(lo, hi);
        next_action_ms_ = now + static_cast<uint32_t>(d(rng_)) * 1000u;
    } else {
        next_action_ms_ = now + static_cast<uint32_t>(std::max(5, settings_.jiggler_interval)) * 1000u;
    }
}

void HidRemoteScreen::tick_mode(uint32_t now) {
    if (!session_ || !running_) return;
    if (mode_ == Mode::Clicker) {
        // Catch up at most a few clicks per tick if the loop stalled.
        int burst = 0;
        while (static_cast<int32_t>(now - next_action_ms_) >= 0 && burst++ < 4) {
            session_->mouse_click(clicker_button_mask(settings_.clicker_button));
            ++clicks_;
            next_action_ms_ += static_cast<uint32_t>(settings_.clicker_delay);
        }
        if (burst) refresh_mode_labels();
        if (static_cast<int32_t>(now - next_action_ms_) > 1000) next_action_ms_ = now;
        return;
    }
    if (mode_ == Mode::Jiggler || mode_ == Mode::StealthJiggler) {
        if (static_cast<int32_t>(now - next_action_ms_) < 0) return;
        const bool stealth = mode_ == Mode::StealthJiggler;
        int8_t dx = 0, dy = 0;
        jiggler_delta(settings_.jiggler_amount, settings_.jiggler_dir, stealth,
                      [this](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }, dx, dy);
        session_->mouse_move(dx, dy);
        if (!stealth) session_->mouse_move(-dx, -dy);
        ++moves_;
        jiggler_schedule();
        refresh_mode_labels();
    }
}

// --- key routing --------------------------------------------------------------------

bool HidRemoteScreen::on_early_key(uint32_t key, const char*, bool long_pressed, void* user_data) {
    auto* self = static_cast<HidRemoteScreen*>(user_data);
    return self ? self->early_key(key, long_pressed) : false;
}

void HidRemoteScreen::on_key_up(uint32_t key, const char*, void* user_data) {
    if (auto* self = static_cast<HidRemoteScreen*>(user_data)) self->key_up(key);
}

bool HidRemoteScreen::early_key(uint32_t key, bool long_pressed) {
    if (device_key(key)) return false;
    const bool keyboard = phase_ == Phase::Mode && mode_ == Mode::Keyboard;

    // After a hold fired, keep swallowing that key until it is really let go:
    // a held key can keep arriving as press/release pairs, and those must not
    // leak into the menu (an Esc there would leave the tool).
    if (key == LV_KEY_ESC && esc_.fired) {
        if (!long_pressed) hold_press(esc_);
        return true;
    }
    if (key == platform::kKeyTab && tab_.fired) {
        if (!long_pressed) hold_press(tab_);
        return true;
    }

    if (keyboard) {
        // Esc / Tab: a tap goes to the host once the key is released; holding
        // ~0.7 s leaves the mode (Esc) or opens System Shortcuts (Tab).
        if (key == LV_KEY_ESC) {
            if (!long_pressed && platform::last_key_mods().fn) {
                // Fn+Esc: immediate, timing-free way back.
                esc_ = {};
                esc_.fired = true;
                esc_.down = true;
                esc_.start = lv_tick_get();
                leave_mode();
                return true;
            }
            if (long_pressed) fire_hold(esc_, true);
            else hold_press(esc_);
            return true;
        }
        if (key == platform::kKeyTab) {
            if (long_pressed) fire_hold(tab_, false);
            else hold_press(tab_);
            return true;
        }
        if (long_pressed) return true; // no duplicate on the 700 ms hold event
        keyboard_send(key, platform::last_key_mods());
        refresh_mode_labels();
        return true;
    }

    // Hold events: a slot key opens host options; everything else is swallowed so a
    // held key does not fire twice. Esc stays with the app (3 s hold = quit).
    if (long_pressed) {
        if (key == LV_KEY_ESC) return false;
        on_hold(key);
        return true;
    }
    // In a running mode the Fn media keys drive the host, not the Zero.
    if (phase_ == Phase::Mode && media_usage(key) != 0) {
        forward_media(key);
        flash(-1);
        return true;
    }
    return false;
}

void HidRemoteScreen::key_up(uint32_t key) {
    if (key == LV_KEY_ESC && (esc_.down || esc_.fired)) {
        hold_release(esc_);
        return;
    }
    if (key == platform::kKeyTab && (tab_.down || tab_.fired)) {
        hold_release(tab_);
        return;
    }
    if (phase_ == Phase::Mode && mode_ == Mode::Keyboard) return;
    if (phase_ == Phase::Hosts && pending_slot_key_ != 0) {
        const uint32_t k = pending_slot_key_;
        pending_slot_key_ = 0;
        if (k == key) select_slot(static_cast<int>(k - '0'));
        return;
    }
    // Any release ends a held talk: the router only reports the release of the
    // most recent key, so a second key pressed mid-hold would otherwise stick.
    if (phase_ == Phase::Mode && mode_ == Mode::PushToTalk && talking_ && !ptt_latched_) {
        ptt_set(false);
    }
}

// --- Esc / Tab tap-vs-hold in Keyboard mode ---------------------------------------
// Press/release pairs closer than kRepeatGapMs count as one continuous hold, so it
// works whether the keyboard reports a held key once or as a stream of repeats.
namespace {
constexpr uint32_t kHoldMs = 700;
constexpr uint32_t kRepeatGapMs = 250;
} // namespace

void HidRemoteScreen::hold_press(HoldKey& h) {
    const uint32_t now = lv_tick_get();
    if (!h.down && (h.start == 0 || now - h.last_release > kRepeatGapMs)) {
        if (!h.fired) {
            h.start = now;
            h.mods = platform::last_key_mods();
        }
    }
    h.down = true;
    h.tap_due = 0;
}

void HidRemoteScreen::hold_release(HoldKey& h) {
    const uint32_t now = lv_tick_get();
    h.down = false;
    h.last_release = now;
    // Send the tap only once no repeat follows; a hold that fired sends nothing.
    if (!h.fired && h.start != 0) h.tap_due = now + kRepeatGapMs;
}

void HidRemoteScreen::fire_hold(HoldKey& h, bool is_esc) {
    if (h.fired) return;
    h.fired = true;
    h.tap_due = 0;
    if (is_esc) {
        leave_mode();
    } else {
        shortcuts_back_ = Phase::Mode;
        set_phase(Phase::Shortcuts);
    }
}

void HidRemoteScreen::tick_hold(HoldKey& h, bool is_esc, uint32_t now) {
    if (h.start == 0) return;
    const bool holding = h.down || now - h.last_release <= kRepeatGapMs;
    if (!h.fired && holding && now - h.start >= kHoldMs &&
        phase_ == Phase::Mode && mode_ == Mode::Keyboard) {
        fire_hold(h, is_esc);
        return;
    }
    if (h.tap_due && !h.down && static_cast<int32_t>(now - h.tap_due) >= 0) {
        h.tap_due = 0;
        if (!h.fired && session_ && phase_ == Phase::Mode && mode_ == Mode::Keyboard) {
            Stroke s;
            s.usage = is_esc ? key::Esc : key::Tab;
            s.mods = static_cast<uint8_t>((h.mods.ctrl ? kModCtrl : 0) | (h.mods.alt ? kModAlt : 0) |
                                          (h.mods.shift ? kModShift : 0));
            session_->tap(s.mods, s.usage, 25);
            mirror_append(s.mods ? stroke_tag(s) : (is_esc ? "[Esc]" : "[Tab]"));
            refresh_mode_labels();
        }
        h = {};
        return;
    }
    // Fully released after a hold: ready for the next press.
    if (h.fired && !h.down && now - h.last_release > kRepeatGapMs) h = {};
}

void HidRemoteScreen::on_hold(uint32_t key) {
    if (phase_ == Phase::Hosts && key >= '1' && key < '1' + kHostSlotCount) {
        const int slot = static_cast<int>(key - '0');
        pending_slot_key_ = 0;
        if (!settings_.host_slots[slot - 1].empty()) {
            host_menu_slot_ = slot;
            sel_by_phase_[static_cast<int>(Phase::HostMenu)] = 1;
            set_phase(Phase::HostMenu);
        }
    }
}

bool HidRemoteScreen::handle_key(uint32_t key) {
    switch (phase_) {
    case Phase::Edit: return handle_edit_key(key);
    case Phase::Mode: return handle_mode_key(key);
    case Phase::Hosts:
        if (key == LV_KEY_ESC) {
            if (pairing_slot_ || waiting_slot_) {
                session_->transport().set_pairing(false);
                session_->transport().set_selected_host(settings_.preferred_host);
                pairing_slot_ = waiting_slot_ = 0;
                show_message("Cancelled");
                rebuild_body();
                return true;
            }
            stop_transport();
            set_phase(Phase::Transport);
            return true;
        }
        if (key >= '1' && key < '1' + kHostSlotCount) {
            // Act on release: a hold opens options instead (see on_hold).
            pending_slot_key_ = key;
            return true;
        }
        if (is_enter(key) || key == ' ') {
            hosts_enter();
            return true;
        }
        if (key == 's' || key == 'S') {
            set_phase(Phase::Settings);
            return true;
        }
        return true;
    case Phase::UsbWait:
        if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
            stop_transport();
            set_phase(Phase::Transport);
            return true;
        }
        if (is_enter(key) || key == ' ') {
            if (session_) set_phase(Phase::Menu);
            return true;
        }
        return key < '4' || key > '8';
    default: break;
    }

    // List phases.
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
        switch (phase_) {
        case Phase::Transport: view_model().go_back(); break;
        case Phase::UsbWait:
            stop_transport();
            set_phase(Phase::Transport);
            break;
        case Phase::Menu: view_model().go_back(); break;
        case Phase::Settings: set_phase(session_ ? Phase::Menu : Phase::Transport); break;
        case Phase::HostMenu: set_phase(Phase::Hosts); break;
        case Phase::Shortcuts: set_phase(shortcuts_back_ == Phase::Mode ? Phase::Mode : Phase::Menu); break;
        default: break;
        }
        return true;
    }
    if (key == LV_KEY_UP) {
        list_move(-1);
        return true;
    }
    if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
        list_move(1);
        return true;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
        if (phase_ == Phase::Settings) list_adjust(key == LV_KEY_RIGHT ? 1 : -1);
        return true;
    }
    if (is_enter(key) || key == ' ') {
        list_enter();
        return true;
    }
    return false; // 4-8 fall through to the action bar
}

bool HidRemoteScreen::handle_mode_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        if (mode_ == Mode::Shorts && shorts_remap_) {
            shorts_remap_ = 0;
            refresh_mode_labels();
            return true;
        }
        leave_mode();
        return true;
    }
    if (!session_) return true;
    switch (mode_) {
    case Mode::Presenter:
    case Mode::PresenterVertical: return handle_presenter_key(key);
    case Mode::Media:
    case Mode::AppleMusic:
    case Mode::Movie: return handle_media_key(key);
    case Mode::Mouse: return handle_mouse_key(key);
    case Mode::Shorts: return handle_shorts_key(key);
    case Mode::Clicker: return handle_clicker_key(key);
    case Mode::Jiggler:
    case Mode::StealthJiggler: return handle_jiggler_key(key);
    case Mode::PushToTalk: return handle_ptt_key(key);
    default: return true;
    }
}

bool HidRemoteScreen::handle_presenter_key(uint32_t key) {
    if (is_arrow(key)) {
        uint32_t k = key;
        if (mode_ == Mode::PresenterVertical) {
            // Rotated 90° clockwise: right→Up, left→Down, up→Left, down→Right.
            if (key == LV_KEY_RIGHT) k = LV_KEY_UP;
            else if (key == LV_KEY_LEFT) k = LV_KEY_DOWN;
            else if (key == LV_KEY_UP) k = LV_KEY_LEFT;
            else k = LV_KEY_RIGHT;
        }
        send_arrow(arrow_usage(k));
        flash(k == LV_KEY_UP ? 0 : k == LV_KEY_DOWN ? 1 : k == LV_KEY_LEFT ? 2 : 3);
        return true;
    }
    if (is_enter(key)) {
        send_arrow(key::Down);
        flash(1);
        return true;
    }
    if (key == LV_KEY_BACKSPACE) {
        send_arrow(key::Up);
        flash(0);
        return true;
    }
    if (key == ' ') {
        session_->tap(0, key::Space);
        flash(9);
        return true;
    }
    if (key == '5' || key == platform::kKeyF5) {
        session_->tap_slow(0, key::function(5));
        flash(10);
        return true;
    }
    switch (key) {
    case 'b': case 'B': session_->tap(0, key::PageUp); flash(5); return true;
    case 'n': case 'N': session_->tap(0, key::PageDown); flash(6); return true;
    case 'h': case 'H': session_->tap(0, key::Home); flash(7); return true;
    case 'e': case 'E': session_->tap(0, key::End); flash(8); return true;
    case 'p': case 'P': session_->consumer(consumer::PlayPause); flash(4); return true;
    default: break;
    }
    return key != '4' && key != '6' && key != '7' && key != '8'; // bar keys → nav_actions
}

bool HidRemoteScreen::handle_media_key(uint32_t key) {
    auto c = [this](uint16_t u, int id) {
        session_->consumer(u);
        flash(id);
        return true;
    };
    auto k = [this](uint8_t mods, uint8_t usage, int id, int hold = 30) {
        session_->tap(mods, usage, hold);
        flash(id);
        return true;
    };
    switch (key) {
    case ' ': return c(consumer::PlayPause, 13);
    case LV_KEY_UP: return c(consumer::VolumeUp, 0);
    case LV_KEY_DOWN: return c(consumer::VolumeDown, 1);
    case LV_KEY_LEFT: return c(consumer::PrevTrack, 3);
    case LV_KEY_RIGHT: return c(consumer::NextTrack, 4);
    case 'm': case 'M': return c(consumer::Mute, 2);
    case 's': case 'S': return c(consumer::Stop, 5);
    case 'k': case 'K': return k(kModCtrl | kModShift, base_usage('m'), 6);
    case 'v': case 'V': return k(kModCtrl | kModGui, base_usage('v'), 7);
    case 'a': case 'A': return k(kModGui, base_usage('a'), 8);
    case 'd': case 'D': return k(kModGui, base_usage('p'), 9);
    case 'w': case 'W': return k(kModGui, base_usage('d'), 10);
    case 'b': case 'B': return k(0, key::Left, 11);
    case 'n': case 'N': return k(0, key::Right, 12);
    case 'o': case 'O':
    case platform::kKeyF11: return k(0, key::function(11), 14, 160);
    default: break;
    }
    return key != '4' && key != '5' && key != '6' && key != '7' && key != '8';
}

bool HidRemoteScreen::handle_mouse_key(uint32_t key) {
    if (is_arrow(key)) {
        mouse_arrow(key);
        return true;
    }
    switch (key) {
    case LV_KEY_ENTER:
    case ' ':
    case 'l':
    case 'L': session_->mouse_click(kMouseLeft); flash(5); return true;
    case 'r':
    case 'R': session_->mouse_click(kMouseRight); flash(7); return true;
    case 'm':
    case 'M': session_->mouse_click(kMouseMiddle); flash(6); return true;
    case 'w':
    case 'W': session_->mouse_move(0, 0, 1); flash(8); return true;
    case 's':
    case 'S': session_->mouse_move(0, 0, -1); flash(9); return true;
    case '1':
    case '2':
        settings_.mouse_sensitivity = clampi(settings_.mouse_sensitivity + (key == '2' ? 1 : -1), 1, 10);
        settings_.save();
        refresh_mode_labels();
        return true;
    default: break;
    }
    return key != '4' && key != '5' && key != '6' && key != '7' && key != '8';
}

bool HidRemoteScreen::handle_shorts_key(uint32_t key) {
    if (shorts_remap_) {
        // Enter, Esc and the 4-8 bar cannot be bound.
        if (is_enter(key) || (key >= '4' && key <= '8')) return true;
        const uint32_t k = is_arrow(key) && key == LV_KEY_UP ? 0u : key;
        if (shorts_remap_ == 1) {
            shorts_pending_up_ = k;
            shorts_remap_ = 2;
        } else {
            settings_.shorts_up = shorts_pending_up_;
            settings_.shorts_down = (key == LV_KEY_DOWN) ? 0u : key;
            settings_.save();
            shorts_remap_ = 0;
            rebuild_body();
            return true;
        }
        refresh_mode_labels();
        return true;
    }
    if (is_enter(key)) {
        shorts_remap_ = 1;
        flash(3);
        refresh_mode_labels();
        return true;
    }
    const uint32_t up = settings_.shorts_up ? settings_.shorts_up : LV_KEY_UP;
    const uint32_t down = settings_.shorts_down ? settings_.shorts_down : LV_KEY_DOWN;
    if (key == down) {
        send_arrow(key::Down);
        flash(1);
        return true;
    }
    if (key == up) {
        send_arrow(key::Up);
        flash(0);
        return true;
    }
    if (key == ' ') {
        session_->tap(0, key::Space);
        flash(2);
        return true;
    }
    return key != '5' && key != '6' && key != '7';
}

bool HidRemoteScreen::handle_clicker_key(uint32_t key) {
    if (key == ' ' || is_enter(key)) {
        running_ = !running_;
        next_action_ms_ = lv_tick_get();
    } else if (key == LV_KEY_LEFT || key == '-') {
        settings_.clicker_delay = clampi(settings_.clicker_delay - 50, 10, 10000);
        settings_.save();
    } else if (key == LV_KEY_RIGHT || key == '=' || key == '+') {
        settings_.clicker_delay = clampi(settings_.clicker_delay + 50, 10, 10000);
        settings_.save();
    } else if (key == 'b' || key == 'B') {
        settings_.clicker_button = (settings_.clicker_button + 1) % 3;
        settings_.save();
    } else {
        return key != '4' && key != '5' && key != '6' && key != '8';
    }
    refresh_mode_labels();
    return true;
}

bool HidRemoteScreen::handle_jiggler_key(uint32_t key) {
    const bool stealth = mode_ == Mode::StealthJiggler;
    if (key == ' ' || is_enter(key)) {
        running_ = !running_;
        next_action_ms_ = lv_tick_get(); // first move right away
    } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
        const int dir = key == LV_KEY_RIGHT ? 1 : -1;
        // ESP32 build bumped Max on '=' in stealth mode; here Z/C always step Min.
        if (stealth) settings_.stealth_min = clampi(settings_.stealth_min + dir * 5, 10, 890);
        else settings_.jiggler_interval = clampi(settings_.jiggler_interval + dir * 5, 5, 600);
        settings_.clamp();
        settings_.save();
        if (running_) jiggler_schedule();
    } else if (stealth && (key == 'u' || key == 'U' || key == 'i' || key == 'I')) {
        const int dir = (key == 'i' || key == 'I') ? 1 : -1;
        settings_.stealth_max = clampi(settings_.stealth_max + dir * 10, settings_.stealth_min + 10, 900);
        settings_.save();
        if (running_) jiggler_schedule();
    } else if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
        settings_.jiggler_amount = clampi(settings_.jiggler_amount + (key == LV_KEY_UP ? 1 : -1), 1, 50);
        settings_.save();
    } else if (key == 'd' || key == 'D') {
        settings_.jiggler_dir = (settings_.jiggler_dir + 1) % 3;
        settings_.save();
    } else {
        return key != '4' && key != '5' && key != '6' && key != '7' && key != '8';
    }
    refresh_mode_labels();
    return true;
}

bool HidRemoteScreen::handle_ptt_key(uint32_t key) {
    if (key == ' ') {
        if (!ptt_latched_) ptt_set(true);
        return true;
    }
    if (is_enter(key)) {
        ptt_latched_ = !talking_;
        ptt_set(!talking_);
        return true;
    }
    if (key == 'p' || key == 'P') {
        if (talking_) return true; // never swap keys while one is held on the host
        settings_.ptt_preset = (settings_.ptt_preset + 1) % kPttPresetCount;
        settings_.save();
        refresh_mode_labels();
        return true;
    }
    return key != '6';
}

// --- action bar & timer -------------------------------------------------------------

const char* HidRemoteScreen::typing_hint() const {
    if (phase_ == Phase::Edit) {
        return edit_secret_ ? "Password  Enter confirm  Esc cancel" : "Type a name  Enter save  Esc cancel";
    }
    if (phase_ == Phase::Hosts) return "1-6 host slot  |  Enter continue";
    if (phase_ == Phase::Mode && mode_ == Mode::Keyboard) return "Typing to the host  |  Fn+Esc or hold Esc: back";
    return nullptr;
}

std::array<view::widgets::ActionSlot, 5> HidRemoteScreen::nav_actions() {
    using view::widgets::ActionSlot;
    if (phase_ == Phase::Edit || (phase_ == Phase::Mode && mode_ == Mode::Keyboard)) return {};
    if (phase_ == Phase::Hosts) return {}; // digits pick slots
    if (phase_ == Phase::UsbWait) {
        return {{
            {view::ICON_ARROW_BACK, [this]() { handle_key(LV_KEY_ESC); }},
            {nullptr, nullptr},
            {view::ICON_CHECK, [this]() { handle_key(LV_KEY_ENTER); }},
            {nullptr, nullptr},
            {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
        }};
    }
    if (phase_ == Phase::Mode) {
        auto tap = [this](uint8_t mods, uint8_t usage, int id) {
            return [this, mods, usage, id]() {
                if (session_) session_->tap(mods, usage, 25);
                flash(id);
            };
        };
        auto cons = [this](uint16_t u, int id) {
            return [this, u, id]() {
                if (session_) session_->consumer(u);
                flash(id);
            };
        };
        switch (mode_) {
        case Mode::Presenter:
        case Mode::PresenterVertical:
            return {{
                {view::ICON_ARROW_LEFT, tap(0, key::Left, 2)},
                {view::ICON_PLAY, [this]() {
                     if (session_) session_->tap_slow(0, key::function(5));
                     flash(10);
                 }},
                {view::ICON_CHECK, tap(0, key::Space, 9)},
                {view::ICON_STOP, tap(0, key::Esc, -1)},
                {view::ICON_ARROW_RIGHT, tap(0, key::Right, 3)},
            }};
        case Mode::Media:
        case Mode::AppleMusic:
        case Mode::Movie:
            return {{
                {view::ICON_MINUS, cons(consumer::VolumeDown, 1)},
                {view::ICON_SKIP_BACK, cons(consumer::PrevTrack, 3)},
                {view::ICON_PLAY, cons(consumer::PlayPause, 13)},
                {view::ICON_SKIP_FORWARD, cons(consumer::NextTrack, 4)},
                {view::ICON_PLUS, cons(consumer::VolumeUp, 0)},
            }};
        case Mode::Mouse:
            return {{
                {view::ICON_CARET_LEFT, [this]() {
                     if (session_) session_->mouse_click(kMouseLeft);
                     flash(5);
                 }},
                {view::ICON_CARET_UP, [this]() {
                     if (session_) session_->mouse_move(0, 0, 1);
                     flash(8);
                 }},
                {view::ICON_DOT, [this]() {
                     if (session_) session_->mouse_click(kMouseMiddle);
                     flash(6);
                 }},
                {view::ICON_CARET_DOWN, [this]() {
                     if (session_) session_->mouse_move(0, 0, -1);
                     flash(9);
                 }},
                {view::ICON_SQUARE_ARROW_RIGHT, [this]() {
                     if (session_) session_->mouse_click(kMouseRight);
                     flash(7);
                 }},
            }};
        case Mode::Shorts:
            return {{
                {nullptr, nullptr},
                {view::ICON_ARROW_UP, tap(0, key::Up, 0)},
                {view::ICON_PLAY, tap(0, key::Space, 2)},
                {view::ICON_ARROW_DOWN, tap(0, key::Down, 1)},
                {nullptr, nullptr},
            }};
        case Mode::Clicker:
        case Mode::Jiggler:
        case Mode::StealthJiggler:
            return {{
                {view::ICON_MINUS, [this]() { handle_mode_key(LV_KEY_LEFT); }},
                {mode_ == Mode::Clicker ? view::ICON_DOT : view::ICON_CARET_UP,
                 [this]() { handle_mode_key(mode_ == Mode::Clicker ? 'b' : LV_KEY_UP); }},
                {view::ICON_PLAY, [this]() { handle_mode_key(' '); }},
                {mode_ == Mode::Clicker ? nullptr : view::ICON_CARET_DOWN,
                 [this]() {
                     if (mode_ != Mode::Clicker) handle_mode_key(LV_KEY_DOWN);
                 }},
                {view::ICON_PLUS, [this]() { handle_mode_key(LV_KEY_RIGHT); }},
            }};
        case Mode::PushToTalk:
            return {{
                {nullptr, nullptr},
                {nullptr, nullptr},
                {view::ICON_CHECK, [this]() { handle_mode_key(LV_KEY_ENTER); }},
                {nullptr, nullptr},
                {nullptr, nullptr},
            }};
        default: return {};
        }
    }
    // List phases: standard 4 back / 5 up / 6 select / 7 down / 8 home.
    return {{
        {view::ICON_ARROW_BACK, [this]() { handle_key(LV_KEY_ESC); }},
        {view::ICON_CARET_UP, [this]() { list_move(-1); }},
        {view::ICON_CHECK, [this]() { list_enter(); }},
        {view::ICON_CARET_DOWN, [this]() { list_move(1); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

void HidRemoteScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<HidRemoteScreen*>(lv_timer_get_user_data(t));
    if (!self) return;
    const uint32_t now = lv_tick_get();
    if (self->flash_until_ && now >= self->flash_until_) self->unflash_all();
    if (self->phase_ == Phase::Mode) self->tick_mode(now);
    self->tick_hold(self->esc_, true, now);
    self->tick_hold(self->tab_, false, now);
    if (now - self->last_status_ms_ >= 300) {
        self->last_status_ms_ = now;
        if (self->phase_ == Phase::UsbWait) self->tick_usb_wait(now);
        self->poll_hosts();
        self->refresh_status();
        if (self->phase_ == Phase::Mode &&
            (self->mode_ == Mode::Jiggler || self->mode_ == Mode::StealthJiggler) && self->running_) {
            self->refresh_mode_labels();
        }
    }
}

} // namespace screen

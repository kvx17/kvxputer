#include "roku_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace screen {
namespace {

constexpr int kPickerCols = 3;
constexpr int kPickerVisibleRows = 2;

std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool looks_like_ipv4(const std::string& s) {
    int dots = 0;
    int part = -1;
    int n = 0;
    for (char c : s) {
        if (c == '.') {
            if (part < 0 || part > 255) return false;
            ++dots;
            part = -1;
            continue;
        }
        if (c < '0' || c > '9') return false;
        if (part < 0) {
            part = c - '0';
            ++n;
        } else {
            part = part * 10 + (c - '0');
            if (part > 255) return false;
        }
    }
    return dots == 3 && part >= 0 && part <= 255 && n >= 4;
}

lv_obj_t* make_dpad_btn(lv_obj_t* parent, const view::KvxTokens& tok, const lv_font_t* font, const char* text,
                        int x, int y, int w, int h) {
    auto* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, w, h);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_style_radius(cell, 6, 0);
    lv_obj_set_style_bg_color(cell, tok.surface, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, tok.rule, 0);
    lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
    auto* lbl = lv_label_create(cell);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, tok.ink, 0);
    lv_obj_center(lbl);
    return cell;
}

void style_dpad_flash(lv_obj_t* cell, bool on, const view::KvxTokens& tok) {
    if (!cell) return;
    lv_obj_set_style_border_color(cell, on ? tok.accent : tok.rule, 0);
    lv_obj_set_style_border_width(cell, on ? 2 : 1, 0);
    lv_obj_set_style_bg_color(cell, on ? tok.surface_sel : tok.surface, 0);
}

} // namespace

RokuScreen::RokuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    queue_.set_result_callback([this](const std::string& key, tools::roku::Status st) {
        on_cmd_result(key, st);
    });
    init();
}

RokuScreen::~RokuScreen() {
    cancel_scan_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    platform::clear_early_key_listener(on_early_key, this);
    platform::clear_screen_key_release_listener(on_key_up, this);
    platform::set_text_entry(false);
    queue_.release_held();
}

void RokuScreen::on_shown() {
    platform::set_early_key_listener(on_early_key, this);
    platform::set_screen_key_release_listener(on_key_up, this);
    if (phase_ == Phase::Picker && !scanning_ && devices_.empty()) {
        start_scan();
    }
    if (mode_ == InputMode::Keyboard) {
        platform::set_text_entry(true);
    }
}

void RokuScreen::build() {
    make_header(content(), "Roku");
    poll_timer_ = lv_timer_create(poll_timer_cb, 80, this);
    build_picker_ui();
    start_scan();
}

const char* RokuScreen::typing_hint() const {
    // Keep 4–8 action slots in both modes; text entry is toggled via set_text_entry.
    return nullptr;
}

void RokuScreen::poll_timer_cb(lv_timer_t* t) {
    auto* self = static_cast<RokuScreen*>(lv_timer_get_user_data(t));
    if (!self) return;

    {
        std::lock_guard<std::mutex> lock(self->scan_mu_);
        if (self->scan_done_) {
            self->scan_done_ = false;
            self->devices_ = std::move(self->scan_pending_);
            self->scan_pending_.clear();
            self->scanning_ = false;
            self->apply_scan_results();
        }
    }

    {
        std::lock_guard<std::mutex> lock(self->result_mu_);
        if (self->pending_status_ == "audio" || self->pending_status_ == "apps") {
            self->pending_status_.clear();
            if (self->overlay_ == Overlay::Audio || self->overlay_ == Overlay::Apps) {
                self->rebuild_overlay();
            }
        } else if (!self->pending_status_.empty()) {
            self->set_status(self->pending_status_);
            self->pending_status_.clear();
            if (self->phase_ == Phase::Remote) self->refresh_remote_labels();
        }
        if (self->pending_forbidden_ && !self->showed_forbidden_) {
            self->showed_forbidden_ = true;
            self->pending_forbidden_ = false;
            self->set_status("Enable Control by mobile apps");
        }
    }

    const uint32_t now = lv_tick_get();
    if (self->banner_until_ms_ && now >= self->banner_until_ms_) {
        self->clear_banner();
    }
    if (self->flash_until_ms_ && now >= self->flash_until_ms_) {
        self->flash_until_ms_ = 0;
        self->flash_cell_ = DpadCell::None;
        if (self->phase_ == Phase::Remote) {
            const auto tok = self->tokens();
            for (auto* c : self->dpad_cells_) style_dpad_flash(c, false, tok);
        }
    }
}

void RokuScreen::exit_roku() {
    esc_armed_ = false;
    esc_long_exit_ = false;
    platform::set_text_entry(false);
    mode_ = InputMode::RemoteKeys;
    queue_.release_held();
    held_ecp_.clear();
    close_overlay();
    view_model().go_back();
}

void RokuScreen::arm_esc_toggle() {
    esc_armed_ = true;
    esc_long_exit_ = false;
}

void RokuScreen::finish_esc_release() {
    if (!esc_armed_) return;
    const bool do_toggle = !esc_long_exit_;
    esc_armed_ = false;
    esc_long_exit_ = false;
    if (!do_toggle) return;
    if (phase_ != Phase::Remote || overlay_ != Overlay::None) return;
    if (mode_ == InputMode::Keyboard) {
        set_input_mode(InputMode::RemoteKeys, true);
    } else {
        set_input_mode(InputMode::Keyboard, true);
    }
}

bool RokuScreen::on_early_key(uint32_t key, const char* /*name*/, bool long_pressed, void* user_data) {
    auto* self = static_cast<RokuScreen*>(user_data);
    if (!self || key != LV_KEY_ESC) return false;

    // Picker: leave Esc to normal go_back (short or long).
    if (self->phase_ == Phase::Picker && self->overlay_ == Overlay::None) {
        return false;
    }

    if (long_pressed) {
        // Hold Esc exits the Roku tool from remote / overlays.
        if (self->esc_armed_ || self->phase_ == Phase::Remote || self->overlay_ != Overlay::None) {
            self->esc_long_exit_ = true;
            self->exit_roku();
            return true;
        }
        return false;
    }

    // Short Esc press: overlays close immediately; remote arms toggle on release.
    if (self->overlay_ != Overlay::None) {
        self->close_overlay();
        self->esc_armed_ = false;
        self->esc_long_exit_ = false;
        return true;
    }
    if (self->phase_ == Phase::Remote) {
        self->arm_esc_toggle();
        return true;
    }
    return false;
}

void RokuScreen::on_key_up(uint32_t key, const char* /*name*/, void* user_data) {
    auto* self = static_cast<RokuScreen*>(user_data);
    if (!self) return;
    if (key == LV_KEY_ESC) {
        self->finish_esc_release();
        return;
    }
    if (!self->held_ecp_.empty()) {
        self->hold_up(self->held_ecp_.c_str());
        self->held_ecp_.clear();
    }
}

void RokuScreen::on_cmd_result(const std::string& key, tools::roku::Status st) {
    std::lock_guard<std::mutex> lock(result_mu_);
    if (st == tools::roku::Status::Ok) {
        pending_status_ = key + " OK";
    } else if (st == tools::roku::Status::Forbidden) {
        pending_forbidden_ = true;
        pending_status_ = tools::roku::status_message(st);
    } else {
        pending_status_ = tools::roku::status_message(st);
    }
}

std::string RokuScreen::load_saved_ip() const {
    std::ifstream in(tools::config_file("roku_ip.txt"));
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    return trim_copy(line);
}

void RokuScreen::save_ip(const std::string& ip) {
    std::ofstream out(tools::config_file("roku_ip.txt"), std::ios::trunc);
    if (out) out << ip << "\n";
}

void RokuScreen::start_scan() {
    if (scanning_) return;
    cancel_scan_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
    cancel_scan_ = false;
    scanning_ = true;
    if (picker_status_) lv_label_set_text(picker_status_, "Scanning…");
    scan_thread_ = std::thread([this]() {
        auto found = tools::roku::ssdp_discover(2500, &cancel_scan_);
        std::vector<tools::roku::Device> enriched;
        for (auto& d : found) {
            if (cancel_scan_) break;
            tools::roku::Device info = d;
            if (tools::roku::query_device_info(d.ip, info) == tools::roku::Status::Ok) {
                enriched.push_back(std::move(info));
            } else {
                enriched.push_back(std::move(d));
            }
        }
        {
            std::lock_guard<std::mutex> lock(scan_mu_);
            scan_pending_ = std::move(enriched);
            scan_done_ = true;
        }
    });
}

void RokuScreen::apply_scan_results() {
    picker_items_.clear();
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
        picker_items_.push_back({PickerKind::Device, i});
    }
    picker_items_.push_back({PickerKind::Rescan, -1});
    picker_items_.push_back({PickerKind::EnterIp, -1});

    const std::string saved = load_saved_ip();
    picker_sel_ = 0;
    if (!saved.empty()) {
        for (size_t i = 0; i < picker_items_.size(); ++i) {
            if (picker_items_[i].kind == PickerKind::Device &&
                devices_[static_cast<size_t>(picker_items_[i].device_index)].ip == saved) {
                picker_sel_ = static_cast<int>(i);
                break;
            }
        }
    }
    picker_scroll_row_ = 0;
    rebuild_picker_tiles();
    refresh_picker_selection();
    if (picker_status_) {
        if (devices_.empty()) {
            lv_label_set_text(picker_status_, "No Roku on this network");
        } else {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%zu device%s", devices_.size(),
                          devices_.size() == 1 ? "" : "s");
            lv_label_set_text(picker_status_, buf);
        }
    }
}

void RokuScreen::build_picker_ui() {
    destroy_picker_ui();
    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;

    picker_root_ = lv_obj_create(content());
    lv_obj_remove_style_all(picker_root_);
    lv_obj_set_size(picker_root_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(picker_root_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(picker_root_, LV_OBJ_FLAG_SCROLLABLE);

    picker_status_ = lv_label_create(picker_root_);
    lv_label_set_text(picker_status_, scanning_ ? "Scanning…" : " ");
    lv_obj_set_style_text_font(picker_status_, font10, 0);
    lv_obj_set_style_text_color(picker_status_, tok.ink_dim, 0);
    lv_obj_align(picker_status_, LV_ALIGN_TOP_LEFT, 6, 0);

    picker_grid_ = lv_obj_create(picker_root_);
    lv_obj_remove_style_all(picker_grid_);
    lv_obj_set_size(picker_grid_, view::kScreenWidth, view::kContentHeight - 18 - 16);
    lv_obj_align(picker_grid_, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_clear_flag(picker_grid_, LV_OBJ_FLAG_SCROLLABLE);

    rebuild_picker_tiles();
    refresh_picker_selection();
}

void RokuScreen::destroy_picker_ui() {
    picker_tiles_.clear();
    picker_icons_.clear();
    picker_labels_.clear();
    if (picker_root_ && lv_obj_is_valid(picker_root_)) {
        lv_obj_delete(picker_root_);
    }
    picker_root_ = nullptr;
    picker_status_ = nullptr;
    picker_grid_ = nullptr;
}

void RokuScreen::rebuild_picker_tiles() {
    if (!picker_grid_) return;
    lv_obj_clean(picker_grid_);
    picker_tiles_.clear();
    picker_icons_.clear();
    picker_labels_.clear();

    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;
    auto* icon_font = assets().load_font("Phosphor-Fill.ttf", 22);

    constexpr int margin = 4;
    constexpr int gap = 4;
    const int grid_w = view::kScreenWidth;
    const int grid_h = view::kContentHeight - 18 - 16;
    const int cell_w = (grid_w - 2 * margin - (kPickerCols - 1) * gap) / kPickerCols;
    const int cell_h = (grid_h - (kPickerVisibleRows - 1) * gap - 2) / kPickerVisibleRows;

    const int n = static_cast<int>(picker_items_.size());
    const int rows = n == 0 ? 0 : (n + kPickerCols - 1) / kPickerCols;
    int sel_row = picker_sel_ / kPickerCols;
    if (sel_row < picker_scroll_row_) picker_scroll_row_ = sel_row;
    if (sel_row >= picker_scroll_row_ + kPickerVisibleRows) {
        picker_scroll_row_ = sel_row - kPickerVisibleRows + 1;
    }
    picker_scroll_row_ = std::clamp(picker_scroll_row_, 0, std::max(0, rows - kPickerVisibleRows));

    for (int vr = 0; vr < kPickerVisibleRows; ++vr) {
        const int src_row = picker_scroll_row_ + vr;
        for (int c = 0; c < kPickerCols; ++c) {
            const int idx = src_row * kPickerCols + c;
            if (idx >= n) continue;
            const auto& item = picker_items_[static_cast<size_t>(idx)];
            const int x = margin + c * (cell_w + gap);
            const int y = vr * (cell_h + gap);

            auto* tile = lv_obj_create(picker_grid_);
            view::style_tile(tile, 8);
            lv_obj_set_size(tile, cell_w, cell_h);
            lv_obj_set_pos(tile, x, y);

            auto* icon = lv_label_create(tile);
            const char* ic = view::ICON_CH_IR;
            if (item.kind == PickerKind::Rescan) ic = view::ICON_MENU_RESET;
            else if (item.kind == PickerKind::EnterIp) ic = view::ICON_MENU_CUSTOM;
            lv_label_set_text(icon, ic);
            if (icon_font) lv_obj_set_style_text_font(icon, icon_font, 0);
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 6);

            auto* label = lv_label_create(tile);
            std::string text;
            if (item.kind == PickerKind::Rescan) {
                text = "Rescan";
            } else if (item.kind == PickerKind::EnterIp) {
                text = "Enter IP";
            } else {
                const auto& d = devices_[static_cast<size_t>(item.device_index)];
                text = d.name.empty() ? d.ip : d.name;
                if (text.size() > 12) text = text.substr(0, 12);
            }
            lv_label_set_text(label, text.c_str());
            lv_obj_set_style_text_font(label, font10, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(label, cell_w - 6);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);

            picker_tiles_.push_back(tile);
            picker_icons_.push_back(icon);
            picker_labels_.push_back(label);
            lv_obj_set_user_data(tile, reinterpret_cast<void*>(static_cast<intptr_t>(idx)));
        }
    }
}

void RokuScreen::refresh_picker_selection() {
    const auto tok = tokens();
    for (size_t i = 0; i < picker_tiles_.size(); ++i) {
        auto* tile = picker_tiles_[i];
        const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(tile)));
        view::TileState st;
        st.selected = (idx == picker_sel_);
        view::apply_tile_state(tile, picker_icons_[i], picker_labels_[i], nullptr, st, tok);
    }
}

void RokuScreen::picker_move(int drow, int dcol) {
    if (picker_items_.empty()) return;
    const int n = static_cast<int>(picker_items_.size());
    int row = picker_sel_ / kPickerCols;
    int col = picker_sel_ % kPickerCols;
    row = std::clamp(row + drow, 0, (n + kPickerCols - 1) / kPickerCols - 1);
    col = std::clamp(col + dcol, 0, kPickerCols - 1);
    int next = row * kPickerCols + col;
    if (next >= n) next = n - 1;
    if (next < 0) next = 0;
    if (next == picker_sel_) return;
    picker_sel_ = next;
    rebuild_picker_tiles();
    refresh_picker_selection();
}

void RokuScreen::picker_activate() {
    if (picker_items_.empty()) return;
    const auto& item = picker_items_[static_cast<size_t>(picker_sel_)];
    if (item.kind == PickerKind::Rescan) {
        start_scan();
        return;
    }
    if (item.kind == PickerKind::EnterIp) {
        open_enter_ip();
        return;
    }
    select_device(devices_[static_cast<size_t>(item.device_index)]);
}

void RokuScreen::select_device(const tools::roku::Device& dev) {
    device_ = dev;
    save_ip(dev.ip);
    queue_.set_ip(dev.ip);
    showed_forbidden_ = false;
    typed_buffer_.clear();
    set_phase(Phase::Remote);
    set_input_mode(InputMode::RemoteKeys, false);
    set_status(dev.model.empty() ? "ready" : dev.model);
}

void RokuScreen::set_phase(Phase p) {
    if (phase_ == p) return;
    close_overlay();
    if (p == Phase::Picker) {
        destroy_remote_ui();
        build_picker_ui();
        if (devices_.empty() && !scanning_) start_scan();
        else apply_scan_results();
    } else {
        destroy_picker_ui();
        build_remote_ui();
    }
    phase_ = p;
    rebuild_action_bar();
}

void RokuScreen::leave_to_picker() {
    platform::set_text_entry(false);
    mode_ = InputMode::RemoteKeys;
    queue_.release_held();
    held_ecp_.clear();
    set_phase(Phase::Picker);
}

void RokuScreen::build_remote_ui() {
    destroy_remote_ui();
    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;
    const lv_font_t* font12 = assets().load_standard_font(12);
    if (!font12) font12 = &lv_font_montserrat_12;

    remote_root_ = lv_obj_create(content());
    lv_obj_remove_style_all(remote_root_);
    lv_obj_set_size(remote_root_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(remote_root_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(remote_root_, LV_OBJ_FLAG_SCROLLABLE);

    name_lbl_ = lv_label_create(remote_root_);
    lv_obj_set_style_text_font(name_lbl_, font12, 0);
    lv_obj_set_style_text_color(name_lbl_, tok.ink, 0);
    lv_obj_align(name_lbl_, LV_ALIGN_TOP_LEFT, 6, 0);

    mode_pill_ = view::make_status_pill(remote_root_, "REMOTE", view::StatusPillKind::Ok, font10, tok);
    lv_obj_align(mode_pill_, LV_ALIGN_TOP_RIGHT, -4, 0);

    // D-pad: 3x3 with corners empty; center OK; F/X/Z/C legends
    constexpr int pad_w = 34;
    constexpr int pad_h = 22;
    constexpr int gap = 3;
    const int pad_total_w = pad_w * 3 + gap * 2;
    const int pad_total_h = pad_h * 3 + gap * 2;
    const int ox = (view::kScreenWidth - pad_total_w) / 2;
    const int oy = 18;

    dpad_cells_[0] = make_dpad_btn(remote_root_, tok, font10, "F", ox + pad_w + gap, oy, pad_w, pad_h);
    dpad_cells_[1] =
        make_dpad_btn(remote_root_, tok, font10, "X", ox + pad_w + gap, oy + 2 * (pad_h + gap), pad_w, pad_h);
    dpad_cells_[2] = make_dpad_btn(remote_root_, tok, font10, "Z", ox, oy + pad_h + gap, pad_w, pad_h);
    dpad_cells_[3] =
        make_dpad_btn(remote_root_, tok, font10, "C", ox + 2 * (pad_w + gap), oy + pad_h + gap, pad_w, pad_h);
    dpad_cells_[4] =
        make_dpad_btn(remote_root_, tok, font10, "OK", ox + pad_w + gap, oy + pad_h + gap, pad_w, pad_h);

    status_lbl_ = lv_label_create(remote_root_);
    lv_obj_set_style_text_font(status_lbl_, font10, 0);
    lv_obj_set_style_text_color(status_lbl_, tok.ink_dim, 0);
    lv_obj_set_width(status_lbl_, view::kScreenWidth - 12);
    lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_align(status_lbl_, LV_ALIGN_BOTTOM_LEFT, 6, -14);

    typed_lbl_ = lv_label_create(remote_root_);
    lv_obj_set_style_text_font(typed_lbl_, font10, 0);
    lv_obj_set_style_text_color(typed_lbl_, tok.accent, 0);
    lv_obj_set_width(typed_lbl_, view::kScreenWidth - 12);
    lv_label_set_long_mode(typed_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_align(typed_lbl_, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    lv_label_set_text(typed_lbl_, "");

    banner_ = lv_obj_create(remote_root_);
    lv_obj_remove_style_all(banner_);
    lv_obj_set_size(banner_, view::kScreenWidth - 24, 28);
    lv_obj_align(banner_, LV_ALIGN_CENTER, 0, -4);
    lv_obj_set_style_radius(banner_, 6, 0);
    lv_obj_set_style_bg_color(banner_, tok.surface, 0);
    lv_obj_set_style_bg_opa(banner_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(banner_, 1, 0);
    lv_obj_set_style_border_color(banner_, tok.accent, 0);
    lv_obj_add_flag(banner_, LV_OBJ_FLAG_HIDDEN);
    banner_lbl_ = lv_label_create(banner_);
    lv_obj_set_style_text_font(banner_lbl_, font10, 0);
    lv_obj_set_style_text_color(banner_lbl_, tok.ink, 0);
    lv_obj_center(banner_lbl_);

    refresh_remote_labels();
    refresh_mode_pill();
}

void RokuScreen::destroy_remote_ui() {
    for (auto& c : dpad_cells_) c = nullptr;
    if (remote_root_ && lv_obj_is_valid(remote_root_)) {
        lv_obj_delete(remote_root_);
    }
    remote_root_ = nullptr;
    name_lbl_ = nullptr;
    mode_pill_ = nullptr;
    status_lbl_ = nullptr;
    typed_lbl_ = nullptr;
    banner_ = nullptr;
    banner_lbl_ = nullptr;
}

void RokuScreen::refresh_remote_labels() {
    if (name_lbl_) {
        std::string n = device_.name.empty() ? device_.ip : device_.name;
        if (n.size() > 18) n = n.substr(0, 18);
        lv_label_set_text(name_lbl_, n.c_str());
    }
    if (status_lbl_) lv_label_set_text(status_lbl_, status_text_.c_str());
    if (typed_lbl_) {
        if (mode_ == InputMode::Keyboard) {
            std::string show = typed_buffer_;
            if (show.size() > 28) show = show.substr(show.size() - 28);
            if (show.empty()) show = "_";
            lv_label_set_text(typed_lbl_, show.c_str());
            lv_obj_clear_flag(typed_lbl_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(typed_lbl_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void RokuScreen::refresh_mode_pill() {
    if (!mode_pill_) return;
    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (mode_ == InputMode::Keyboard) {
        view::set_status_pill(mode_pill_, "KEYBOARD", view::StatusPillKind::Busy, tok);
    } else {
        view::set_status_pill(mode_pill_, "REMOTE", view::StatusPillKind::Ok, tok);
    }
    (void)font10;
}

void RokuScreen::set_status(const std::string& s) {
    status_text_ = s;
    if (status_text_.size() > 36) status_text_ = status_text_.substr(0, 36);
    if (status_lbl_) lv_label_set_text(status_lbl_, status_text_.c_str());
}

void RokuScreen::show_banner(const char* text) {
    if (!banner_ || !banner_lbl_) return;
    lv_label_set_text(banner_lbl_, text);
    lv_obj_clear_flag(banner_, LV_OBJ_FLAG_HIDDEN);
    banner_until_ms_ = lv_tick_get() + 1000;
}

void RokuScreen::clear_banner() {
    banner_until_ms_ = 0;
    if (banner_) lv_obj_add_flag(banner_, LV_OBJ_FLAG_HIDDEN);
}

void RokuScreen::flash_dpad(DpadCell cell) {
    flash_cell_ = cell;
    flash_until_ms_ = lv_tick_get() + 120;
    if (phase_ != Phase::Remote) return;
    const auto tok = tokens();
    for (auto* c : dpad_cells_) style_dpad_flash(c, false, tok);
    int idx = -1;
    switch (cell) {
    case DpadCell::Up:
        idx = 0;
        break;
    case DpadCell::Down:
        idx = 1;
        break;
    case DpadCell::Left:
        idx = 2;
        break;
    case DpadCell::Right:
        idx = 3;
        break;
    case DpadCell::Ok:
        idx = 4;
        break;
    default:
        break;
    }
    if (idx >= 0) style_dpad_flash(dpad_cells_[idx], true, tok);
}

void RokuScreen::set_input_mode(InputMode m, bool show) {
    mode_ = m;
    if (m == InputMode::Keyboard) {
        platform::set_text_entry(true);
        if (show) show_banner("Switching to Keyboard Mode");
    } else {
        platform::set_text_entry(false);
        if (show) show_banner("Switching to Remote Mode");
    }
    refresh_mode_pill();
    refresh_remote_labels();
}

void RokuScreen::press_once(const char* key) {
    if (!key || !key[0]) return;
    queue_.enqueue_press(key);
    set_status(key);
}

void RokuScreen::hold_down(const char* key) {
    if (!key || !key[0]) return;
    if (!held_ecp_.empty() && held_ecp_ != key) {
        hold_up(held_ecp_.c_str());
    }
    held_ecp_ = key;
    queue_.enqueue_down(key);
    set_status(key);
}

void RokuScreen::hold_up(const char* key) {
    if (!key || !key[0]) return;
    queue_.enqueue_up(key);
}

void RokuScreen::hold_bar(const char* key) {
    // Action-bar clicks have no key-up; send a single keypress.
    press_once(key);
}

void RokuScreen::enter_remote() {
    set_phase(Phase::Remote);
}

// --- Overlays ----------------------------------------------------------------

void RokuScreen::close_overlay() {
    overlay_ = Overlay::None;
    if (overlay_root_ && lv_obj_is_valid(overlay_root_)) {
        lv_obj_delete(overlay_root_);
    }
    overlay_root_ = nullptr;
    if (mode_ == InputMode::Keyboard) platform::set_text_entry(true);
}

void RokuScreen::rebuild_overlay() {
    if (overlay_root_ && lv_obj_is_valid(overlay_root_)) {
        lv_obj_delete(overlay_root_);
    }
    overlay_root_ = nullptr;
    if (overlay_ == Overlay::None) return;

    const auto tok = tokens();
    const lv_font_t* font10 = assets().load_standard_font(10);
    if (!font10) font10 = &lv_font_montserrat_12;
    const lv_font_t* font12 = assets().load_standard_font(12);
    if (!font12) font12 = &lv_font_montserrat_12;

    overlay_root_ = lv_obj_create(content());
    lv_obj_remove_style_all(overlay_root_);
    lv_obj_set_size(overlay_root_, view::kScreenWidth, view::kContentHeight);
    lv_obj_align(overlay_root_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(overlay_root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_root_, LV_OPA_60, 0);
    lv_obj_clear_flag(overlay_root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* panel = lv_obj_create(overlay_root_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 292, 120);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, tok.surface, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, tok.accent, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, font12, 0);
    lv_obj_set_style_text_color(title, tok.ink, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 0);

    auto* body = lv_label_create(panel);
    lv_obj_set_style_text_font(body, font10, 0);
    lv_obj_set_style_text_color(body, tok.ink_dim, 0);
    lv_obj_set_width(body, 276);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 2, 18);

    if (overlay_ == Overlay::Help) {
        lv_label_set_text(title, "Roku help");
        lv_label_set_text(body,
                          "F/X/Z/C arrows  OK Enter  Bksp Back\n"
                          "4 Vol-  5 Rew  6 Play  7 FF  8 Vol+\n"
                          "Q Home  S Search  M Mute  I Info  G Replay\n"
                          "A Apps  B Audio  P Power  R Find  D Devices\n"
                          "Esc tap Remote/Keyboard\n"
                          "Hold Esc exit  H Help");
    } else if (overlay_ == Overlay::Audio) {
        lv_label_set_text(title, "Audio");
        std::ostringstream ss;
        if (audio_loading_) {
            ss << "Loading…";
        } else if (!audio_.raw_note.empty() && audio_.volume < 0 && audio_.destinations.empty()) {
            ss << audio_.raw_note << "\n";
            ss << "Pair BT on Roku: Settings > Audio";
        } else {
            ss << "Vol " << (audio_.volume >= 0 ? std::to_string(audio_.volume) : "?");
            ss << (audio_.muted ? "  muted" : "") << "\n";
            if (audio_.headphones_connected || device_.headphones_connected) ss << "Headphones connected\n";
            if (audio_.destinations.empty()) {
                ss << "No destination names\n";
            } else {
                for (size_t i = 0; i < audio_.destinations.size() && i < 3; ++i) {
                    ss << "- " << audio_.destinations[i] << "\n";
                }
            }
            ss << "Pair BT on Roku: Settings > Audio";
        }
        lv_label_set_text(body, ss.str().c_str());
    } else if (overlay_ == Overlay::Power) {
        lv_label_set_text(title, "Power Off?");
        lv_label_set_text(body, "Enter = Power Off\nEsc = Cancel");
    } else if (overlay_ == Overlay::EnterIp) {
        lv_label_set_text(title, "Roku IP");
        std::string show = enter_ip_buf_.empty() ? "_" : enter_ip_buf_;
        lv_label_set_text_fmt(body, "%s\nEnter = connect  Esc = cancel", show.c_str());
        platform::set_text_entry(true);
    } else if (overlay_ == Overlay::Apps) {
        lv_label_set_text(title, "Apps");
        if (apps_loading_) {
            lv_label_set_text(body, "Loading…");
        } else if (apps_.empty()) {
            lv_label_set_text(body, "No apps");
        } else {
            std::ostringstream ss;
            const int start = std::max(0, apps_sel_ - 1);
            for (int i = start; i < static_cast<int>(apps_.size()) && i < start + 4; ++i) {
                ss << (i == apps_sel_ ? "> " : "  ") << apps_[static_cast<size_t>(i)].name << "\n";
            }
            ss << "Enter launch  Esc back";
            lv_label_set_text(body, ss.str().c_str());
        }
    }
}

void RokuScreen::open_help() {
    if (overlay_ == Overlay::Help) {
        close_overlay();
        return;
    }
    overlay_ = Overlay::Help;
    rebuild_overlay();
}

void RokuScreen::open_audio() {
    overlay_ = Overlay::Audio;
    audio_loading_ = true;
    audio_ = {};
    audio_.headphones_connected = device_.headphones_connected;
    rebuild_overlay();
    std::thread([this]() {
        tools::roku::AudioInfo info;
        const auto st = tools::roku::query_audio_device(device_.ip, info);
        info.headphones_connected = info.headphones_connected || device_.headphones_connected;
        if (st != tools::roku::Status::Ok && info.raw_note.empty()) {
            info.raw_note = tools::roku::status_message(st);
        }
        {
            std::lock_guard<std::mutex> lock(result_mu_);
            audio_ = std::move(info);
            audio_loading_ = false;
            pending_status_ = "audio";
        }
    }).detach();
}

void RokuScreen::open_apps() {
    overlay_ = Overlay::Apps;
    apps_loading_ = true;
    apps_sel_ = 0;
    rebuild_overlay();
    std::thread([this]() {
        std::vector<tools::roku::App> list;
        const auto st = tools::roku::query_apps(device_.ip, list);
        {
            std::lock_guard<std::mutex> lock(result_mu_);
            if (st == tools::roku::Status::Ok) {
                apps_ = std::move(list);
            } else {
                apps_.clear();
                pending_status_ = tools::roku::status_message(st);
            }
            apps_loading_ = false;
            pending_status_ = pending_status_.empty() ? "apps" : pending_status_;
        }
    }).detach();
}

void RokuScreen::open_power() {
    overlay_ = Overlay::Power;
    rebuild_overlay();
}

void RokuScreen::open_enter_ip() {
    overlay_ = Overlay::EnterIp;
    enter_ip_buf_ = load_saved_ip();
    rebuild_overlay();
}

void RokuScreen::launch_selected_app() {
    if (apps_.empty() || apps_sel_ < 0 || apps_sel_ >= static_cast<int>(apps_.size())) return;
    const auto& app = apps_[static_cast<size_t>(apps_sel_)];
    std::thread([this, id = app.id, name = app.name]() {
        const auto st = tools::roku::launch_app(device_.ip, id);
        std::lock_guard<std::mutex> lock(result_mu_);
        pending_status_ = st == tools::roku::Status::Ok ? ("Launch " + name) : tools::roku::status_message(st);
    }).detach();
    close_overlay();
}

void RokuScreen::confirm_power() {
    press_once("PowerOff");
    close_overlay();
}

// --- Input -------------------------------------------------------------------

bool RokuScreen::handle_overlay_key(uint32_t key) {
    if (overlay_ == Overlay::None) return false;

    // Esc closes overlays in on_early_key (hold Esc exits the tool).
    if (key == LV_KEY_ESC) return true;

    if (overlay_ == Overlay::Help) {
        if (key == 'h' || key == 'H') {
            close_overlay();
            return true;
        }
        return true; // consume
    }

    if (overlay_ == Overlay::Power) {
        if (key == LV_KEY_ENTER || key == '\n') {
            confirm_power();
            return true;
        }
        return true;
    }

    if (overlay_ == Overlay::EnterIp) {
        if (key == LV_KEY_ENTER || key == '\n') {
            const std::string ip = trim_copy(enter_ip_buf_);
            if (looks_like_ipv4(ip)) {
                tools::roku::Device d;
                d.ip = ip;
                d.name = ip;
                // Enrich in background after select
                select_device(d);
                close_overlay();
                std::thread([this, ip]() {
                    tools::roku::Device info;
                    if (tools::roku::query_device_info(ip, info) == tools::roku::Status::Ok) {
                        std::lock_guard<std::mutex> lock(result_mu_);
                        device_ = info;
                        pending_status_ = info.name;
                    }
                }).detach();
            } else {
                set_status("Bad IP");
            }
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == 8) {
            if (!enter_ip_buf_.empty()) enter_ip_buf_.pop_back();
            rebuild_overlay();
            return true;
        }
        if (key >= 32 && key <= 126) {
            if (enter_ip_buf_.size() < 15) {
                enter_ip_buf_ += static_cast<char>(key);
                rebuild_overlay();
            }
            return true;
        }
        return true;
    }

    if (overlay_ == Overlay::Apps) {
        if (key == LV_KEY_UP) {
            if (apps_sel_ > 0) {
                --apps_sel_;
                rebuild_overlay();
            }
            return true;
        }
        if (key == LV_KEY_DOWN) {
            if (apps_sel_ + 1 < static_cast<int>(apps_.size())) {
                ++apps_sel_;
                rebuild_overlay();
            }
            return true;
        }
        if (key == LV_KEY_ENTER || key == '\n') {
            launch_selected_app();
            return true;
        }
        return true;
    }

    if (overlay_ == Overlay::Audio) {
        return true;
    }
    return true;
}

bool RokuScreen::handle_picker_key(uint32_t key) {
    if (overlay_ != Overlay::None) return handle_overlay_key(key);
    if (key == LV_KEY_UP) {
        picker_move(-1, 0);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        picker_move(1, 0);
        return true;
    }
    if (key == LV_KEY_LEFT) {
        picker_move(0, -1);
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        picker_move(0, 1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == ' ') {
        picker_activate();
        return true;
    }
    if (key == 'h' || key == 'H') {
        open_help();
        return true;
    }
    return KvxScreen::handle_key(key);
}

bool RokuScreen::handle_keyboard_key(uint32_t key) {
    // Esc is handled in on_early_key (tap = toggle, hold = exit).
    if (key == LV_KEY_ESC) return true;
    if (key == LV_KEY_ENTER || key == '\n') {
        press_once("Enter");
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == 8) {
        press_once("Backspace");
        if (!typed_buffer_.empty()) typed_buffer_.pop_back();
        refresh_remote_labels();
        return true;
    }
    // Action bar digits stay for volume etc.
    if (key == '4') {
        hold_down("VolumeDown");
        return true;
    }
    if (key == '5') {
        hold_down("Rev");
        return true;
    }
    if (key == '6') {
        press_once("Play");
        return true;
    }
    if (key == '7') {
        hold_down("Fwd");
        return true;
    }
    if (key == '8') {
        hold_down("VolumeUp");
        return true;
    }
    // Letters type Lit_ keys in Keyboard mode (H help is Remote-only; use Fn+H globally).
    if (key >= 32 && key <= 126) {
        const char c = static_cast<char>(key);
        const std::string lit = tools::roku::lit_key(c);
        press_once(lit.c_str());
        typed_buffer_ += c;
        if (typed_buffer_.size() > 64) typed_buffer_.erase(0, typed_buffer_.size() - 64);
        refresh_remote_labels();
        return true;
    }
    return true;
}

bool RokuScreen::handle_remote_key(uint32_t key) {
    // Esc is handled in on_early_key (tap = toggle, hold = exit).
    if (key == LV_KEY_ESC) return true;
    if (key == LV_KEY_UP) {
        flash_dpad(DpadCell::Up);
        hold_down("Up");
        return true;
    }
    if (key == LV_KEY_DOWN) {
        flash_dpad(DpadCell::Down);
        hold_down("Down");
        return true;
    }
    if (key == LV_KEY_LEFT) {
        flash_dpad(DpadCell::Left);
        hold_down("Left");
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        flash_dpad(DpadCell::Right);
        hold_down("Right");
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == ' ') {
        flash_dpad(DpadCell::Ok);
        press_once("Select");
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == 8) {
        press_once("Back");
        return true;
    }
    if (key == '4') {
        hold_down("VolumeDown");
        return true;
    }
    if (key == '5') {
        hold_down("Rev");
        return true;
    }
    if (key == '6') {
        press_once("Play");
        return true;
    }
    if (key == '7') {
        hold_down("Fwd");
        return true;
    }
    if (key == '8') {
        hold_down("VolumeUp");
        return true;
    }
    if (key == 'q' || key == 'Q') {
        press_once("Home");
        return true;
    }
    if (key == 's' || key == 'S') {
        press_once("Search");
        set_input_mode(InputMode::Keyboard, true);
        return true;
    }
    if (key == 'm' || key == 'M') {
        press_once("VolumeMute");
        return true;
    }
    if (key == 'i' || key == 'I') {
        press_once("Info");
        return true;
    }
    if (key == 'g' || key == 'G') {
        press_once("InstantReplay");
        return true;
    }
    if (key == 'a' || key == 'A') {
        open_apps();
        return true;
    }
    if (key == 'b' || key == 'B') {
        open_audio();
        return true;
    }
    if (key == 'p' || key == 'P') {
        open_power();
        return true;
    }
    if (key == 'r' || key == 'R') {
        if (device_.supports_find_remote) press_once("FindRemote");
        else set_status("FindRemote unsupported");
        return true;
    }
    if (key == 'd' || key == 'D') {
        leave_to_picker();
        return true;
    }
    if (key == 'h' || key == 'H') {
        open_help();
        return true;
    }
    if (key == '9') {
        press_once("ChannelUp");
        return true;
    }
    if (key == '0') {
        press_once("ChannelDown");
        return true;
    }
    return true;
}

bool RokuScreen::handle_key(uint32_t key) {
    // Refresh overlays after async load.
    {
        std::lock_guard<std::mutex> lock(result_mu_);
        if (pending_status_ == "audio" || pending_status_ == "apps") {
            pending_status_.clear();
            if (overlay_ == Overlay::Audio || overlay_ == Overlay::Apps) rebuild_overlay();
        } else if (pending_status_ == device_.name || (!pending_status_.empty() && name_lbl_ &&
                                                       phase_ == Phase::Remote && overlay_ == Overlay::None)) {
            // device enrich
            if (name_lbl_ && !device_.name.empty()) refresh_remote_labels();
        }
    }

    if (overlay_ != Overlay::None) return handle_overlay_key(key);

    if (phase_ == Phase::Picker) return handle_picker_key(key);
    if (mode_ == InputMode::Keyboard) return handle_keyboard_key(key);
    return handle_remote_key(key);
}

std::array<view::widgets::ActionSlot, 5> RokuScreen::nav_actions() {
    if (phase_ == Phase::Picker) {
        return {{
            {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
            {view::ICON_CARET_UP, [this]() { picker_move(-1, 0); }},
            {view::ICON_CHECK, [this]() { picker_activate(); }},
            {view::ICON_CARET_DOWN, [this]() { picker_move(1, 0); }},
            {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
        }};
    }
    return {{
        {view::ICON_MINUS, [this]() { hold_bar("VolumeDown"); }},
        {view::ICON_SKIP_BACK, [this]() { hold_bar("Rev"); }},
        {view::ICON_PLAY, [this]() { hold_bar("Play"); }},
        {view::ICON_SKIP_FORWARD, [this]() { hold_bar("Fwd"); }},
        {view::ICON_PLUS, [this]() { hold_bar("VolumeUp"); }},
    }};
}

} // namespace screen

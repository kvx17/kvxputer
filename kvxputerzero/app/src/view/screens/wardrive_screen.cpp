#include "wardrive_screen.h"

#include "asset_manager.h"
#include "backlight.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>

namespace screen {
namespace {

void poll_cb(lv_timer_t* t) {
    auto* self = static_cast<WardriveScreen*>(lv_timer_get_user_data(t));
    if (self) self->flush_pending();
}

void set_label(lv_obj_t* label, const char* text, lv_color_t color) {
    if (!label) return;
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, color, 0);
}

} // namespace

WardriveScreen::WardriveScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                               std::string name_column, WorkerFn worker, KeyFn on_key, DoneFn on_done)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      name_column_(std::move(name_column)),
      worker_(std::move(worker)),
      on_key_(std::move(on_key)),
      on_done_(std::move(on_done)) {
    init();
}

WardriveScreen::~WardriveScreen() {
    cancel_display_blank();
    wake_display();
    platform::clear_early_key_listener(on_early_key, this);
    platform::clear_screen_key_release_listener(on_key_up, this);
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    stop_worker();
}

WardriveScreen::Cols WardriveScreen::make_cols(lv_obj_t* parent, int width) {
    Cols cols;
    cols.row = lv_obj_create(parent);
    lv_obj_remove_style_all(cols.row);
    lv_obj_set_size(cols.row, width, 16);
    lv_obj_set_flex_flow(cols.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cols.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cols.row, 2, 0);
    lv_obj_set_style_pad_left(cols.row, 4, 0);
    lv_obj_clear_flag(cols.row, LV_OBJ_FLAG_SCROLLABLE);

    auto add = [&](int w) {
        auto* label = lv_label_create(cols.row);
        lv_obj_set_width(label, w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, font_, 0);
        lv_label_set_text(label, "");
        return label;
    };
    cols.name = add(164);
    cols.channel = add(32);
    cols.rssi = add(48);
    cols.mark = add(52);
    return cols;
}

void WardriveScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();
    font_ = assets().load_standard_font(12);
    if (!font_) font_ = &lv_font_montserrat_12;

    status_label_ = lv_label_create(content());
    lv_label_set_text(status_label_, "starting...");
    lv_obj_set_style_text_color(status_label_, tok.ink_dim, 0);
    lv_obj_set_style_text_font(status_label_, font_, 0);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status_label_, view::kScreenWidth - 8);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 4, 20);

    constexpr int kHeaderY = 36;
    constexpr int kListTop = 52;
    constexpr int kRowW = view::kScreenWidth - 4;
    {
        Cols head = make_cols(content(), kRowW);
        header_ = head.row;
        lv_obj_align(header_, LV_ALIGN_TOP_LEFT, 2, kHeaderY);
        set_label(head.name, name_column_.c_str(), tok.ink_dim);
        set_label(head.channel, "CH", tok.ink_dim);
        set_label(head.rssi, "dBm", tok.ink_dim);
        set_label(head.mark, "Sec", tok.ink_dim);
        lv_obj_set_style_border_side(header_, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(header_, 1, 0);
        lv_obj_set_style_border_color(header_, tok.rule, 0);
        lv_obj_set_style_border_opa(header_, LV_OPA_COVER, 0);
    }

    list_ = lv_obj_create(content());
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, view::kScreenWidth, view::kContentHeight - kListTop);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, kListTop);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 0, 0);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_SCROLLABLE);

    const int vis = std::max(1, (view::kContentHeight - kListTop) / 16);
    rows_ui_.reserve(static_cast<size_t>(vis));
    for (int i = 0; i < vis; ++i) {
        Cols cols = make_cols(list_, kRowW);
        lv_obj_add_event_cb(cols.row, on_row_clicked, LV_EVENT_CLICKED, this);
        rows_ui_.push_back(cols);
    }

    empty_label_ = lv_label_create(content());
    lv_label_set_text(empty_label_, "Scanning for networks");
    lv_obj_set_style_text_font(empty_label_, font_, 0);
    lv_obj_set_style_text_color(empty_label_, tok.ink_dim, 0);
    lv_obj_align(empty_label_, LV_ALIGN_TOP_LEFT, 8, 74);

    detail_ = view::make_panel(content(), kHeaderY, tok);
    lv_obj_set_style_pad_top(detail_, 2, 0);
    lv_obj_set_style_pad_bottom(detail_, 2, 0);
    lv_obj_set_style_pad_row(detail_, 1, 0);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    blank_cover_ = lv_obj_create(root());
    lv_obj_remove_style_all(blank_cover_);
    lv_obj_set_size(blank_cover_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_pos(blank_cover_, 0, 0);
    lv_obj_set_style_bg_color(blank_cover_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(blank_cover_, LV_OPA_COVER, 0);
    lv_obj_add_flag(blank_cover_, LV_OBJ_FLAG_HIDDEN);
    auto* blank_label = lv_label_create(blank_cover_);
    lv_label_set_text(blank_label, "Display off    any key");
    lv_obj_set_style_text_font(blank_label, font_, 0);
    lv_obj_set_style_text_color(blank_label, tok.ink_dim, 0);
    lv_obj_align(blank_label, LV_ALIGN_CENTER, 0, 0);

    refresh_table();
    poll_timer_ = lv_timer_create(poll_cb, 100, this);
    platform::set_early_key_listener(on_early_key, this);
    platform::set_screen_key_release_listener(on_key_up, this);

    cancel_ = false;
    worker_done_ = false;
    worker_thread_ = std::thread([this]() {
        auto set_rows = [this](std::vector<WardriveRow> rows) {
            std::lock_guard<std::mutex> lock(mu_);
            pending_rows_ = std::move(rows);
            rows_dirty_ = true;
        };
        auto set_status = [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(mu_);
            status_ = s;
        };
        if (worker_) worker_(cancel_, set_rows, set_status);
        std::lock_guard<std::mutex> lock(mu_);
        worker_done_ = true;
        if (status_.empty() || status_ == "starting...") status_ = cancel_ ? "cancelled" : "done";
    });
}

void WardriveScreen::on_row_clicked(lv_event_t* event) {
    auto* self = static_cast<WardriveScreen*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self || !row || self->expanded_) return;
    const auto index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)));
    if (index < 0 || index >= static_cast<int>(self->rows_.size())) return;
    self->selection_pinned_ = true;
    self->selected_ = index;
    self->selected_key_ = self->rows_[static_cast<size_t>(index)].key;
    self->expand_selected();
}

void WardriveScreen::paint_row(const Cols& cols, const WardriveRow* data, bool selected) {
    const auto tok = tokens();
    const lv_color_t ink = selected ? tok.ink : tok.ink_dim;
    if (!data) {
        set_label(cols.name, "", ink);
        set_label(cols.channel, "", ink);
        set_label(cols.rssi, "", ink);
        set_label(cols.mark, "", ink);
        lv_obj_set_style_bg_opa(cols.row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cols.row, 0, 0);
        return;
    }
    set_label(cols.name, data->name.c_str(), ink);
    set_label(cols.channel, data->channel.c_str(), ink);
    set_label(cols.rssi, data->rssi.c_str(), ink);
    set_label(cols.mark, data->mark.c_str(), ink);
    lv_obj_set_style_bg_color(cols.row, tok.surface_sel, 0);
    lv_obj_set_style_bg_opa(cols.row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(cols.row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(cols.row, 1, 0);
    lv_obj_set_style_border_color(cols.row, selected ? tok.accent : tok.rule, 0);
    lv_obj_set_style_border_opa(cols.row, LV_OPA_COVER, 0);
}

void WardriveScreen::refresh_table() {
    const int count = static_cast<int>(rows_.size());
    const int vis = static_cast<int>(rows_ui_.size());
    if (count == 0) selected_ = 0;
    else if (selected_ < 0) selected_ = 0;
    else if (selected_ >= count) selected_ = count - 1;

    int start = 0;
    if (count > 0) {
        start = selected_ - vis / 2;
        if (start < 0) start = 0;
        if (start > count - vis) start = std::max(0, count - vis);
    }

    for (int i = 0; i < vis; ++i) {
        auto& cols = rows_ui_[static_cast<size_t>(i)];
        const int index = start + i;
        const bool live = index >= 0 && index < count;
        if (live) {
            lv_obj_clear_flag(cols.row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(cols.row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(cols.row, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
            paint_row(cols, &rows_[static_cast<size_t>(index)], index == selected_);
        } else {
            lv_obj_add_flag(cols.row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(cols.row, LV_OBJ_FLAG_CLICKABLE);
            paint_row(cols, nullptr, false);
        }
    }
    if (empty_label_) {
        if (count == 0 && !expanded_) lv_obj_clear_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void WardriveScreen::move_selection(int delta) {
    if (expanded_) {
        if (detail_) lv_obj_scroll_by(detail_, 0, delta > 0 ? -14 : 14, LV_ANIM_OFF);
        return;
    }
    if (rows_.empty()) return;
    selection_pinned_ = true;
    const int count = static_cast<int>(rows_.size());
    selected_ = (selected_ + delta + count) % count;
    selected_key_ = rows_[static_cast<size_t>(selected_)].key;
    refresh_table();
}

void WardriveScreen::rebuild_detail() {
    if (!detail_) return;
    lv_obj_clean(detail_);
    detail_labels_.clear();
    detail_values_.clear();
    const auto tok = tokens();
    const WardriveRow* row = nullptr;
    for (const auto& candidate : rows_) {
        if (candidate.key == expanded_key_) {
            row = &candidate;
            break;
        }
    }
    if (!row || row->details.empty()) {
        auto* made = view::make_kv_row(detail_, "—", "network left range", font_, tok);
        detail_labels_.push_back("—");
        detail_values_.push_back(lv_obj_get_child(made, 1));
        return;
    }
    for (const auto& field : row->details) {
        auto* made = view::make_kv_row(detail_, field.first.c_str(), field.second.c_str(), font_, tok);
        detail_labels_.push_back(field.first);
        detail_values_.push_back(lv_obj_get_child(made, 1));
    }
}

void WardriveScreen::expand_selected() {
    if (rows_.empty() || selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return;
    expanded_ = true;
    expanded_key_ = rows_[static_cast<size_t>(selected_)].key;
    if (list_) lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
    if (header_) lv_obj_add_flag(header_, LV_OBJ_FLAG_HIDDEN);
    if (empty_label_) lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    if (detail_) lv_obj_clear_flag(detail_, LV_OBJ_FLAG_HIDDEN);
    rebuild_detail();
}

void WardriveScreen::collapse() {
    expanded_ = false;
    if (detail_) lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
    if (header_) lv_obj_clear_flag(header_, LV_OBJ_FLAG_HIDDEN);
    if (list_) lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    refresh_table();
}

void WardriveScreen::flush_pending() {
    std::vector<WardriveRow> incoming;
    std::string status;
    bool dirty = false;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        status = status_;
        dirty = rows_dirty_;
        if (dirty) {
            incoming = std::move(pending_rows_);
            rows_dirty_ = false;
        }
        done = worker_done_;
    }
    if (status_label_ && !status.empty()) lv_label_set_text(status_label_, status.c_str());
    if (dirty) {
        rows_ = std::move(incoming);
        if (!selection_pinned_) {
            selected_ = 0;
            selected_key_ = rows_.empty() ? std::string() : rows_.front().key;
        } else if (!selected_key_.empty()) {
            selected_ = 0;
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                if (rows_[static_cast<size_t>(i)].key == selected_key_) {
                    selected_ = i;
                    break;
                }
            }
            if (!rows_.empty()) selected_key_ = rows_[static_cast<size_t>(selected_)].key;
        }
        if (!expanded_) refresh_table();
        else {
            const WardriveRow* row = nullptr;
            for (const auto& candidate : rows_) {
                if (candidate.key == expanded_key_) {
                    row = &candidate;
                    break;
                }
            }
            bool same = row && row->details.size() == detail_labels_.size();
            if (same) {
                for (size_t i = 0; i < detail_labels_.size(); ++i) {
                    if (detail_labels_[i] != row->details[i].first) {
                        same = false;
                        break;
                    }
                }
            }
            if (!same) rebuild_detail();
            else {
                for (size_t i = 0; i < detail_values_.size() && row && i < row->details.size(); ++i) {
                    if (detail_values_[i]) lv_label_set_text(detail_values_[i], row->details[i].second.c_str());
                }
            }
        }
    }
    if (done && poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
        if (worker_thread_.joinable()) worker_thread_.join();
    }
}

void WardriveScreen::stop_worker() {
    cancel_ = true;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void WardriveScreen::arm_display_blank() {
    if (display_off_ || blank_timer_) return;
    blank_timer_ = lv_timer_create(blank_timer_cb, 3000, this);
    if (blank_timer_) lv_timer_set_repeat_count(blank_timer_, 1);
}

void WardriveScreen::cancel_display_blank() {
    if (!blank_timer_) return;
    lv_timer_delete(blank_timer_);
    blank_timer_ = nullptr;
}

void WardriveScreen::blank_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<WardriveScreen*>(lv_timer_get_user_data(timer));
    if (!self) return;
    // LVGL deletes a one-shot timer after this callback returns.
    self->blank_timer_ = nullptr;
    self->blank_display();
}

void WardriveScreen::blank_display() {
    if (display_off_) return;
    int32_t current = 0;
    if (platform::read_brightness_percent(current) && current > 0) {
        saved_brightness_ = current;
        saved_brightness_valid_ = true;
    }
    platform::write_brightness_percent(0);
    display_off_ = true;
    if (blank_cover_) {
        lv_obj_remove_flag(blank_cover_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(blank_cover_);
    }
}

void WardriveScreen::wake_display() {
    if (!display_off_) return;
    display_off_ = false;
    if (saved_brightness_valid_) platform::write_brightness_percent(saved_brightness_);
    else platform::write_brightness_percent(70);
    if (blank_cover_) lv_obj_add_flag(blank_cover_, LV_OBJ_FLAG_HIDDEN);
}

bool WardriveScreen::on_early_key(uint32_t key, const char*, bool long_pressed, void* user_data) {
    auto* self = static_cast<WardriveScreen*>(user_data);
    if (!self) return false;
    if (self->display_off_) {
        if (!long_pressed) {
            self->wake_display();
            // The key that woke the screen must not also leave or toggle a control.
            self->swallow_long_ = key;
        }
        return true;
    }
    if (long_pressed && key == self->swallow_long_) {
        self->swallow_long_ = 0;
        return true;
    }
    if (!long_pressed) self->swallow_long_ = 0;
    return false;
}

void WardriveScreen::on_key_up(uint32_t key, const char*, void* user_data) {
    auto* self = static_cast<WardriveScreen*>(user_data);
    if (!self) return;
    if (key == platform::kKeyTab) self->cancel_display_blank();
}

void WardriveScreen::leave() {
    cancel_display_blank();
    wake_display();
    stop_worker();
    if (on_done_ && !done_fired_) {
        done_fired_ = true;
        on_done_();
        return;
    }
    view_model().go_back();
}

void WardriveScreen::go_home() {
    cancel_display_blank();
    wake_display();
    stop_worker();
    if (on_done_ && !done_fired_) {
        done_fired_ = true;
        on_done_();
        return;
    }
    view_model().go_home();
}

bool WardriveScreen::handle_key(uint32_t key) {
    if (key == platform::kKeyTab) {
        arm_display_blank();
        return true;
    }
    if (key == LV_KEY_ESC) {
        if (expanded_) {
            collapse();
            return true;
        }
        leave();
        return true;
    }
    if (key == LV_KEY_DOWN) {
        move_selection(1);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n') {
        if (expanded_) collapse();
        else expand_selected();
        return true;
    }
    if (on_key_ && on_key_(key)) return true;
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> WardriveScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             if (expanded_) collapse();
             else leave();
         }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {view::ICON_MENU_ABOUT,
         [this]() {
             if (expanded_) collapse();
             else expand_selected();
         }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_HOUSE, [this]() { go_home(); }},
    }};
}

} // namespace screen

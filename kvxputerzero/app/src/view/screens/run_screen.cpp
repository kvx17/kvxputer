#include "run_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>

namespace screen {
namespace {

void poll_cb(lv_timer_t* t) {
    auto* self = static_cast<RunScreen*>(lv_timer_get_user_data(t));
    if (self) self->flush_pending();
}

} // namespace

RunScreen::RunScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                     WorkerFn worker, DoneFn on_done)
    : KvxScreen(view_model, assets), title_(std::move(title)), worker_(std::move(worker)),
      on_done_(std::move(on_done)) {
    init();
}

RunScreen::~RunScreen() {
    stop_worker();
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void RunScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    if (!font) font = &lv_font_montserrat_12;

    status_label_ = lv_label_create(content());
    lv_label_set_text(status_label_, "starting...");
    lv_obj_set_style_text_color(status_label_, tok.ink_dim, 0);
    lv_obj_set_style_text_font(status_label_, font, 0);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status_label_, view::kScreenWidth - 8);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 4, 20);

    // Log lines: plain text + left-rule cursor (not menu pills).
    constexpr int kListTop = 36;
    constexpr int kRowH = 14;
    list_ = view::make_list(content(), kListTop);
    lv_obj_set_style_pad_row(list_, 0, 0);
    const int vis = std::max(1, (view::kContentHeight - kListTop - 4) / kRowH);
    rows_.reserve(static_cast<size_t>(vis));
    for (int i = 0; i < vis; ++i) {
        auto* row = view::make_log_row(list_, font, tok);
        lv_obj_set_height(row, kRowH);
        rows_.push_back(row);
    }
    refresh_list();

    poll_timer_ = lv_timer_create(poll_cb, 100, this);
}

void RunScreen::on_shown() {
    start_worker();
}

void RunScreen::start_worker() {
    if (worker_started_) return;
    worker_started_ = true;
    cancel_ = false;
    worker_done_ = false;
    worker_thread_ = std::thread([this]() {
        auto append = [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(mu_);
            pending_lines_.push_back(line);
        };
        auto set_status = [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(mu_);
            status_ = s;
        };
        if (worker_) worker_(cancel_, append, set_status);
        {
            std::lock_guard<std::mutex> lock(mu_);
            worker_done_ = true;
            if (status_.empty() || status_ == "starting...") status_ = cancel_ ? "cancelled" : "done";
        }
    });
}

void RunScreen::flush_pending() {
    std::vector<std::string> lines;
    std::string status;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        lines.swap(pending_lines_);
        status = status_;
        done = worker_done_;
    }
    if (!pinned_ && !status.empty() && status_label_) lv_label_set_text(status_label_, status.c_str());
    if (!lines.empty()) {
        constexpr size_t kMaxLines = 250;
        lines_.insert(lines_.end(), lines.begin(), lines.end());
        if (lines_.size() > kMaxLines) {
            const size_t drop = lines_.size() - kMaxLines;
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::vector<std::string>::difference_type>(drop));
            if (selected_ >= 0) selected_ = std::max(0, selected_ - static_cast<int>(drop));
        }
        if (follow_tail_) selected_ = static_cast<int>(lines_.size()) - 1;
        refresh_list();
    }
    if (done && poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
        if (worker_thread_.joinable()) worker_thread_.join();
        if (on_done_ && !done_notified_) {
            done_notified_ = true;
            on_done_(cancel_.load());
        }
    }
}

void RunScreen::stop_worker() {
    cancel_ = true;
    if (worker_thread_.joinable()) worker_thread_.join();
    if (on_done_ && !done_notified_) {
        done_notified_ = true;
        on_done_(true);
    }
}

void RunScreen::refresh_list() {
    if (rows_.empty()) return;
    const int count = static_cast<int>(lines_.size());
    const int vis = static_cast<int>(rows_.size());
    if (count == 0) selected_ = -1;
    else if (selected_ < 0) selected_ = 0;
    else if (selected_ >= count) selected_ = count - 1;

    int start = 0;
    if (selected_ >= 0) {
        start = selected_ - vis / 2;
        if (start < 0) start = 0;
        if (start > count - vis) start = std::max(0, count - vis);
    }

    const auto tok = tokens();
    for (int i = 0; i < vis; ++i) {
        auto* row = rows_[static_cast<size_t>(i)];
        const int index = start + i;
        const bool live = index >= 0 && index < count;
        if (auto* label = lv_obj_get_child(row, 0)) {
            lv_label_set_text(label, live ? lines_[static_cast<size_t>(index)].c_str() : "");
        }
        view::apply_log_row_state(row, live && index == selected_, tok);
        if (live) lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

void RunScreen::move_selection(int delta) {
    if (lines_.empty()) return;
    follow_tail_ = false;
    const int count = static_cast<int>(lines_.size());
    if (selected_ < 0) selected_ = 0;
    selected_ = (selected_ + delta + count) % count;
    refresh_list();
}

void RunScreen::accept_line() {
    follow_tail_ = false;
    if (selected_ >= 0 && selected_ < static_cast<int>(lines_.size())) {
        pinned_ = true;
        pinned_text_ = lines_[static_cast<size_t>(selected_)];
        if (status_label_) lv_label_set_text(status_label_, pinned_text_.c_str());
    }
    // Freeze a live scan so the highlighted row stays put. Back still leaves.
    if (!worker_done_) cancel_ = true;
}

bool RunScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_worker();
        view_model().go_back();
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
        accept_line();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> RunScreen::nav_actions() {
    // Slot 6 stays empty so logs don't look activatable; Enter still pins/freezes.
    return {{
        {view::ICON_ARROW_BACK,
         [this]() {
             stop_worker();
             view_model().go_back();
         }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {"", {}},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_HOUSE,
         [this]() {
             stop_worker();
             view_model().go_home();
         }},
    }};
}

} // namespace screen

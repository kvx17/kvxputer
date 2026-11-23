#pragma once

#include "kvx_screen.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

// Live log screen. Worker runs on a background thread; Esc sets cancel and joins.
class RunScreen final : public KvxScreen {
public:
    using WorkerFn = std::function<void(std::atomic<bool>& cancel,
                                        const std::function<void(const std::string&)>& append_line,
                                        const std::function<void(const std::string&)>& set_status)>;
    // Invoked on the UI thread after the worker joins (Esc, home, or natural finish).
    using DoneFn = std::function<void(bool cancelled)>;

    RunScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
              WorkerFn worker, DoneFn on_done = {});
    ~RunScreen() override;

    void flush_pending();

    void on_shown() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void start_worker();
    void stop_worker();
    void move_selection(int delta);
    void accept_line();
    void refresh_list();

    bool worker_started_{false};

    std::string title_;
    WorkerFn worker_;
    DoneFn on_done_;
    bool done_notified_{false};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> rows_;
    lv_timer_t* poll_timer_{nullptr};

    std::atomic<bool> cancel_{false};
    std::thread worker_thread_;
    std::mutex mu_;
    std::string status_;
    std::vector<std::string> pending_lines_;
    std::vector<std::string> lines_;
    int selected_{-1};
    bool follow_tail_{true};
    bool pinned_{false};
    std::string pinned_text_;
    bool worker_done_{false};
};

} // namespace screen

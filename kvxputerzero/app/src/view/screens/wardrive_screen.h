#pragma once

#include "kvx_screen.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace screen {

// One captured network (or BLE device) shown as a table row.
struct WardriveRow {
    std::string key;
    std::string name;
    std::string channel;
    std::string rssi;
    std::string mark;
    std::vector<std::pair<std::string, std::string>> details;
};

// Live wardrive table. The worker keeps scanning; Enter or a row click expands that network.
class WardriveScreen final : public KvxScreen {
public:
    using RowsFn = std::function<void(std::vector<WardriveRow>)>;
    using StatusFn = std::function<void(const std::string&)>;
    using WorkerFn = std::function<void(std::atomic<bool>& cancel, const RowsFn& set_rows, const StatusFn& set_status)>;
    // Optional letter-key handler. Return true if consumed. Called for printable keys.
    using KeyFn = std::function<bool(uint32_t key)>;
    // Called after the worker joins on leave/home (session summary, etc.).
    using DoneFn = std::function<void()>;

    WardriveScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                   std::string name_column, WorkerFn worker, KeyFn on_key = {}, DoneFn on_done = {});
    ~WardriveScreen() override;

    void flush_pending();

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    struct Cols {
        lv_obj_t* row{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* channel{nullptr};
        lv_obj_t* rssi{nullptr};
        lv_obj_t* mark{nullptr};
    };

    static void on_row_clicked(lv_event_t* event);
    Cols make_cols(lv_obj_t* parent, int width);
    void paint_row(const Cols& cols, const WardriveRow* data, bool selected);
    void refresh_table();
    void move_selection(int delta);
    void expand_selected();
    void collapse();
    void rebuild_detail();
    void leave();
    void go_home();
    void stop_worker();
    void arm_display_blank();
    void cancel_display_blank();
    void blank_display();
    void wake_display();
    static void blank_timer_cb(lv_timer_t* timer);
    static bool on_early_key(uint32_t key, const char* key_name, bool long_pressed, void* user_data);
    static void on_key_up(uint32_t key, const char* key_name, void* user_data);

    std::string title_;
    std::string name_column_;
    WorkerFn worker_;
    KeyFn on_key_;
    DoneFn on_done_;
    bool done_fired_{false};

    lv_obj_t* status_label_{nullptr};
    lv_obj_t* header_{nullptr};
    lv_obj_t* list_{nullptr};
    lv_obj_t* empty_label_{nullptr};
    lv_obj_t* detail_{nullptr};
    std::vector<Cols> rows_ui_;
    std::vector<std::string> detail_labels_;
    std::vector<lv_obj_t*> detail_values_;
    lv_timer_t* poll_timer_{nullptr};
    const lv_font_t* font_{nullptr};

    std::atomic<bool> cancel_{false};
    std::thread worker_thread_;
    std::mutex mu_;
    std::string status_;
    std::vector<WardriveRow> pending_rows_;
    bool rows_dirty_{false};
    bool worker_done_{false};

    std::vector<WardriveRow> rows_;
    int selected_{0};
    bool selection_pinned_{false};
    std::string selected_key_;
    bool expanded_{false};
    std::string expanded_key_;

    lv_obj_t* blank_cover_{nullptr};
    lv_timer_t* blank_timer_{nullptr};
    bool display_off_{false};
    int32_t saved_brightness_{70};
    bool saved_brightness_valid_{false};
    uint32_t swallow_long_{0};
};

} // namespace screen

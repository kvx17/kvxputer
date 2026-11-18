#pragma once

#include "kvx_screen.h"
#include "kvx_style.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

struct InfoField {
    std::string label;
    std::string value;
};

// Static status / probe card. Worker fills fields asynchronously; R re-runs.
class InfoScreen final : public KvxScreen {
public:
    using Fields = std::vector<InfoField>;
    using WorkerFn = std::function<void(std::atomic<bool>& cancel,
                                        Fields& out_fields,
                                        std::string& out_status,
                                        view::StatusPillKind& out_kind)>;

    // Synchronous constructor for already-known fields (confirmations, static info).
    InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
               Fields fields, std::string status = "OK",
               view::StatusPillKind kind = view::StatusPillKind::Ok);

    // Async probe constructor.
    InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
               WorkerFn worker);

    ~InfoScreen() override;

    void flush_pending();

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void start_worker();
    void stop_worker();
    void rebuild_fields(const Fields& fields);
    void apply_status(const std::string& status, view::StatusPillKind kind);

    std::string title_;
    WorkerFn worker_;
    Fields initial_fields_;
    std::string initial_status_;
    view::StatusPillKind initial_kind_{view::StatusPillKind::Idle};

    lv_obj_t* pill_{nullptr};
    lv_obj_t* panel_{nullptr};
    lv_obj_t* status_line_{nullptr};
    std::vector<lv_obj_t*> field_rows_;
    lv_timer_t* poll_timer_{nullptr};
    const lv_font_t* font_{nullptr};

    std::atomic<bool> cancel_{false};
    std::thread worker_thread_;
    std::mutex mu_;
    Fields pending_fields_;
    std::string pending_status_;
    view::StatusPillKind pending_kind_{view::StatusPillKind::Busy};
    bool pending_ready_{false};
    bool worker_done_{false};
    bool has_worker_{false};
};

} // namespace screen

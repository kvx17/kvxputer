#include "info_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {
namespace {

void poll_cb(lv_timer_t* t) {
    auto* self = static_cast<InfoScreen*>(lv_timer_get_user_data(t));
    if (self) self->flush_pending();
}

} // namespace

InfoScreen::InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                       Fields fields, std::string status, view::StatusPillKind kind)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      initial_fields_(std::move(fields)),
      initial_status_(std::move(status)),
      initial_kind_(kind),
      has_worker_(false),
      live_(false) {
    init();
}

InfoScreen::InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                       WorkerFn worker)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      worker_(std::move(worker)),
      initial_status_("probing..."),
      initial_kind_(view::StatusPillKind::Busy),
      has_worker_(true),
      live_(false) {
    init();
}

InfoScreen::InfoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                       LiveWorkerFn live_worker)
    : KvxScreen(view_model, assets),
      title_(std::move(title)),
      live_worker_(std::move(live_worker)),
      initial_status_("live..."),
      initial_kind_(view::StatusPillKind::Busy),
      has_worker_(true),
      live_(true) {
    init();
}

InfoScreen::~InfoScreen() {
    stop_worker();
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void InfoScreen::build() {
    make_header(content(), title_.c_str());
    const auto tok = tokens();
    font_ = assets().load_standard_font(12);
    if (!font_) font_ = &lv_font_montserrat_12;

    pill_ = view::make_status_pill(content(), initial_status_.c_str(), initial_kind_, font_, tok);
    lv_obj_align(pill_, LV_ALIGN_TOP_LEFT, 4, 20);
    status_line_ = nullptr;

    panel_ = view::make_panel(content(), 36, tok);
    if (!has_worker_ || !initial_fields_.empty()) {
        rebuild_fields(initial_fields_);
    }

    if (has_worker_) {
        poll_timer_ = lv_timer_create(poll_cb, 80, this);
        start_worker();
    }
}

void InfoScreen::rebuild_fields(const Fields& fields) {
    if (!panel_) return;
    for (auto* row : field_rows_) {
        if (row) lv_obj_delete(row);
    }
    field_rows_.clear();
    const auto tok = tokens();
    if (fields.empty()) {
        auto* row = view::make_kv_row(panel_, "—", "no data", font_, tok);
        field_rows_.push_back(row);
        return;
    }
    for (const auto& f : fields) {
        field_rows_.push_back(view::make_kv_row(panel_, f.label.c_str(), f.value.c_str(), font_, tok));
    }
}

void InfoScreen::apply_status(const std::string& status, view::StatusPillKind kind) {
    const auto tok = tokens();
    view::set_status_pill(pill_, status.c_str(), kind, tok);
}

void InfoScreen::start_worker() {
    if (!worker_ && !live_worker_) return;
    stop_worker();
    cancel_ = false;
    worker_done_ = false;
    pending_ready_ = false;
    apply_status(live_ ? "live..." : "probing...", view::StatusPillKind::Busy);
    worker_thread_ = std::thread([this]() {
        if (live_ && live_worker_) {
            PublishFn publish = [this](Fields fields, std::string status, view::StatusPillKind kind) {
                std::lock_guard<std::mutex> lock(mu_);
                pending_fields_ = std::move(fields);
                pending_status_ = std::move(status);
                pending_kind_ = kind;
                pending_ready_ = true;
            };
            try {
                live_worker_(cancel_, publish);
            } catch (...) {
                publish({}, "error", view::StatusPillKind::Error);
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (pending_status_.empty()) pending_status_ = cancel_ ? "cancelled" : "done";
                worker_done_ = true;
                pending_ready_ = true;
            }
            return;
        }
        Fields fields;
        std::string status = "done";
        view::StatusPillKind kind = view::StatusPillKind::Ok;
        try {
            worker_(cancel_, fields, status, kind);
        } catch (...) {
            status = "error";
            kind = view::StatusPillKind::Error;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            pending_fields_ = std::move(fields);
            pending_status_ = status.empty() ? (cancel_ ? "cancelled" : "done") : status;
            pending_kind_ = kind;
            pending_ready_ = true;
            worker_done_ = true;
        }
    });
}

void InfoScreen::stop_worker() {
    cancel_ = true;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void InfoScreen::flush_pending() {
    Fields fields;
    std::string status;
    view::StatusPillKind kind = view::StatusPillKind::Idle;
    bool ready = false;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ready = pending_ready_;
        if (ready) {
            fields = pending_fields_;
            status = pending_status_;
            kind = pending_kind_;
            pending_ready_ = false;
        }
        done = worker_done_;
    }
    if (ready) {
        apply_status(status, kind);
        rebuild_fields(fields);
    }
    if (done && worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool InfoScreen::handle_key(uint32_t key) {
    if ((key == 'r' || key == 'R') && has_worker_ && !live_) {
        start_worker();
        return true;
    }
    if (key == LV_KEY_DOWN) {
        if (panel_) lv_obj_scroll_by(panel_, 0, -14, LV_ANIM_OFF);
        return true;
    }
    if (key == LV_KEY_UP) {
        if (panel_) lv_obj_scroll_by(panel_, 0, 14, LV_ANIM_OFF);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> InfoScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP,
         [this]() {
             if (panel_) lv_obj_scroll_by(panel_, 0, 14, LV_ANIM_OFF);
         }},
        {!live_ && has_worker_ ? view::ICON_MENU_RESET : "",
         !live_ && has_worker_ ? std::function<void()>([this]() { start_worker(); }) : std::function<void()>()},
        {view::ICON_CARET_DOWN,
         [this]() {
             if (panel_) lv_obj_scroll_by(panel_, 0, -14, LV_ANIM_OFF);
         }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

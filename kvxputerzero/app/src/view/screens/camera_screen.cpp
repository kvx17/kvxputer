#include "camera_screen.h"

#include "asset_manager.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace screen {
namespace {

const char* guide_name(int mode) {
    switch (mode) {
    case 0: return "guides off";
    case 1: return "grid";
    case 2: return "diamond";
    case 3: return "lines";
    default: return "all guides";
    }
}

} // namespace

CameraScreen::CameraScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

CameraScreen::~CameraScreen() {
    stop_session();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
    }
    if (draw_) {
        lv_draw_buf_destroy(draw_);
        draw_ = nullptr;
    }
}

void CameraScreen::stop_session() {
    session_.stop();
}

void CameraScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    plot_w_ = view::kScreenWidth;
    plot_h_ = view::kContentHeight;

    draw_ = lv_draw_buf_create(plot_w_, plot_h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_size(canvas_, plot_w_, plot_h_);
        lv_obj_align(canvas_, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);
    }

    guide_layer_ = lv_obj_create(root);
    lv_obj_remove_style_all(guide_layer_);
    lv_obj_set_size(guide_layer_, plot_w_, plot_h_);
    lv_obj_align(guide_layer_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(guide_layer_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(guide_layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(guide_layer_, LV_OBJ_FLAG_EVENT_BUBBLE);

    hud_ = lv_obj_create(root);
    lv_obj_remove_style_all(hud_);
    lv_obj_set_size(hud_, plot_w_, 28);
    lv_obj_align(hud_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hud_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hud_, LV_OPA_60, 0);

    auto* font = assets().load_standard_font(11);
    filter_lbl_ = lv_label_create(hud_);
    lv_obj_set_style_text_color(filter_lbl_, tok.ink, 0);
    if (font) lv_obj_set_style_text_font(filter_lbl_, font, 0);
    lv_obj_align(filter_lbl_, LV_ALIGN_LEFT_MID, 4, 0);

    rec_lbl_ = lv_label_create(hud_);
    lv_obj_set_style_text_color(rec_lbl_, lv_color_hex(0xFF4444), 0);
    if (font) lv_obj_set_style_text_font(rec_lbl_, font, 0);
    lv_label_set_text(rec_lbl_, "");
    lv_obj_align(rec_lbl_, LV_ALIGN_RIGHT_MID, -4, 0);

    status_lbl_ = lv_label_create(root);
    lv_obj_set_style_text_color(status_lbl_, tok.accent, 0);
    if (font) lv_obj_set_style_text_font(status_lbl_, font, 0);
    lv_obj_align(status_lbl_, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_label_set_text(status_lbl_, "");

    std::string err;
    if (!session_.start(err)) {
        last_msg_ = err.empty() ? "camera unavailable" : err;
    } else if (session_.backend_name() == "simulation") {
        last_msg_ = "Simulation";
    } else {
        last_msg_ = session_.backend_name();
    }
    update_hud();
    rebuild_guides();

    timer_ = lv_timer_create(timer_cb, 80, this);
    build_help();
}

void CameraScreen::build_help() {
    help_ = lv_obj_create(root());
    lv_obj_remove_style_all(help_);
    lv_obj_set_size(help_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(help_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(help_, LV_OPA_60, 0);
    lv_obj_add_flag(help_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(help_, LV_OBJ_FLAG_SCROLLABLE);

    const auto tok = tokens();
    auto* panel = lv_obj_create(help_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 300, 156);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, tok.surface, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, tok.accent, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    auto* title_font = assets().load_standard_font(14);
    auto* row_font = assets().load_standard_font(11);
    auto* title = lv_label_create(panel);
    lv_label_set_text(title, "Camera");
    lv_obj_set_style_text_color(title, tok.ink, 0);
    if (title_font) lv_obj_set_style_text_font(title, title_font, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto* close = lv_label_create(panel);
    lv_label_set_text(close, "H / 4 close");
    lv_obj_set_style_text_color(close, tok.accent, 0);
    if (row_font) lv_obj_set_style_text_font(close, row_font, 0);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 2);

    help_list_ = lv_obj_create(panel);
    lv_obj_remove_style_all(help_list_);
    lv_obj_set_size(help_list_, 284, 124);
    lv_obj_align(help_list_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(help_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(help_list_, 1, 0);
    lv_obj_set_scroll_dir(help_list_, LV_DIR_VER);
    lv_obj_add_flag(help_list_, LV_OBJ_FLAG_SCROLLABLE);
    auto* list = help_list_;

    static constexpr const char* kRows[] = {
        "Z / Left     Previous filter",
        "C / Right    Next filter",
        "F / Up       Dimmer",
        "X / Down     Brighter",
        "6 / Enter    Take photo",
        "D            Photo details footer",
        "R            Record / stop video",
        "G            Framing guides",
        "1            Hide chrome",
        "8            Open gallery",
        "4 / Esc      Back",
        "H            This help",
        "Photos       ~/Pictures/DCIM/Camera",
        "Video        ~/Videos/Camera",
        "Gallery      Arrows move, 6 opens",
        "             5/7 zoom, 8 resets",
        "             P photos, V videos",
        "Help         Up/Down scroll",
    };
    for (const char* text : kRows) {
        auto* row = lv_label_create(list);
        lv_label_set_text(row, text);
        lv_obj_set_style_text_color(row, tok.ink, 0);
        if (row_font) lv_obj_set_style_text_font(row, row_font, 0);
        lv_obj_set_width(row, 276);
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
    }
}

void CameraScreen::set_help(bool open) {
    help_open_ = open;
    if (!help_) return;
    if (open) {
        lv_obj_clear_flag(help_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(help_);
    } else {
        lv_obj_add_flag(help_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CameraScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<CameraScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void CameraScreen::on_timer() {
    draw_preview();
    update_hud();
}

void CameraScreen::update_fit(int frame_w, int frame_h) {
    int view_w = plot_w_;
    int view_h = plot_h_;
    if (draw_ && draw_->header.h > 0 && view_h > static_cast<int>(draw_->header.h)) {
        view_h = static_cast<int>(draw_->header.h);
    }
    if (draw_ && draw_->header.w > 0 && view_w > static_cast<int>(draw_->header.w)) {
        view_w = static_cast<int>(draw_->header.w);
    }
    fit_view_h_ = view_h;
    if (frame_w <= 0 || frame_h <= 0 || view_w <= 0 || view_h <= 0) {
        fit_x_ = 0;
        fit_y_ = 0;
        fit_w_ = view_w;
        fit_h_ = view_h;
        return;
    }
    int dw = view_w;
    int dh = frame_h * view_w / frame_w;
    if (dh > view_h) {
        dh = view_h;
        dw = std::max(1, frame_w * view_h / frame_h);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    fit_w_ = dw;
    fit_h_ = dh;
    fit_x_ = (view_w - dw) / 2;
    fit_y_ = (view_h - dh) / 2;
}

void CameraScreen::draw_preview() {
    if (!draw_ || !draw_->data || !canvas_) return;
    platform::CameraFrame frame;
    if (!session_.copy_preview(frame) || frame.width <= 0 || frame.height <= 0) return;
    if (frame.rgb565.size() < static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height)) return;

    if (frame.width != preview_w_ || frame.height != preview_h_ || plot_h_ != fitted_plot_h_) {
        preview_w_ = frame.width;
        preview_h_ = frame.height;
        fitted_plot_h_ = plot_h_;
        update_fit(preview_w_, preview_h_);
        rebuild_guides();
    }

    auto* dst = static_cast<uint8_t*>(draw_->data);
    const int view_h = fit_view_h_ > 0 ? fit_view_h_ : plot_h_;
    std::memset(dst, 0, static_cast<size_t>(stride_ * view_h));
    for (int y = 0; y < fit_h_; ++y) {
        const int sy = y * frame.height / fit_h_;
        const uint16_t* src = frame.rgb565.data() + static_cast<size_t>(sy) * static_cast<size_t>(frame.width);
        auto* row = reinterpret_cast<uint16_t*>(dst + static_cast<size_t>(fit_y_ + y) * static_cast<size_t>(stride_));
        for (int x = 0; x < fit_w_; ++x) {
            const int sx = x * frame.width / fit_w_;
            row[fit_x_ + x] = src[sx];
        }
    }
    lv_obj_invalidate(canvas_);
}

void CameraScreen::update_hud() {
    if (filter_lbl_) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s  br %.1f  %s%s", tools::camfilter::filter_name(session_.filter()),
                      session_.brightness(), guide_name(static_cast<int>(guide_)),
                      session_.details_footer() ? "  det" : "");
        lv_label_set_text(filter_lbl_, buf);
    }
    if (rec_lbl_) {
        lv_label_set_text(rec_lbl_, session_.recording() ? "REC" : "");
    }
    if (status_lbl_) {
        lv_label_set_text(status_lbl_, last_msg_.c_str());
    }
}

void CameraScreen::rebuild_guides() {
    if (!guide_layer_) return;
    lv_obj_clean(guide_layer_);
    const auto accent = sec_color();
    const int x0 = fit_x_;
    const int y0 = fit_y_;
    const int w = fit_w_ > 0 ? fit_w_ : plot_w_;
    const int h = fit_h_ > 0 ? fit_h_ : plot_h_;
    auto add_line = [&](int x1, int y1, int x2, int y2) {
        auto* line = lv_line_create(guide_layer_);
        static thread_local lv_point_precise_t pts[2];
        // Each line needs its own points storage; use heap via user data.
        auto* p = new lv_point_precise_t[2]{{static_cast<lv_value_precise_t>(x1), static_cast<lv_value_precise_t>(y1)},
                                            {static_cast<lv_value_precise_t>(x2), static_cast<lv_value_precise_t>(y2)}};
        lv_line_set_points(line, p, 2);
        lv_obj_add_event_cb(
            line,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                    delete[] static_cast<lv_point_precise_t*>(lv_event_get_user_data(e));
                }
            },
            LV_EVENT_DELETE, p);
        lv_obj_set_style_line_color(line, accent, 0);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_opa(line, LV_OPA_70, 0);
        (void)pts;
    };

    const bool grid = guide_ == GuideMode::Grid || guide_ == GuideMode::All;
    const bool diamond = guide_ == GuideMode::Diamond || guide_ == GuideMode::All;
    const bool lines = guide_ == GuideMode::Lines || guide_ == GuideMode::All;

    if (grid) {
        add_line(x0 + w / 3, y0, x0 + w / 3, y0 + h);
        add_line(x0 + 2 * w / 3, y0, x0 + 2 * w / 3, y0 + h);
        add_line(x0, y0 + h / 3, x0 + w, y0 + h / 3);
        add_line(x0, y0 + 2 * h / 3, x0 + w, y0 + 2 * h / 3);
    }
    if (diamond) {
        add_line(x0 + w / 2, y0, x0 + w, y0 + h / 2);
        add_line(x0 + w, y0 + h / 2, x0 + w / 2, y0 + h);
        add_line(x0 + w / 2, y0 + h, x0, y0 + h / 2);
        add_line(x0, y0 + h / 2, x0 + w / 2, y0);
    }
    if (lines) {
        add_line(x0 + w / 2, y0, x0 + w / 2, y0 + h);
        add_line(x0, y0 + h / 2, x0 + w, y0 + h / 2);
    }
}

void CameraScreen::cycle_guide() {
    guide_ = static_cast<GuideMode>((static_cast<int>(guide_) + 1) % 5);
    rebuild_guides();
    last_msg_ = guide_name(static_cast<int>(guide_));
}

void CameraScreen::adjust_brightness(float delta) {
    session_.set_brightness(session_.brightness() + delta);
    last_msg_ = "brightness";
}

void CameraScreen::take_photo() {
    std::string path, err;
    int w = 0;
    int h = 0;
    if (session_.capture_still(path, err, &w, &h)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "saved %dx%d", w, h);
        last_msg_ = buf;
    } else {
        last_msg_ = err.empty() ? "capture failed" : err;
    }
}

void CameraScreen::toggle_record() {
    if (session_.recording()) {
        session_.stop_recording();
        last_msg_ = session_.record_path().empty() ? "stopped" : session_.record_path();
        return;
    }
    std::string path, err;
    if (session_.start_recording(path, err)) {
        last_msg_ = std::string("REC ") + path;
    } else {
        last_msg_ = err.empty() ? "record failed" : err;
    }
}

std::array<view::widgets::ActionSlot, 5> CameraScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_LEFT, [this]() {
             session_.set_filter(tools::camfilter::step_filter(session_.filter(), -1));
         }},
        {view::ICON_CHECK, [this]() { take_photo(); }},
        {view::ICON_ARROW_RIGHT, [this]() {
             session_.set_filter(tools::camfilter::step_filter(session_.filter(), 1));
         }},
        {view::ICON_CH_GALLERY, [this]() { view_model().show_page(model::AppPage::Gallery); }},
    }};
}

bool CameraScreen::handle_key(uint32_t key) {
    if (help_open_) {
        if (help_list_ && (key == LV_KEY_DOWN || key == LV_KEY_RIGHT)) {
            lv_obj_scroll_by(help_list_, 0, -18, LV_ANIM_OFF);
            return true;
        }
        if (help_list_ && (key == LV_KEY_UP || key == LV_KEY_LEFT)) {
            lv_obj_scroll_by(help_list_, 0, 18, LV_ANIM_OFF);
            return true;
        }
        if (key == 'h' || key == 'H' || key == '4' || key == LV_KEY_ESC || key == LV_KEY_ENTER || key == '6') {
            set_help(false);
        }
        return true;
    }
    if (key == 'h' || key == 'H') {
        set_help(true);
        return true;
    }
    if (key == LV_KEY_ESC || key == '4') {
        session_.stop_recording();
        stop_session();
        view_model().go_back();
        return true;
    }
    if (key == LV_KEY_LEFT) {
        session_.set_filter(tools::camfilter::step_filter(session_.filter(), -1));
        return true;
    }
    if (key == LV_KEY_RIGHT || key == 'j' || key == 'J') {
        session_.set_filter(tools::camfilter::step_filter(session_.filter(), 1));
        return true;
    }
    // Bare F/X become LV_KEY_UP/DOWN; plan: F dims, X brightens.
    if (key == LV_KEY_UP || key == 'f' || key == 'F') {
        adjust_brightness(-0.1f);
        return true;
    }
    if (key == LV_KEY_DOWN || key == 'x' || key == 'X') {
        adjust_brightness(0.1f);
        return true;
    }
    if (key == LV_KEY_ENTER || key == '\n' || key == ' ' || key == '6') {
        take_photo();
        return true;
    }
    if (key == 'r' || key == 'R') {
        toggle_record();
        return true;
    }
    if (key == 'g' || key == 'G') {
        cycle_guide();
        return true;
    }
    if (key == 'd' || key == 'D') {
        session_.set_details_footer(!session_.details_footer());
        last_msg_ = session_.details_footer() ? "photo details on" : "photo details off";
        return true;
    }
    if (key == '1') {
        chrome_on_ = !chrome_on_;
        set_chrome_visible(chrome_on_);
        if (hud_) {
            if (chrome_on_) lv_obj_clear_flag(hud_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(hud_, LV_OBJ_FLAG_HIDDEN);
        }
        if (status_lbl_) {
            if (chrome_on_) lv_obj_clear_flag(status_lbl_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(status_lbl_, LV_OBJ_FLAG_HIDDEN);
        }
        plot_h_ = chrome_on_ ? view::kContentHeight : view::kScreenHeight;
        if (canvas_) lv_obj_set_size(canvas_, plot_w_, plot_h_);
        if (guide_layer_) lv_obj_set_size(guide_layer_, plot_w_, plot_h_);
        update_fit(preview_w_, preview_h_);
        fitted_plot_h_ = plot_h_;
        rebuild_guides();
        return true;
    }
    if (key == '8') {
        session_.stop_recording();
        stop_session();
        view_model().show_page(model::AppPage::Gallery);
        return true;
    }
    return false;
}

} // namespace screen

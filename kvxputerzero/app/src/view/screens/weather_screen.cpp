#include "weather_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace screen {
namespace {

const char* kWeekday[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

std::tm local_tm(int64_t unix_utc, int offset) {
    std::time_t local = static_cast<std::time_t>(unix_utc + offset);
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &local);
#else
    gmtime_r(&local, &out);
#endif
    return out;
}

uint16_t pack565(lv_color_t c) {
    return static_cast<uint16_t>(((c.red & 0xF8) << 8) | ((c.green & 0xFC) << 3) | (c.blue >> 3));
}
void put_px(uint8_t* d, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(d + y * stride)[x] = c;
}
void draw_line(uint8_t* d, int stride, int w, int h, int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put_px(d, stride, w, h, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

const char* kFaceName[8] = {"CONDITIONS", "TEMPERATURE", "PRECIP",  "WIND",
                            "PRESSURE",   "HUMIDITY",    "UV INDEX", "SUN & MOON"};

const char* moon_phase(int64_t unix_utc) {
    // Days since a known new moon (2000-01-06 18:14 UTC), mod synodic month.
    const double synodic = 29.530588853;
    double days = (static_cast<double>(unix_utc) - 947182440.0) / 86400.0;
    double age = std::fmod(days, synodic);
    if (age < 0) age += synodic;
    const int idx = static_cast<int>(age / synodic * 8.0 + 0.5) % 8;
    static const char* kPhase[8] = {"New Moon",    "Waxing Cres", "First Qtr", "Waxing Gib",
                                    "Full Moon", "Waning Gib",  "Last Qtr",  "Waning Cres"};
    return kPhase[idx];
}

} // namespace

WeatherScreen::WeatherScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    cfg_ = platform::wx_load_config();
    // Show cached data instantly if present for the active city.
    const auto& c = cfg_.cities[static_cast<size_t>(cfg_.active)];
    const std::string cached = platform::wx_load_cache(c.lat, c.lon);
    if (!cached.empty()) {
        std::string err;
        platform::Weather w = platform::wx_parse(cached, err);
        if (w.valid) wx_ = w;
    }
    init();
}

WeatherScreen::~WeatherScreen() {
    stop_worker();
    if (poll_) lv_timer_delete(poll_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void WeatherScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Weather");

    font_ = assets().load_standard_font(13);
    small_font_ = assets().load_standard_font(11);
    icon_font_ = assets().load_font("Phosphor-Fill.ttf", 30);
    const lv_font_t* slot_icon_font = assets().load_font("Phosphor-Fill.ttf", 16);
    auto* big = assets().load_standard_font(30);

    chip_ = view::widgets::make_status_chip(root, "...", view::StatusPillKind::Busy, small_font_, tok);

    // Current block (below the make_header row, which owns content y0..18).
    auto* big26 = assets().load_standard_font(26);
    temp_ = view::widgets::make_readout(root, big26 ? big26 : big, tok);
    lv_obj_set_style_text_color(temp_, tok.ink, 0);
    lv_obj_set_width(temp_, 92);
    lv_obj_set_pos(temp_, 6, 20);

    cond_icon_ = lv_label_create(root);
    lv_obj_set_style_text_font(cond_icon_, icon_font_ ? icon_font_ : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cond_icon_, tok.accent, 0);
    lv_obj_set_pos(cond_icon_, 98, 22);

    city_ = lv_label_create(root);
    lv_obj_set_style_text_font(city_, font_ ? font_ : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(city_, tok.accent, 0);
    lv_label_set_long_mode(city_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(city_, 182);
    lv_obj_set_pos(city_, 132, 21);

    cond_text_ = lv_label_create(root);
    lv_obj_set_style_text_font(cond_text_, font_ ? font_ : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cond_text_, tok.ink, 0);
    lv_label_set_long_mode(cond_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(cond_text_, 182);
    lv_obj_set_pos(cond_text_, 132, 37);

    sub_ = lv_label_create(root);
    lv_obj_set_style_text_font(sub_, small_font_ ? small_font_ : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub_, tok.ink_dim, 0);
    lv_label_set_long_mode(sub_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sub_, 308);
    lv_obj_set_pos(sub_, 6, 52);

    face_label_ = lv_label_create(root);
    lv_obj_set_style_text_font(face_label_, font_ ? font_ : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(face_label_, tok.accent, 0);
    lv_obj_set_width(face_label_, view::kScreenWidth - 12);
    lv_obj_set_style_text_align(face_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(face_label_, 6, 64);

    caption_ = lv_label_create(root);
    lv_obj_set_style_text_font(caption_, small_font_ ? small_font_ : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(caption_, tok.ink_dim, 0);
    lv_obj_set_pos(caption_, 6, 82);

    // Body: a canvas graph (Temperature) over the same region as the slot strip
    // (Conditions). Only one is visible at a time.
    const int body_y = 94;
    cw_ = 308;
    ch_ = view::kContentHeight - body_y;
    if (ch_ < 24) ch_ = 24;
    draw_ = lv_draw_buf_create(cw_, ch_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 6, body_y);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    const int slot_w = 50;
    for (int i = 0; i < kSlots; ++i) {
        const int x = 6 + i * 51;
        slot_lbl_[i] = lv_label_create(root);
        lv_obj_set_style_text_font(slot_lbl_[i], small_font_ ? small_font_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(slot_lbl_[i], tok.ink_dim, 0);
        lv_obj_set_width(slot_lbl_[i], slot_w);
        lv_obj_set_style_text_align(slot_lbl_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(slot_lbl_[i], x, body_y);

        slot_icon_[i] = lv_label_create(root);
        lv_obj_set_style_text_font(slot_icon_[i],
                                   slot_icon_font ? slot_icon_font : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(slot_icon_[i], tok.accent, 0);
        lv_obj_set_width(slot_icon_[i], slot_w);
        lv_obj_set_style_text_align(slot_icon_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(slot_icon_[i], x, body_y + 12);

        slot_val_[i] = lv_label_create(root);
        lv_obj_set_style_text_font(slot_val_[i], small_font_ ? small_font_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(slot_val_[i], tok.ink, 0);
        lv_obj_set_width(slot_val_[i], slot_w);
        lv_obj_set_style_text_align(slot_val_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(slot_val_[i], x, body_y + 28);
    }

    update_current();
    redraw();

    poll_ = lv_timer_create(poll_cb, 120, this);
    start_fetch();
}

void WeatherScreen::update_status(const char* text, view::StatusPillKind kind) {
    if (chip_) view::set_status_pill(chip_, text, kind, tokens());
}

void WeatherScreen::update_current() {
    const auto tok = tokens();
    const auto& c = cfg_.cities[static_cast<size_t>(cfg_.active)];
    char buf[96];

    if (cfg_.cities.size() > 1) {
        std::snprintf(buf, sizeof(buf), "%s  %d/%d", c.name.c_str(), cfg_.active + 1,
                      static_cast<int>(cfg_.cities.size()));
    } else {
        std::snprintf(buf, sizeof(buf), "%s", c.name.c_str());
    }
    lv_label_set_text(city_, buf);

    if (!wx_.valid) {
        lv_label_set_text(temp_, "--°");
        lv_label_set_text(cond_icon_, "");
        lv_label_set_text(cond_text_, "No data");
        lv_label_set_text(sub_, "");
        return;
    }

    std::snprintf(buf, sizeof(buf), "%.0f°%s", static_cast<double>(wx_.temp), unit_suffix());
    lv_label_set_text(temp_, buf);
    lv_label_set_text(cond_icon_, platform::wx_condition_icon(wx_.weather_code, wx_.is_day));
    lv_label_set_text(cond_text_, platform::wx_condition_text(wx_.weather_code));

    float hi = wx_.temp, lo = wx_.temp;
    if (!wx_.daily.empty()) {
        hi = wx_.daily[0].t_max;
        lo = wx_.daily[0].t_min;
    }
    const char* wu = cfg_.celsius ? "km/h" : "mph";
    std::snprintf(buf, sizeof(buf), "Feels %.0f°   H %.0f°  L %.0f°   Wind %.0f %s",
                  static_cast<double>(wx_.apparent), static_cast<double>(hi),
                  static_cast<double>(lo), static_cast<double>(wx_.wind_speed), wu);
    lv_label_set_text(sub_, buf);
    (void)tok;
}

void WeatherScreen::redraw() {
    char fl[48];
    std::snprintf(fl, sizeof(fl), "<  %s  >    %d/%d", kFaceName[face_], face_ + 1, kFaceCount);
    lv_label_set_text(face_label_, fl);

    enum Body { Strip, Graph, Sun };
    const Body body = face_ == 0 ? Strip : (face_ == 7 ? Sun : Graph);
    const bool show_slots = body != Graph;
    for (int i = 0; i < kSlots; ++i) {
        if (show_slots) {
            lv_obj_clear_flag(slot_lbl_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(slot_icon_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(slot_val_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(slot_lbl_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(slot_icon_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(slot_val_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (canvas_) {
        if (body == Graph) lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }

    char cap[72];
    std::snprintf(cap, sizeof(cap), "%s", daily_mode_ ? "NEXT DAYS" : "NEXT HOURS");

    const std::time_t now = std::time(nullptr);
    const int hn = static_cast<int>(wx_.hourly.size());
    int hstart = 0;
    while (hstart < hn && wx_.hourly[hstart].time < now) ++hstart;
    const int dn = static_cast<int>(wx_.daily.size());

    if (body == Strip) {
        // Conditions: condition icon + temp per hour / day.
        for (int i = 0; i < kSlots; ++i) {
            char l[12], v[12];
            lv_label_set_text(slot_icon_[i], "");
            if (daily_mode_ && i < dn) {
                const std::tm t = local_tm(wx_.daily[i].time, wx_.utc_offset);
                std::snprintf(l, sizeof(l), "%s", kWeekday[t.tm_wday % 7]);
                std::snprintf(v, sizeof(v), "%.0f\xC2\xB0", static_cast<double>(wx_.daily[i].t_max));
                lv_label_set_text(slot_icon_[i],
                                  platform::wx_condition_icon(wx_.daily[i].weather_code, true));
            } else if (!daily_mode_ && hstart + i * 2 < hn) {
                const auto& h = wx_.hourly[hstart + i * 2];
                const std::tm t = local_tm(h.time, wx_.utc_offset);
                std::snprintf(l, sizeof(l), "%02dh", t.tm_hour);
                std::snprintf(v, sizeof(v), "%.0f\xC2\xB0", static_cast<double>(h.temp));
                lv_label_set_text(slot_icon_[i], platform::wx_condition_icon(
                                                     h.weather_code, t.tm_hour >= 7 && t.tm_hour < 20));
            } else {
                std::snprintf(l, sizeof(l), "-");
                std::snprintf(v, sizeof(v), "-");
            }
            lv_label_set_text(slot_lbl_[i], l);
            lv_label_set_text(slot_val_[i], v);
        }
    } else if (body == Sun) {
        for (int i = 0; i < kSlots; ++i) {
            lv_label_set_text(slot_icon_[i], "");
            lv_label_set_text(slot_lbl_[i], "");
            lv_label_set_text(slot_val_[i], "");
        }
        if (!daily_mode_ && dn > 0) {
            const auto& d0 = wx_.daily[0];
            const std::tm sr = local_tm(d0.sunrise, wx_.utc_offset);
            const std::tm ss = local_tm(d0.sunset, wx_.utc_offset);
            const int64_t daylen = d0.sunset - d0.sunrise;
            char v[12];
            lv_label_set_text(slot_lbl_[0], "Rise");
            std::snprintf(v, sizeof(v), "%02d:%02d", sr.tm_hour, sr.tm_min);
            lv_label_set_text(slot_val_[0], v);
            lv_label_set_text(slot_lbl_[1], "Set");
            std::snprintf(v, sizeof(v), "%02d:%02d", ss.tm_hour, ss.tm_min);
            lv_label_set_text(slot_val_[1], v);
            lv_label_set_text(slot_lbl_[2], "Day");
            std::snprintf(v, sizeof(v), "%lldh%02lld", static_cast<long long>(daylen / 3600),
                          static_cast<long long>((daylen % 3600) / 60));
            lv_label_set_text(slot_val_[2], v);
            std::snprintf(cap, sizeof(cap), "TODAY  up %lldh%02lld   Moon %s",
                          static_cast<long long>(daylen / 3600),
                          static_cast<long long>((daylen % 3600) / 60), moon_phase(now));
        } else {
            for (int i = 0; i < kSlots && i < dn; ++i) {
                const std::tm t = local_tm(wx_.daily[i].time, wx_.utc_offset);
                const std::tm sr = local_tm(wx_.daily[i].sunrise, wx_.utc_offset);
                char l[12], v[12];
                std::snprintf(l, sizeof(l), "%s", kWeekday[t.tm_wday % 7]);
                std::snprintf(v, sizeof(v), "%02d:%02d", sr.tm_hour, sr.tm_min);
                lv_label_set_text(slot_lbl_[i], l);
                lv_label_set_text(slot_val_[i], v);
            }
            std::snprintf(cap, sizeof(cap), "SUNRISE BY DAY");
        }
    } else if (canvas_ && draw_ && draw_->data) {
        // Metric graph (line or bars). Build the window series.
        float vals[14];
        int count = 0;
        bool bars = false;
        const char* unit = "";
        float fixed_max = 0.f;  // 0 = auto-scale from data
        const char* wu = cfg_.celsius ? "km/h" : "mph";

        auto day_mean = [&](int d, int which) -> float {  // which: 0 pressure, 1 humidity
            const int base = d * 24;
            float sum = 0.f;
            int c = 0;
            for (int k = 0; k < 24 && base + k < hn; ++k) {
                sum += which == 0 ? wx_.hourly[base + k].pressure : wx_.hourly[base + k].humidity;
                ++c;
            }
            return c ? sum / static_cast<float>(c) : 0.f;
        };

        for (int i = 0; (daily_mode_ ? i < dn : hstart + i < hn) && count < 14; ++i) {
            float v = 0.f;
            if (daily_mode_) {
                const auto& d = wx_.daily[i];
                switch (face_) {
                case 1: v = d.t_max; break;
                case 2: v = d.precip_prob; break;
                case 3: v = d.wind_max; break;
                case 4: v = day_mean(i, 0); break;
                case 5: v = day_mean(i, 1); break;
                case 6: v = d.uv_max; break;
                }
            } else {
                const auto& h = wx_.hourly[hstart + i];
                switch (face_) {
                case 1: v = h.temp; break;
                case 2: v = h.precip_prob; break;
                case 3: v = h.wind_speed; break;
                case 4: v = h.pressure; break;
                case 5: v = h.humidity; break;
                case 6: v = h.uv; break;
                }
            }
            vals[count++] = v;
        }
        switch (face_) {
        case 1: unit = "\xC2\xB0"; break;
        case 2: unit = "%"; bars = true; fixed_max = 100.f; break;
        case 3: unit = wu; break;
        case 4: unit = "hPa"; break;
        case 5: unit = "%"; fixed_max = 100.f; break;
        case 6: unit = ""; bars = true; fixed_max = 11.f; break;
        }

        const auto tok = tokens();
        const uint16_t bg = pack565(tok.bg);
        const uint16_t accent = pack565(tok.accent);
        const uint16_t grid = pack565(tok.rule);
        uint8_t* d = static_cast<uint8_t*>(draw_->data);
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) put_px(d, stride_, cw_, ch_, x, y, bg);

        if (count >= 1) {
            float vmin = vals[0], vmax = vals[0];
            for (int i = 1; i < count; ++i) {
                vmin = std::min(vmin, vals[i]);
                vmax = std::max(vmax, vals[i]);
            }
            const int pad = 4;
            const int gh = ch_ - 2 * pad;
            float lo = vmin, hi = vmax;
            if (bars || fixed_max > 0.f) {
                lo = 0.f;
                hi = fixed_max > 0.f ? fixed_max : vmax;
            }
            if (hi - lo < 1e-3f) hi = lo + 1.f;
            auto ypx = [&](float v) {
                return pad + static_cast<int>((hi - v) / (hi - lo) * gh);
            };
            const int ymid = pad + gh / 2;
            for (int x = 0; x < cw_; x += 6) put_px(d, stride_, cw_, ch_, x, ymid, grid);
            if (bars) {
                const int bw = std::max(2, cw_ / std::max(1, count) - 3);
                for (int i = 0; i < count; ++i) {
                    const int cx = count > 1 ? i * (cw_ - 1) / (count - 1) : cw_ / 2;
                    const int top = ypx(vals[i]);
                    for (int x = cx - bw / 2; x <= cx + bw / 2; ++x)
                        for (int y = top; y < ch_ - pad; ++y) put_px(d, stride_, cw_, ch_, x, y, accent);
                }
            } else if (count >= 2) {
                for (int i = 0; i + 1 < count; ++i) {
                    const int x0 = i * (cw_ - 1) / (count - 1);
                    const int x1 = (i + 1) * (cw_ - 1) / (count - 1);
                    draw_line(d, stride_, cw_, ch_, x0, ypx(vals[i]), x1, ypx(vals[i + 1]), accent);
                    draw_line(d, stride_, cw_, ch_, x0, ypx(vals[i]) + 1, x1, ypx(vals[i + 1]) + 1,
                              accent);
                }
            }
            std::snprintf(cap, sizeof(cap), "%s  now %.0f%s  (%.0f-%.0f)",
                          daily_mode_ ? "NEXT DAYS" : "NEXT HOURS", static_cast<double>(vals[0]), unit,
                          static_cast<double>(vmin), static_cast<double>(vmax));
        }
        lv_obj_invalidate(canvas_);
    }

    lv_label_set_text(caption_, cap);
}

void WeatherScreen::cycle_city(int delta) {
    const int n = static_cast<int>(cfg_.cities.size());
    if (n <= 1) return;
    cfg_.active = (cfg_.active + delta + n) % n;
    platform::wx_save_config(cfg_);
    // Show cache for the newly active city immediately, then fetch fresh.
    const auto& c = cfg_.cities[static_cast<size_t>(cfg_.active)];
    wx_ = {};
    const std::string cached = platform::wx_load_cache(c.lat, c.lon);
    if (!cached.empty()) {
        std::string err;
        platform::Weather w = platform::wx_parse(cached, err);
        if (w.valid) wx_ = w;
    }
    update_current();
    redraw();
    start_fetch();
}

void WeatherScreen::start_fetch() {
    if (fetching_) return;
    stop_worker();
    cancel_ = false;
    pending_ready_ = false;
    worker_done_ = false;
    fetching_ = true;
    update_status("updating", view::StatusPillKind::Busy);

    const auto c = cfg_.cities[static_cast<size_t>(cfg_.active)];
    const bool celsius = cfg_.celsius;
    worker_ = std::thread([this, c, celsius]() {
        std::string err;
        platform::Weather w = platform::fetch_weather(c.lat, c.lon, celsius, err);
        std::lock_guard<std::mutex> lock(mu_);
        pending_ = std::move(w);
        pending_err_ = std::move(err);
        pending_ready_ = true;
        worker_done_ = true;
    });
}

void WeatherScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    fetching_ = false;
}

void WeatherScreen::poll_cb(lv_timer_t* t) {
    auto* self = static_cast<WeatherScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_poll();
}

void WeatherScreen::on_poll() {
    bool ready = false;
    bool done = false;
    platform::Weather w;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ready = pending_ready_;
        if (ready) {
            w = pending_;
            err = pending_err_;
            pending_ready_ = false;
        }
        done = worker_done_;
    }
    if (ready) {
        if (w.valid) {
            wx_ = w;
            last_age_shown_ = 0;
            update_status("live", view::StatusPillKind::Ok);
            update_current();
            redraw();
        } else if (wx_.valid) {
            update_status("offline", view::StatusPillKind::Missing);  // keep showing cached data
        } else {
            update_status("no signal", view::StatusPillKind::Error);
        }
    }
    if (done && worker_.joinable()) {
        worker_.join();
        fetching_ = false;
    }

    // Auto-refresh on the configured interval, and keep the "age" chip current.
    if (wx_.valid && !fetching_ && wx_.fetched_at > 0) {
        const int64_t age = static_cast<int64_t>(std::time(nullptr)) - wx_.fetched_at;
        if (cfg_.refresh_min > 0 && age >= static_cast<int64_t>(cfg_.refresh_min) * 60) {
            start_fetch();
        } else {
            const int64_t m = age / 60;
            if (m != last_age_shown_) {
                last_age_shown_ = m;
                char b[16];
                if (age < 60) std::snprintf(b, sizeof(b), "now");
                else if (m < 60) std::snprintf(b, sizeof(b), "%lldm", static_cast<long long>(m));
                else std::snprintf(b, sizeof(b), "%lldh", static_cast<long long>(m / 60));
                update_status(b, m < 90 ? view::StatusPillKind::Ok : view::StatusPillKind::Missing);
            }
        }
    }
}

bool WeatherScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) return KvxScreen::handle_key(key);
    if (key == LV_KEY_LEFT) {
        face_ = (face_ - 1 + kFaceCount) % kFaceCount;
        redraw();
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        face_ = (face_ + 1) % kFaceCount;
        redraw();
        return true;
    }
    if (key == LV_KEY_UP) {
        cycle_city(-1);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        cycle_city(1);
        return true;
    }
    if (key == platform::kKeyTab || key == LV_KEY_NEXT || key == LV_KEY_ENTER) {
        daily_mode_ = !daily_mode_;
        redraw();
        return true;
    }
    if (key == 'r' || key == 'R') {
        start_fetch();
        return true;
    }
    if (key == 's' || key == 'S') {
        view_model().open_weather_settings();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> WeatherScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() { face_ = (face_ - 1 + kFaceCount) % kFaceCount; redraw(); }},
        {view::ICON_CH_CONFIG, [this]() { view_model().open_weather_settings(); }},
        {view::ICON_ARROW_RIGHT, [this]() { face_ = (face_ + 1) % kFaceCount; redraw(); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

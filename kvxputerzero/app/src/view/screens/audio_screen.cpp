#include "audio_screen.h"

#include "asset_manager.h"
#include "audio.h"
#include "dsp_fft.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"
#include "user_paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace screen {
namespace {

constexpr int kRate = 16000;
constexpr int kChunkMs = 64;
constexpr int kBands = 32;

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void put_px(uint8_t* data, int stride, int w, int h, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    reinterpret_cast<uint16_t*>(data + y * stride)[x] = c;
}

void vline(uint8_t* data, int stride, int w, int h, int x, int y0, int y1, uint16_t c) {
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) put_px(data, stride, w, h, x, y, c);
}

const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                              "F#", "G",  "G#", "A",  "A#", "B"};

void note_for_freq(float hz, std::string& name, int& cents) {
    name = "--";
    cents = 0;
    if (hz < 20.f) return;
    const float midi = 69.f + 12.f * std::log2(hz / 440.f);
    const int nearest = static_cast<int>(std::lround(midi));
    cents = static_cast<int>(std::lround((midi - nearest) * 100.f));
    const int idx = ((nearest % 12) + 12) % 12;
    const int octave = nearest / 12 - 1;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", kNoteNames[idx], octave);
    name = buf;
}

} // namespace

AudioScreen::AudioScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    bands_.assign(kBands, 0.f);
    wave_.assign(300, 0.f);
    init();
}

AudioScreen::~AudioScreen() {
    stop_worker();
    if (timer_) lv_timer_delete(timer_);
    if (canvas_ && lv_obj_is_valid(canvas_)) lv_obj_delete(canvas_);
    if (draw_) lv_draw_buf_destroy(draw_);
}

void AudioScreen::build() {
    auto* root = content();
    const auto tok = tokens();
    make_header(root, "Audio");

    header_ = lv_label_create(root);
    auto* font = assets().load_standard_font(11);
    lv_obj_set_style_text_font(header_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(header_, tok.ink, 0);
    lv_obj_set_pos(header_, 4, 22);
    lv_obj_set_width(header_, 312);

    w_ = 320;
    h_ = view::kContentHeight - 60;
    if (h_ < 40) h_ = 40;
    draw_ = lv_draw_buf_create(w_, h_, LV_COLOR_FORMAT_RGB565, 0);
    if (draw_ && draw_->data) {
        stride_ = static_cast<int>(draw_->header.stride);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_draw_buf(canvas_, draw_);
        lv_obj_set_pos(canvas_, 0, 38);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    }

    note_ = lv_label_create(root);
    auto* big = assets().load_standard_font(40);
    lv_obj_set_style_text_font(note_, big ? big : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(note_, tok.accent, 0);
    lv_obj_align(note_, LV_ALIGN_CENTER, 0, -6);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

    hint_ = lv_label_create(root);
    lv_obj_set_style_text_font(hint_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_, tok.ink_dim, 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(hint_, "Tab mode   Enter rec   Esc back");

    update_header();
    draw();
    start_worker();
    timer_ = lv_timer_create(timer_cb, 33, this);
}

void AudioScreen::start_worker() {
    stop_worker();
    cancel_ = false;
    worker_ = std::thread([this]() {
        while (!cancel_) {
            std::vector<int16_t> pcm;
            std::string err;
            if (!platform::capture_pcm(pcm, kRate, kChunkMs, err) || pcm.empty()) {
                have_audio_ = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                continue;
            }
            have_audio_ = true;

            std::vector<float> f(pcm.size());
            double sumsq = 0.0;
            for (size_t i = 0; i < pcm.size(); ++i) {
                const float v = static_cast<float>(pcm[i]) / 32768.f;
                f[i] = v;
                sumsq += static_cast<double>(v) * v;
            }
            const float rms = pcm.empty() ? 0.f : std::sqrt(static_cast<float>(sumsq / pcm.size()));

            std::vector<float> mags;
            tools::dsp::magnitude_spectrum(f, mags);
            std::vector<float> bands(kBands, 0.f);
            if (mags.size() > 2) {
                const float lo = 1.f;
                const float hi = static_cast<float>(mags.size() - 1);
                for (int b = 0; b < kBands; ++b) {
                    const float t0 = static_cast<float>(b) / kBands;
                    const float t1 = static_cast<float>(b + 1) / kBands;
                    const int i0 = std::max(1, static_cast<int>(lo * std::pow(hi / lo, t0)));
                    const int i1 = std::max(i0 + 1, static_cast<int>(lo * std::pow(hi / lo, t1)));
                    float m = 0.f;
                    for (int i = i0; i < i1 && i < static_cast<int>(mags.size()); ++i) m = std::max(m, mags[i]);
                    bands[b] = m;
                }
            }
            const float dom = tools::dsp::dominant_freq(f, kRate);

            // Downsample waveform to display width.
            std::vector<float> wave(300, 0.f);
            if (!f.empty()) {
                for (size_t x = 0; x < wave.size(); ++x) {
                    const size_t idx = x * (f.size() - 1) / (wave.size() - 1);
                    wave[x] = f[idx];
                }
            }

            {
                std::lock_guard<std::mutex> lock(mu_);
                wave_ = std::move(wave);
                bands_ = std::move(bands);
                level_ = rms;
                dom_hz_ = dom;
            }
            data_ready_ = true;
        }
    });
}

void AudioScreen::stop_worker() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

void AudioScreen::timer_cb(lv_timer_t* t) {
    auto* self = static_cast<AudioScreen*>(lv_timer_get_user_data(t));
    if (self) self->on_timer();
}

void AudioScreen::on_timer() {
    const bool fresh = data_ready_.exchange(false);
    if (fresh || mode_ == Mode::Recorder || mode_ == Mode::Tuner) update_header();
    // Redraw the graphic modes every tick so motion stays fluid between captures.
    if (mode_ == Mode::Meter || mode_ == Mode::Spectrum) draw();
}

void AudioScreen::update_header() {
    if (!header_) return;
    char buf[160];
    const char* src = have_audio_ ? "" : "  (no input)";
    switch (mode_) {
        case Mode::Meter: {
            float db = level_ > 1e-5f ? 20.f * std::log10(level_) : -99.f;
            std::snprintf(buf, sizeof(buf), "Meter / Waveform   %.0f dBFS%s", db, src);
            break;
        }
        case Mode::Spectrum:
            std::snprintf(buf, sizeof(buf), "Spectrum  32-band%s", src);
            break;
        case Mode::Tuner:
            std::snprintf(buf, sizeof(buf), "Tuner   %.1f Hz%s", dom_hz_, src);
            break;
        case Mode::Recorder: {
            std::lock_guard<std::mutex> lock(rec_mu_);
            std::snprintf(buf, sizeof(buf), "Recorder   %s", rec_status_.c_str());
            break;
        }
    }
    lv_label_set_text(header_, buf);

    if (note_) {
        if (mode_ == Mode::Tuner) {
            lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
            std::string name;
            int cents = 0;
            note_for_freq(dom_hz_, name, cents);
            lv_label_set_text_fmt(note_, "%s  %+d", name.c_str(), cents);
        } else {
            lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (canvas_) {
        if (mode_ == Mode::Tuner || mode_ == Mode::Recorder) lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AudioScreen::draw() {
    if (!draw_ || !draw_->data) return;
    auto* data = static_cast<uint8_t*>(draw_->data);
    const uint16_t bg = pack565(8, 10, 14);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) reinterpret_cast<uint16_t*>(data + y * stride_)[x] = bg;

    std::vector<float> wave, bands;
    float level;
    {
        std::lock_guard<std::mutex> lock(mu_);
        wave = wave_;
        bands = bands_;
        level = level_;
    }

    const uint16_t grid = pack565(28, 32, 40);
    for (int x = 0; x < w_; x += 2) put_px(data, stride_, w_, h_, x, h_ / 2, grid);

    if (mode_ == Mode::Meter) {
        // dB level meter bar across the top (-60..0 dBFS).
        const float db = level > 1e-5f ? 20.f * std::log10(level) : -60.f;
        const float t = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
        const int bw = static_cast<int>(t * (w_ - 1));
        for (int x = 0; x < bw; ++x) {
            const uint16_t lc = x > w_ * 85 / 100 ? pack565(255, 90, 90) : pack565(40, 255, 120);
            vline(data, stride_, w_, h_, x, 2, 9, lc);
        }
        // Auto-gained waveform: real audio is low-amplitude, so normalize the
        // peak toward the full height (capped so silence doesn't blow up noise).
        float peak = 1e-3f;
        for (float v : wave) peak = std::max(peak, std::fabs(v));
        const float gain = std::clamp(0.85f / peak, 1.f, 25.f);
        const uint16_t wc = pack565(120, 220, 255);
        const int mid = 12 + (h_ - 14) / 2;
        const int amp = (h_ - 16) / 2;
        int prev_y = mid;
        for (size_t x = 0; x < wave.size() && static_cast<int>(x) < w_; ++x) {
            const int y = mid - static_cast<int>(std::clamp(wave[x] * gain, -1.f, 1.f) * amp);
            vline(data, stride_, w_, h_, static_cast<int>(x), prev_y, y, wc);
            prev_y = y;
        }
    } else if (mode_ == Mode::Spectrum) {
        const int n = static_cast<int>(bands.size());
        if (n > 0) {
            const int bw = std::max(2, w_ / n);
            float mx = 1e-5f;
            for (float v : bands) mx = std::max(mx, v);
            for (int b = 0; b < n; ++b) {
                const float t = bands[b] / mx;
                const int bh = std::clamp(static_cast<int>(t * (h_ - 4)), 0, h_ - 2);
                const uint16_t col = pack565(static_cast<uint8_t>(60 + 195 * t), 220,
                                             static_cast<uint8_t>(80 + 120 * (1.f - t)));
                const int x0 = b * bw;
                for (int x = x0; x < x0 + bw - 1 && x < w_; ++x)
                    vline(data, stride_, w_, h_, x, h_ - 2, h_ - 2 - bh, col);
            }
        }
    }

    if (canvas_) lv_obj_invalidate(canvas_);
}

void AudioScreen::cycle_mode(int delta) {
    int m = static_cast<int>(mode_) + delta;
    const int count = 4;
    m = (m % count + count) % count;
    mode_ = static_cast<Mode>(m);
    update_header();
    draw();
}

void AudioScreen::start_record() {
    bool expected = false;
    if (!recording_.compare_exchange_strong(expected, true)) return;
    {
        std::lock_guard<std::mutex> lock(rec_mu_);
        rec_status_ = "Recording 3s...";
    }
    std::thread([this]() {
        std::vector<int16_t> pcm;
        std::string err;
        const bool ok = platform::capture_pcm(pcm, 44100, 3000, err);
        std::string status;
        if (ok && !pcm.empty()) {
            namespace fs = std::filesystem;
            const std::string dir = tools::config_dir() + "/recordings";
            std::error_code ec;
            fs::create_directories(dir, ec);
            char name[64];
            std::snprintf(name, sizeof(name), "/rec_%ld.wav", static_cast<long>(std::time(nullptr)));
            const std::string path = dir + name;
            if (platform::write_wav(path, pcm, 44100, err)) status = std::string("Saved ") + (name + 1);
            else status = "Write failed: " + err;
        } else {
            status = "Capture failed: " + (err.empty() ? std::string("no input") : err);
        }
        {
            std::lock_guard<std::mutex> lock(rec_mu_);
            rec_status_ = status;
        }
        recording_ = false;
    }).detach();
}

bool AudioScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        stop_worker();
        return KvxScreen::handle_key(key);
    }
    if (key == platform::kKeyTab || key == 'm' || key == 'M' || key == LV_KEY_RIGHT) {
        cycle_mode(1);
        return true;
    }
    if (key == LV_KEY_LEFT) {
        cycle_mode(-1);
        return true;
    }
    if (key >= '1' && key <= '4') {
        mode_ = static_cast<Mode>(key - '1');
        update_header();
        draw();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        if (mode_ == Mode::Recorder) start_record();
        else cycle_mode(1);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> AudioScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { stop_worker(); view_model().go_back(); }},
        {view::ICON_ARROW_LEFT, [this]() { cycle_mode(-1); }},
        {view::ICON_CHECK, [this]() {
             if (mode_ == Mode::Recorder) start_record();
             else cycle_mode(1);
         }},
        {view::ICON_ARROW_RIGHT, [this]() { cycle_mode(1); }},
        {view::ICON_HOUSE, [this]() { stop_worker(); view_model().go_home(); }},
    }};
}

} // namespace screen

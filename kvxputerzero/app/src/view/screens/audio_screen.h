#pragma once

#include "kvx_screen.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

// Mic analyzer: level + waveform, 32-band spectrum, tuner (note + cents) and a
// short recorder. (pocket-zero-tools "Audio".)
class AudioScreen final : public KvxScreen {
public:
    AudioScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~AudioScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    enum class Mode { Meter, Spectrum, Tuner, Recorder };

    static void timer_cb(lv_timer_t* t);
    void on_timer();
    void start_worker();
    void stop_worker();
    void cycle_mode(int delta);
    void start_record();
    void update_header();
    void draw();

    lv_obj_t* header_{nullptr};
    lv_obj_t* note_{nullptr};   // big note label, tuner mode only
    lv_obj_t* hint_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* draw_{nullptr};
    lv_timer_t* timer_{nullptr};
    int stride_{0};
    int w_{0};
    int h_{0};

    std::thread worker_;
    std::mutex mu_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> data_ready_{false};
    std::atomic<bool> have_audio_{false};

    std::vector<float> wave_;   // normalized waveform for display
    std::vector<float> bands_;  // 32 spectrum bands
    float level_{0.f};
    float dom_hz_{0.f};

    std::atomic<bool> recording_{false};
    std::mutex rec_mu_;
    std::string rec_status_{"Enter to record 3s"};

    Mode mode_{Mode::Meter};
};

} // namespace screen

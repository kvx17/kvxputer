#pragma once

#include "kvx_screen.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace screen {

// Left half is the tone list. Right half shows the last five keys and the tone
// that is playing. A tap plays a short beep. Holding the key keeps that tone
// going until the key is released.
class DtmfDialerScreen final : public KvxScreen {
public:
    DtmfDialerScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~DtmfDialerScreen() override;

protected:
    void build() override;
    void on_shown() override;
    const char* typing_hint() const override;
    bool handle_key(uint32_t key) override;

private:
    static void on_key_up(uint32_t key, const char* key_name, void* user);
    static void tick_cb(lv_timer_t* timer);

    void begin_hold(int index);
    void release_index(int index);
    void remember(int index);
    void backspace();
    void refresh();
    void player_main();
    void play_held(int index);
    void note_error(std::string message);

    std::array<lv_obj_t*, 16> rows_{};
    std::array<lv_obj_t*, 16> row_labels_{};
    std::array<lv_obj_t*, 5> hist_{};
    lv_obj_t* current_ch_{nullptr};
    lv_obj_t* current_hz_{nullptr};
    lv_obj_t* previous_ch_{nullptr};
    lv_obj_t* previous_hz_{nullptr};
    lv_timer_t* timer_{nullptr};

    std::array<int, 5> history_{};
    int hist_count_{0};
    int current_{-1};
    int previous_{-1};

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<int> queue_;
    int held_{-1};
    bool stop_{false};
    std::string error_;
    std::thread player_;
};

} // namespace screen

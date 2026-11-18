#include "boot_splash.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "logger.h"
#include "theme.h"

#include "lvgl.h"

#if LV_USE_GIF
#include "src/widgets/gif/lv_gif.h"
#endif

#include <algorithm>
#include <string>

namespace app {
namespace {

struct SplashState {
    bool skip = false;
    bool finished = false;
};

bool splash_key_listener(uint32_t /*key*/,
                         const char* /*key_name*/,
                         bool long_pressed,
                         void* user_data) {
    if (long_pressed) return false;
    auto* state = static_cast<SplashState*>(user_data);
    if (state) state->skip = true;
    return true;
}

} // namespace

void play_boot_splash(AssetManager& assets) {
#if !LV_USE_GIF
    LV_UNUSED(assets);
    LOG_WARN("LV_USE_GIF disabled; skipping boot splash");
    return;
#else
    const auto gif_path = assets.resolve("images/boot.gif");
    if (gif_path.empty()) {
        LOG_WARN("boot.gif not found; skipping splash");
        return;
    }

    SplashState state;
    platform::set_global_key_listener(splash_key_listener, &state);

    auto* screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    auto* gif = lv_gif_create(screen);
    if (!gif) {
        LOG_WARN("failed to create gif widget");
        platform::clear_global_key_listener(splash_key_listener, &state);
        lv_obj_delete(screen);
        return;
    }

    // LVGL STDIO driver letter A: + absolute path.
    const std::string src = std::string("A:") + gif_path.string();
    lv_gif_set_src(gif, src.c_str());
    if (!lv_gif_is_loaded(gif)) {
        LOG_WARN("failed to load boot.gif from {}", src);
        platform::clear_global_key_listener(splash_key_listener, &state);
        lv_obj_delete(screen);
        return;
    }

    // Play once (Cardputer factory splash); any key skips.
    lv_gif_set_loop_count(gif, 1);
    lv_obj_center(gif);

    // Scale ~250x135 GIF toward 320x170 without cropping.
    constexpr int32_t kGifW = 250;
    constexpr int32_t kGifH = 135;
    const int32_t scale =
        std::min((view::kScreenWidth * 256) / kGifW, (view::kScreenHeight * 256) / kGifH);
    if (scale > 256) {
        lv_obj_set_style_transform_scale(gif, scale, 0);
        lv_obj_set_style_transform_pivot_x(gif, kGifW / 2, 0);
        lv_obj_set_style_transform_pivot_y(gif, kGifH / 2, 0);
    }

    lv_screen_load(screen);
    LOG_INFO("boot splash: {} (press any key to skip)", src);

    const uint32_t started = lv_tick_get();
    constexpr uint32_t kMaxMs = 20000;
    int32_t last_frame = -1;
    int stagnant = 0;

    while (!state.skip && !state.finished) {
        lv_timer_handler();
        lv_delay_ms(5);

        if (lv_tick_elaps(started) > kMaxMs) {
            break;
        }

        const int32_t frames = lv_gif_get_frame_count(gif);
        const int32_t frame = lv_gif_get_current_frame_index(gif);
        if (frames > 0 && frame + 1 >= frames) {
            // Last frame reached; allow a couple of ticks then exit.
            if (frame == last_frame) {
                if (++stagnant > 8) state.finished = true;
            } else {
                stagnant = 0;
            }
        }
        last_frame = frame;
    }

    platform::clear_global_key_listener(splash_key_listener, &state);
    lv_obj_delete(screen);
    LOG_INFO("boot splash done (skipped={})", state.skip ? "yes" : "no");
#endif
}

} // namespace app

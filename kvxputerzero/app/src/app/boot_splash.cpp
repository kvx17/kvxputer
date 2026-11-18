#include "boot_splash.h"

#include "linux_input.h"
#include "logger.h"
#include "matrix_rain.h"
#include "theme.h"

#include "lvgl.h"

#include <cstdlib>

namespace app {
namespace {

struct SplashState {
    bool skip = false;
};

BootEventPump event_pump = nullptr;
void* event_pump_data = nullptr;

bool splash_key_listener(uint32_t key,
                         const char* key_name,
                         bool /*long_pressed*/,
                         void* user_data) {
    auto* state = static_cast<SplashState*>(user_data);
    if (state && !state->skip) {
        LOG_INFO("matrix boot skip key={} ({})", key, key_name ? key_name : "");
        state->skip = true;
    }
    return true;
}

} // namespace

void set_boot_event_pump(BootEventPump pump, void* user_data) {
    event_pump = pump;
    event_pump_data = user_data;
}

void play_boot_splash(AssetManager& /*assets*/) {
    // Automation / screenshot scripts skip the matrix rain.
    if (std::getenv("KVX_SKIP_BOOT") || std::getenv("KVX_UI_SCRIPT") ||
        std::getenv("KVX_SCREENSHOT_PATH") || std::getenv("KVX_OPEN_TOOL")) {
        LOG_INFO("matrix boot skipped (automation env set)");
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
    lv_screen_load(screen);

    {
        view::MatrixRain rain;
        if (!rain.mount(screen)) {
            LOG_WARN("matrix boot failed to start");
        } else {
            LOG_INFO("matrix boot ({} ms, any key skips)", view::MatrixRain::kDurationMs);
            while (!state.skip && !rain.finished()) {
                if (event_pump && !event_pump(event_pump_data)) {
                    LOG_INFO("matrix boot event pump stopped");
                    state.skip = true;
                    break;
                }
                lv_timer_handler();
                rain.step();
                lv_delay_ms(5);
            }
            LOG_INFO("matrix boot done (skipped={})", state.skip ? "yes" : "no");
        }
    }

    platform::clear_global_key_listener(splash_key_listener, &state);
    lv_obj_delete(screen);
}

} // namespace app

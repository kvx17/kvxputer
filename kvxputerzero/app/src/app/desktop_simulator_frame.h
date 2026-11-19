/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if USE_DESKTOP

#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;
union SDL_Event;

namespace app {

class AssetManager;
class DesktopVirtualKeypad;

class DesktopSimulatorFrame {
public:
    static constexpr int kWidth   = 630;
    static constexpr int kHeight  = 405;
    static constexpr int kScreenX = 155;
    static constexpr int kScreenY = 21;

    explicit DesktopSimulatorFrame(const AssetManager& assets);
    ~DesktopSimulatorFrame();

    DesktopSimulatorFrame(const DesktopSimulatorFrame&) = delete;
    DesktopSimulatorFrame& operator=(const DesktopSimulatorFrame&) = delete;

    lv_display_t* display() const;
    bool process_events();
    void bind_dark_mode(lv_subject_t* dark_mode_subject);

    // Deterministic desktop automation (KVX_UI_SCRIPT). Returns false if keypad missing.
    bool inject_named_key(const std::string& name);
    bool inject_char(char ch);
    void inject_text(const std::string& text);

private:
    static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
    static void pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static void encoder_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static void theme_observer(lv_observer_t* observer, lv_subject_t* subject);

    bool initialize_sdl();
    bool initialize_display();
    bool load_shell_textures(const AssetManager& assets);
    SDL_Texture* load_png_texture(const char* path);
    void handle_event(const SDL_Event& event, bool& running);
    void update_pointer(int32_t window_x, int32_t window_y, bool pressed);
    void emit_physical_key(int32_t sdl_key, bool pressed);
    void render();
    void set_dark_mode(bool dark_mode);

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* light_texture_{nullptr};
    SDL_Texture* dark_texture_{nullptr};
    SDL_Texture* lvgl_texture_{nullptr};
    lv_display_t* display_{nullptr};
    lv_indev_t* pointer_{nullptr};
    lv_indev_t* encoder_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
    std::unique_ptr<DesktopVirtualKeypad> keypad_;
    std::vector<uint32_t> framebuffer_;
    int32_t pointer_x_{0};
    int32_t pointer_y_{0};
    int16_t encoder_diff_{0};
    bool pointer_pressed_{false};
    bool encoder_pressed_{false};
    bool dark_mode_{false};
    bool initialized_{false};
    // SDL TEXTINPUT echo to suppress after KEYDOWN already emitted the glyph.
    // -1 = none; otherwise the ASCII code that the next TEXTINPUT may repeat.
    int swallow_text_char_{-1};
};

} // namespace app

#endif

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "desktop_simulator_frame.h"

#if USE_DESKTOP

#include "asset_manager.h"
#include "desktop_virtual_keypad.h"
#include "linux_input.h"
#include "logger.h"
#include "theme.h"

#include <SDL.h>
#include <png.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace app {
namespace {

constexpr SDL_Rect kScreenRect{DesktopSimulatorFrame::kScreenX,
                               DesktopSimulatorFrame::kScreenY,
                               view::kScreenWidth,
                               view::kScreenHeight};

uint32_t map_control_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS:
            return LV_KEY_RIGHT;
        case SDLK_LEFT:
        case SDLK_KP_MINUS:
            return LV_KEY_LEFT;
        case SDLK_UP:
            return LV_KEY_UP;
        case SDLK_DOWN:
            return LV_KEY_DOWN;
        case SDLK_ESCAPE:
            return LV_KEY_ESC;
        case SDLK_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case SDLK_DELETE:
            return LV_KEY_DEL;
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
            return LV_KEY_ENTER;
        case SDLK_TAB:
        case SDLK_PAGEDOWN:
            return LV_KEY_NEXT;
        case SDLK_PAGEUP:
            return LV_KEY_PREV;
        case SDLK_HOME:
            return LV_KEY_HOME;
        case SDLK_END:
            return LV_KEY_END;
        case SDLK_PRINTSCREEN:
            return platform::kKeyPrintScreen;
        case SDLK_HELP:
            return platform::kKeyHelp;
        case SDLK_KP_4:
            return '4';
        default:
            return 0;
    }
}

uint32_t utf8_character_size(const char* text) {
    const auto lead = static_cast<uint8_t>(*text);
    if ((lead & 0x80U) == 0) return 1;
    if ((lead & 0xe0U) == 0xc0U) return 2;
    if ((lead & 0xf0U) == 0xe0U) return 3;
    if ((lead & 0xf8U) == 0xf0U) return 4;
    return 1;
}

} // namespace

DesktopSimulatorFrame::DesktopSimulatorFrame(const AssetManager& assets) {
    initialized_ = initialize_sdl() && load_shell_textures(assets) && initialize_display();
    if (!initialized_) {
        LOG_ERROR("failed to initialize desktop SDL compositor");
    }
}

DesktopSimulatorFrame::~DesktopSimulatorFrame() {
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }

    keypad_.reset();
    if (pointer_) lv_indev_delete(pointer_);
    if (encoder_) lv_indev_delete(encoder_);
    pointer_ = nullptr;
    encoder_ = nullptr;

    if (display_) {
        lv_display_delete(display_);
        display_ = nullptr;
    }
    if (lvgl_texture_) SDL_DestroyTexture(lvgl_texture_);
    if (light_texture_) SDL_DestroyTexture(light_texture_);
    if (dark_texture_) SDL_DestroyTexture(dark_texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);

    SDL_StopTextInput();
    SDL_Quit();
}

lv_display_t* DesktopSimulatorFrame::display() const {
    return initialized_ ? display_ : nullptr;
}

bool DesktopSimulatorFrame::initialize_sdl() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("kvxputerzero",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               kWidth,
                               kHeight,
                               SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        LOG_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        LOG_WARN("accelerated vsync renderer unavailable: {}", SDL_GetError());
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    }
    if (!renderer_) {
        LOG_WARN("hardware renderer unavailable, falling back to software: {}", SDL_GetError());
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        LOG_ERROR("SDL_CreateRenderer failed: {}", SDL_GetError());
        return false;
    }

    if (SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight) != 0) {
        LOG_ERROR("SDL_RenderSetLogicalSize failed: {}", SDL_GetError());
        return false;
    }

    SDL_RendererInfo info{};
    SDL_GetRendererInfo(renderer_, &info);
    LOG_INFO("SDL renderer: {} accelerated={} vsync={}",
             info.name ? info.name : "unknown",
             (info.flags & SDL_RENDERER_ACCELERATED) != 0,
             (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0);
    SDL_StartTextInput();
    lv_tick_set_cb(SDL_GetTicks);
    lv_delay_set_cb(SDL_Delay);
    return true;
}

bool DesktopSimulatorFrame::load_shell_textures(const AssetManager& assets) {
    const auto light_path = assets.resolve("images/cp0-light.png");
    const auto dark_path = assets.resolve("images/cp0-dark.png");
    if (light_path.empty() || dark_path.empty()) {
        LOG_ERROR("desktop simulator shell assets are missing");
        return false;
    }

    light_texture_ = load_png_texture(light_path.string().c_str());
    dark_texture_ = load_png_texture(dark_path.string().c_str());
    return light_texture_ && dark_texture_;
}

SDL_Texture* DesktopSimulatorFrame::load_png_texture(const char* path) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path)) {
        LOG_ERROR("failed to read PNG {}: {}", path, image.message);
        return nullptr;
    }

    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        LOG_ERROR("failed to decode PNG {}: {}", path, image.message);
        png_image_free(&image);
        return nullptr;
    }

    auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),
                                                        static_cast<int>(image.width),
                                                        static_cast<int>(image.height),
                                                        32,
                                                        static_cast<int>(image.width * 4U),
                                                        SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        LOG_ERROR("failed to create SDL surface for {}: {}", path, SDL_GetError());
        png_image_free(&image);
        return nullptr;
    }

    auto* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);
    png_image_free(&image);
    if (!texture) {
        LOG_ERROR("failed to create SDL texture for {}: {}", path, SDL_GetError());
    }
    return texture;
}

bool DesktopSimulatorFrame::initialize_display() {
    lvgl_texture_ = SDL_CreateTexture(renderer_,
                                      SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      view::kScreenWidth,
                                      view::kScreenHeight);
    if (!lvgl_texture_) {
        LOG_ERROR("failed to create LVGL SDL texture: {}", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(lvgl_texture_, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(lvgl_texture_, SDL_ScaleModeNearest);
#endif

    display_ = lv_display_create(view::kScreenWidth, view::kScreenHeight);
    if (!display_) {
        return false;
    }

    framebuffer_.resize(static_cast<size_t>(view::kScreenWidth) * view::kScreenHeight);
    lv_display_set_driver_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display_,
                           framebuffer_.data(),
                           nullptr,
                           framebuffer_.size() * sizeof(framebuffer_[0]),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flush_cb);

    pointer_ = lv_indev_create();
    lv_indev_set_type(pointer_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer_, pointer_read_cb);
    lv_indev_set_driver_data(pointer_, this);
    lv_indev_set_mode(pointer_, LV_INDEV_MODE_EVENT);
    lv_indev_set_display(pointer_, display_);

    encoder_ = lv_indev_create();
    lv_indev_set_type(encoder_, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(encoder_, encoder_read_cb);
    lv_indev_set_driver_data(encoder_, this);
    lv_indev_set_mode(encoder_, LV_INDEV_MODE_EVENT);
    lv_indev_set_display(encoder_, display_);

    keypad_ = std::make_unique<DesktopVirtualKeypad>(display_);
    render();
    return true;
}

void DesktopSimulatorFrame::bind_dark_mode(lv_subject_t* dark_mode_subject) {
    if (!dark_mode_subject) {
        return;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
    }
    theme_observer_ = lv_subject_add_observer(dark_mode_subject, theme_observer, this);
}

void DesktopSimulatorFrame::theme_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* frame = static_cast<DesktopSimulatorFrame*>(lv_observer_get_user_data(observer));
    if (frame) {
        frame->set_dark_mode(lv_subject_get_int(subject) != 0);
    }
}

void DesktopSimulatorFrame::set_dark_mode(bool dark_mode) {
    dark_mode_ = dark_mode;
    render();
}

void DesktopSimulatorFrame::flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    auto* frame = static_cast<DesktopSimulatorFrame*>(lv_display_get_driver_data(display));
    if (frame && lv_display_flush_is_last(display)) {
        SDL_UpdateTexture(frame->lvgl_texture_,
                          nullptr,
                          frame->framebuffer_.data(),
                          view::kScreenWidth * static_cast<int>(sizeof(uint32_t)));
        frame->render();
    }
    lv_display_flush_ready(display);
}

void DesktopSimulatorFrame::pointer_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* frame = static_cast<DesktopSimulatorFrame*>(lv_indev_get_driver_data(indev));
    data->point.x = frame->pointer_x_;
    data->point.y = frame->pointer_y_;
    data->state = frame->pointer_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void DesktopSimulatorFrame::encoder_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* frame = static_cast<DesktopSimulatorFrame*>(lv_indev_get_driver_data(indev));
    data->enc_diff = frame->encoder_diff_;
    data->state = frame->encoder_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    frame->encoder_diff_ = 0;
}

bool DesktopSimulatorFrame::process_events() {
    if (!initialized_) {
        return false;
    }

    bool running = true;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        handle_event(event, running);
    }

    // Desktop debug: /tmp/kvx_inject_keys — one token per frame (Esc Enter Sym letter/digit).
    if (keypad_) {
        std::ifstream inject("/tmp/kvx_inject_keys");
        if (inject) {
            std::vector<std::string> tokens;
            for (std::string token; inject >> token;) tokens.push_back(std::move(token));
            inject.close();
            std::remove("/tmp/kvx_inject_keys");
            if (!tokens.empty()) {
                const std::string token = tokens.front();
                if (token == "Esc" || token == "Escape") {
                    keypad_->emit_key(LV_KEY_ESC);
                } else if (token == "Enter" || token == "Return") {
                    keypad_->emit_key(LV_KEY_ENTER);
                } else if (token == "Sym") {
                    keypad_->handle_pointer(8 + 25, 195 + 49 + 23, true);
                    keypad_->handle_pointer(8 + 25, 195 + 49 + 23, false);
                } else if (token.size() == 1) {
                    const char ch = token[0];
                    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
                        keypad_->host_key_event(ch, true);
                        keypad_->host_key_event(ch, false);
                    } else if (ch >= 'A' && ch <= 'Z') {
                        keypad_->set_host_shift(true);
                        keypad_->host_key_event(static_cast<char>(ch - 'A' + 'a'), true);
                        keypad_->host_key_event(static_cast<char>(ch - 'A' + 'a'), false);
                        keypad_->set_host_shift(false);
                    } else {
                        keypad_->emit_key(static_cast<uint32_t>(static_cast<unsigned char>(ch)));
                    }
                }
                if (tokens.size() > 1) {
                    std::ofstream rest("/tmp/kvx_inject_keys");
                    for (size_t i = 1; i < tokens.size(); ++i) {
                        if (i > 1) rest << ' ';
                        rest << tokens[i];
                    }
                }
            }
        }
    }
    return running;
}

void DesktopSimulatorFrame::handle_event(const SDL_Event& event, bool& running) {
    switch (event.type) {
        case SDL_QUIT:
            running = false;
            return;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED) render();
            if (event.window.event == SDL_WINDOWEVENT_LEAVE) update_pointer(0, 0, false);
            return;
        case SDL_MOUSEMOTION:
            update_pointer(event.motion.x, event.motion.y, pointer_pressed_);
            return;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            const bool pressed = event.type == SDL_MOUSEBUTTONDOWN;
            if (event.button.button == SDL_BUTTON_LEFT) {
                float logical_x = 0.0F;
                float logical_y = 0.0F;
                SDL_RenderWindowToLogical(renderer_, event.button.x, event.button.y, &logical_x, &logical_y);
                const auto x = static_cast<int32_t>(logical_x);
                const auto y = static_cast<int32_t>(logical_y);
                const bool in_screen = x >= kScreenRect.x && x < kScreenRect.x + kScreenRect.w &&
                                       y >= kScreenRect.y && y < kScreenRect.y + kScreenRect.h;
                update_pointer(event.button.x, event.button.y, pressed && in_screen);
                keypad_->handle_pointer(x, y, pressed);
                render();
            }
            else if (event.button.button == SDL_BUTTON_MIDDLE) {
                encoder_pressed_ = pressed;
                lv_indev_read(encoder_);
            }
            return;
        }
        case SDL_MOUSEWHEEL:
            encoder_diff_ = static_cast<int16_t>(-event.wheel.y);
            lv_indev_read(encoder_);
            return;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (event.key.repeat) {
                return;
            }
            const bool down = event.type == SDL_KEYDOWN;
            const auto sym = event.key.keysym.sym;
            const auto mod = event.key.keysym.mod;
            if (keypad_) {
                if (sym == SDLK_LALT || sym == SDLK_RALT) {
                    keypad_->set_host_fn(down);
                    return;
                }
                if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) {
                    keypad_->set_host_shift(down);
                    return;
                }
                char base = 0;
                if ((mod & KMOD_CTRL) == 0 && sym >= SDLK_a && sym <= SDLK_z) {
                    base = static_cast<char>(sym);
                }
                else if ((mod & KMOD_CTRL) == 0 && sym >= SDLK_0 && sym <= SDLK_9) {
                    base = static_cast<char>(sym);
                }
                if (base != 0) {
                    keypad_->host_key_event(base, down);
                    if (down) {
                        swallow_text_ = true;
                    }
                    return;
                }
            }
            const auto control = map_control_key(sym);
            if (control != 0) {
                emit_physical_key(static_cast<int32_t>(control), down);
            }
            return;
        }
        case SDL_TEXTINPUT: {
            if (swallow_text_) {
                swallow_text_ = false;
                return;
            }
            const char* cursor = event.text.text;
            while (*cursor != '\0') {
                const auto length = utf8_character_size(cursor);
                uint32_t key = 0;
                std::copy_n(reinterpret_cast<const uint8_t*>(cursor),
                            std::min<uint32_t>(length, sizeof(key)),
                            reinterpret_cast<uint8_t*>(&key));
                if (keypad_ && key != '4') {
                    keypad_->emit_key(key);
                }
                cursor += length;
            }
            return;
        }
        default:
            return;
    }
}

void DesktopSimulatorFrame::update_pointer(int32_t window_x, int32_t window_y, bool pressed) {
    float logical_x = 0.0F;
    float logical_y = 0.0F;
    SDL_RenderWindowToLogical(renderer_, window_x, window_y, &logical_x, &logical_y);
    pointer_x_ = std::clamp(static_cast<int32_t>(logical_x) - kScreenRect.x,
                            0,
                            view::kScreenWidth - 1);
    pointer_y_ = std::clamp(static_cast<int32_t>(logical_y) - kScreenRect.y,
                            0,
                            view::kScreenHeight - 1);
    pointer_pressed_ = pressed;
    lv_indev_read(pointer_);
}

void DesktopSimulatorFrame::emit_physical_key(int32_t key, bool pressed) {
    if (keypad_) {
        keypad_->emit_key_state(static_cast<uint32_t>(key), pressed);
    }
}

void DesktopSimulatorFrame::render() {
    if (!renderer_) {
        return;
    }

    SDL_SetRenderDrawColor(renderer_, 0x20, 0x21, 0x24, 0xff);
    SDL_RenderClear(renderer_);
    auto* shell = dark_mode_ ? dark_texture_ : light_texture_;
    if (shell) SDL_RenderCopy(renderer_, shell, nullptr, nullptr);
    if (keypad_) keypad_->render(renderer_);
    if (lvgl_texture_) SDL_RenderCopy(renderer_, lvgl_texture_, nullptr, &kScreenRect);
    SDL_RenderPresent(renderer_);
}

bool DesktopSimulatorFrame::inject_named_key(const std::string& name) {
    if (!keypad_ || name.empty()) return false;

    auto lower = name;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    if (lower == "up") {
        keypad_->emit_key(LV_KEY_UP);
        return true;
    }
    if (lower == "down") {
        keypad_->emit_key(LV_KEY_DOWN);
        return true;
    }
    if (lower == "left") {
        keypad_->emit_key(LV_KEY_LEFT);
        return true;
    }
    if (lower == "right") {
        keypad_->emit_key(LV_KEY_RIGHT);
        return true;
    }
    if (lower == "enter" || lower == "return" || lower == "ok") {
        keypad_->emit_key(LV_KEY_ENTER);
        return true;
    }
    if (lower == "esc" || lower == "escape" || lower == "back") {
        keypad_->emit_key(LV_KEY_ESC);
        return true;
    }
    if (lower == "backspace" || lower == "del" || lower == "delete") {
        keypad_->emit_key(LV_KEY_BACKSPACE);
        return true;
    }
    if (lower == "tab") {
        keypad_->emit_key(LV_KEY_NEXT);
        return true;
    }
    if (lower == "home") {
        // Action-bar slot 8 → house
        keypad_->emit_key(static_cast<uint32_t>('8'));
        return true;
    }
    if (lower == "fn+h" || lower == "help") {
        keypad_->emit_key(platform::kKeyHelp);
        return true;
    }
    if (lower == "fn+j" || lower == "printscreen" || lower == "screenshot") {
        keypad_->emit_key(platform::kKeyPrintScreen);
        return true;
    }
    if (lower.rfind("fn+", 0) == 0 && lower.size() == 4) {
        const char base = lower[3];
        keypad_->set_host_fn(true);
        keypad_->host_key_event(base, true);
        keypad_->host_key_event(base, false);
        keypad_->set_host_fn(false);
        return true;
    }
    if (name.size() == 1) {
        return inject_char(name[0]);
    }
    LOG_WARN("ui script: unknown key '{}'", name);
    return false;
}

bool DesktopSimulatorFrame::inject_char(char ch) {
    if (!keypad_) return false;
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
        keypad_->host_key_event(ch, true);
        keypad_->host_key_event(ch, false);
        return true;
    }
    if (ch >= 'A' && ch <= 'Z') {
        keypad_->set_host_shift(true);
        keypad_->host_key_event(static_cast<char>(ch - 'A' + 'a'), true);
        keypad_->host_key_event(static_cast<char>(ch - 'A' + 'a'), false);
        keypad_->set_host_shift(false);
        return true;
    }
    keypad_->emit_key(static_cast<uint32_t>(static_cast<unsigned char>(ch)));
    return true;
}

void DesktopSimulatorFrame::inject_text(const std::string& text) {
    for (char ch : text) {
        inject_char(ch);
    }
}

} // namespace app

#endif

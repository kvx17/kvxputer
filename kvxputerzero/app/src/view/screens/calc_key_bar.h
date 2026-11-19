#pragma once

#include "kvx_style.h"
#include "lvgl.h"

#include <array>
#include <cstddef>
#include <functional>

namespace screen {

// On-screen operator strip for calculator text fields.
// Digits/letters always type into the field; this bar only inserts punctuation
// via click or ↑ / ← → / Enter (no Sym+letter or letter-key shortcuts).
class CalcKeyBar {
public:
    using InsertFn = std::function<void(char)>;
    using ModeFn   = std::function<void()>;

    void build(lv_obj_t* parent,
               const view::KvxTokens& tokens,
               const lv_font_t* font,
               int y,
               int width,
               const char* glyphs, // e.g. "./-+*(),"
               InsertFn on_insert,
               const char* mode_label = nullptr, // optional leading chip (DEG/RAD)
               ModeFn on_mode = {});

    void set_mode_label(const char* label);
    void set_focused(bool focused);
    bool focused() const { return focused_; }

    // Returns true when the key was used for chip navigation / insert / mode.
    bool handle_key(uint32_t key);

private:
    void refresh_styles();
    void activate_selected();

    static constexpr size_t kMaxKeys = 10;

    InsertFn on_insert_;
    ModeFn on_mode_;
    lv_obj_t* row_{nullptr};
    lv_obj_t* mode_btn_{nullptr};
    lv_obj_t* mode_label_{nullptr};
    std::array<lv_obj_t*, kMaxKeys> btns_{};
    std::array<char, kMaxKeys> glyphs_{};
    size_t count_{0};
    int selected_{0}; // 0 = mode chip when present, else first op
    bool has_mode_{false};
    bool focused_{false};
    view::KvxTokens tokens_{};
};

} // namespace screen

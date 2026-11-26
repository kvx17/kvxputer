#pragma once

#include "kvx_style.h"
#include "lvgl.h"

#include <cmath>
#include <string>
#include <vector>

namespace view::widgets {

// --- Shared instrument chrome -------------------------------------------------
// These build the common pieces every ported "instrument" screen uses, so they
// share one look and obey the pocket-zero frame (status chip top-right, a big
// glance-first readout, a hint bar). All colors come from KvxTokens, so the
// user's main / accent / secondary-accent configuration drives them.

// Status chip anchored to the top-right of the body (under the 18px header).
// Update later with view::set_status_pill(chip, ...).
lv_obj_t* make_status_chip(lv_obj_t* body, const char* text, view::StatusPillKind kind,
                           const lv_font_t* font, const view::KvxTokens& tokens);

// Consistent bottom hint bar (dim text). Update with lv_label_set_text.
lv_obj_t* make_hint_bar(lv_obj_t* body, const char* text, const lv_font_t* font,
                        const view::KvxTokens& tokens);

// Big glance-first readout in the accent color. Caller positions and sets text.
lv_obj_t* make_readout(lv_obj_t* parent, const lv_font_t* big_font, const view::KvxTokens& tokens);

// Secondary caption under a readout (dim). Caller positions and sets text.
lv_obj_t* make_caption(lv_obj_t* parent, const lv_font_t* font, const view::KvxTokens& tokens);

// --- Smooth value follower ----------------------------------------------------
// Exponential ease toward a target, for bubbles / pendulums / compass tapes that
// should "follow a reading" rather than jump. No LVGL; drive from a timer tick.
struct Smooth {
    float value{0.f};
    float target{0.f};

    void reset(float v) { value = target = v; }
    void set(float t) { target = t; }
    // time_const_ms: larger = slower follow. dt_ms: elapsed since last tick.
    void tick(float dt_ms, float time_const_ms) {
        if (time_const_ms < 1.f || dt_ms <= 0.f) {
            value = target;
            return;
        }
        const float a = 1.f - std::exp(-dt_ms / time_const_ms);
        value += (target - value) * a;
    }
};

// --- Section tabs -------------------------------------------------------------
// A row of section names with a gliding accent underline. Left/Right switch
// sections on multi-view instruments (Audio, Level). Evenly spaced slots, so no
// text measurement is needed.
class SectionTabs {
public:
    void build(lv_obj_t* body, int y, const std::vector<std::string>& names, const lv_font_t* font,
               const view::KvxTokens& tokens);
    void set_active(int idx, bool animate);
    int active() const { return active_; }
    int count() const { return static_cast<int>(labels_.size()); }
    void cycle(int delta) { set_active((active_ + delta + count()) % count(), true); }

private:
    std::vector<lv_obj_t*> labels_;
    lv_obj_t* underline_{nullptr};
    view::KvxTokens tokens_{};
    int active_{0};
    int slot_w_{0};
    int underline_w_{0};
};

} // namespace view::widgets

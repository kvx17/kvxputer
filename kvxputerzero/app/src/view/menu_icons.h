#pragma once

#include "kvx_style.h"
#include "launch_catalog.h"
#include "tool_registry.h"

namespace view {

// Channel tile glyph. Unknown ids use the tools toolbox.
const char* channel_icon(const char* channel_id);

// A tool glyph when one fits, otherwise that tool's channel glyph.
const char* tool_icon(model::ToolId id);

// Search and the shortcut picker. Main-menu channels, pages, and tools.
const char* icon_for_target(const model::LaunchTarget& target);

// Main-menu glyphs are the accent green. Other glyphs are white.
// On a light list the white glyph sits on a dark chip so it stays visible.
void add_target_icon(lv_obj_t* row, const lv_font_t* icon_font, const KvxTokens& tokens,
                     const model::LaunchTarget& target);

} // namespace view

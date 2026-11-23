#pragma once

#include "tool_registry.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace runner {

enum class Kind {
    Log,     // streaming / one-shot text lines
    Menu,    // opens children
    Info,    // static text
    StandIn, // text substitute for a pixel screen
};

struct ArgDesc {
    const char* name;
    const char* prompt;
    bool required;
};

using LineFn = std::function<void(const std::string&)>;
using RunFn = std::function<int(const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line)>;

struct Entry {
    const char* channel; // wifi, tools, discovery, …
    const char* key;
    const char* label;
    model::ToolId tool_id; // None when not in tool_registry
    Kind kind;
    bool confirm;          // CLI asks before run
    bool needs_exclusive;  // GUI must be quit first
    std::vector<ArgDesc> args;
    std::vector<std::string> children; // for Menu: "channel/key" or just keys in same channel
    RunFn run;             // null for Menu / GUI-only reason rows
    const char* reason;    // non-null => classified but not runnable (e.g. matrix)
};

// Full catalog (ToolIds + Tools-channel extras + stand-ins).
const std::vector<Entry>& catalog();

const Entry* find_entry(const char* channel, const char* key);
std::vector<const Entry*> entries_for_channel(const char* channel);

// Walk every ToolId (except None/Count). Returns error lines; empty = ok.
std::vector<std::string> check_tool_ids_classified();

// True when a graphical kvxputerzero process is running (not this CLI / helpers).
bool graphical_app_busy();

} // namespace runner

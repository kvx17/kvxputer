#pragma once

#include "base_model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace model {

enum class LaunchKind {
    Channel,
    Tool,
    Page,
};

// Something the search bar, Discovery-adjacent shortcuts, and the shortcut
// picker can open. `id` is stable in the user config (`ch:wifi`, `tool:ble_scan`).
struct LaunchTarget {
    std::string id;
    std::string label;
    std::string group;
    LaunchKind kind = LaunchKind::Tool;
    int channel_index = -1;
    ToolId tool = ToolId::None;
    AppPage page = AppPage::ChannelGrid;
    int aux = 0;
    // Lowercase copies built once so a keystroke does not reallocate the catalog.
    std::string fold_label;
    std::string fold_group;
    std::string fold_id;
};

struct DiscoveryEntry {
    ToolId id;
    const char* label;
};

const std::vector<LaunchTarget>& launch_targets();
const LaunchTarget* find_launch_target(const std::string& id);
// Best matches first. Empty query returns nothing (callers show recents instead).
std::vector<const LaunchTarget*> search_launch_targets(const std::string& query);

// ADV Discovery flag, limited to tools this app actually has.
const std::vector<DiscoveryEntry>& discovery_entries();

} // namespace model

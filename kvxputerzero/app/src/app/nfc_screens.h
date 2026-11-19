#pragma once

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "kvx_screen.h"
#include "tool_registry.h"

#include <memory>
#include <string>

namespace app {

// Returns nullptr if id is not an NFC-channel tool.
std::unique_ptr<screen::KvxScreen> make_nfc_tool_screen(viewmodel::BaseViewModel& vm, AssetManager& assets,
                                                        model::ToolId id, const std::string& param);

} // namespace app

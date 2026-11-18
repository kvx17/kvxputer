#pragma once

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "kvx_screen.h"
#include "tool_registry.h"

#include <memory>

namespace app {

std::unique_ptr<screen::KvxScreen> make_channel_tool_menu(viewmodel::BaseViewModel& vm,
                                                          AssetManager& assets);
std::unique_ptr<screen::KvxScreen> make_discovery_screen(viewmodel::BaseViewModel& vm,
                                                         AssetManager& assets);
std::unique_ptr<screen::KvxScreen> make_tool_run_screen(viewmodel::BaseViewModel& vm,
                                                        AssetManager& assets);

} // namespace app

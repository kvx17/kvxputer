#pragma once

namespace app {

class AssetManager;

// Desktop builds poll SDL from here so a key can skip the splash before the main loop.
using BootEventPump = bool (*)(void* user_data);
void set_boot_event_pump(BootEventPump pump, void* user_data);

// Play the matrix rain and KVXPUTER wordmark for 5 seconds, or until any key.
void play_boot_splash(AssetManager& assets);

} // namespace app

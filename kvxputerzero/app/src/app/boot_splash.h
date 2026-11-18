#pragma once

namespace app {

class AssetManager;

// Play the Cardputer intro GIF once (or until any key). Returns when finished.
void play_boot_splash(AssetManager& assets);

} // namespace app

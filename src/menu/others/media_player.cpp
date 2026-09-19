#include "media_player.h"

#if defined(HAS_NS4168_SPKR)

#include "menu/others/audio.h"
#include "menu/others/audio_player.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/hal/radio_mem.h"
#include <globals.h>

// Extensions understood by the audio pipeline (see isAudioFile / audio.cpp).
static const char *kAudioExtFilter = "MP3|WAV|FLAC|AAC|OPUS|MOD|RTTTL|TXT";

void mediaPlayerApp() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) {
        displayError("No storage available", true);
        return;
    }

    // Make sure the default media folder exists so users have a home for files.
    kvx::paths::ensureDir(*fs, kvx::paths::MEDIA_AUDIO);

    // Start browsing in the media folder; fall back to root if it is missing.
    String startDir = fs->exists(kvx::paths::MEDIA_AUDIO) ? String(kvx::paths::MEDIA_AUDIO) : String("/");

    String path = loopSD(*fs, true, kAudioExtFilter, startDir);
    if (path == "" || path == "/") return; // user cancelled

    if (!isAudioFile(path)) {
        displayError("Not a supported audio file", true);
        return;
    }

    uiRamEnterHeavy();
    musicPlayerUI(fs, path);
    uiRamLeaveHeavy();
}

#else

void mediaPlayerApp() {}

#endif

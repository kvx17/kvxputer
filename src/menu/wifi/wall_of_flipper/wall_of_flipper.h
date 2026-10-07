#pragma once

#if defined(EVIL_EXTENSIONS)
#include "root/ui/scanner_list.h"

void wallOfFlipperMenu();

// Shared matcher for PC Connect / on-device Wall of Flipper.
bool looksLikeFlipper(const ScannerAdvSnap &dev);
#endif

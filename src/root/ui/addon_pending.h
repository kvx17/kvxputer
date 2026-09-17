/*
 * Shared "port pending" dialog for Evil-Cardputer addons still awaiting a logic port.
 * Reference: resources/Evil-Cardputer-v1-5-4.ino (not compiled).
 */
#pragma once

#include "root/ui/display.h"
#include <Arduino.h>

inline void addonPending(const char *name) {
    displayInfo(String(name) + "\n(port pending — see docs/EVIL_FEATURE_MAP.md)", true);
}

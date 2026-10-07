#pragma once

#if defined(EVIL_EXTENSIONS)
#include <Arduino.h>

void skimmerMenu();

// Returns matching rule name/prefix, or nullptr if no match.
const char *skimmerMatchRule(const String &name, const String &addr);

#endif

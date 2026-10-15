#pragma once

#include <Arduino.h>

#if defined(HAS_KEYBOARD) && !defined(LITE_VERSION)
void totpMenu();
#endif

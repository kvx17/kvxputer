#pragma once

#include <Arduino.h>

#if defined(SOC_USB_OTG_SUPPORTED) && !defined(LITE_VERSION)

// USB host profiler / BadUSB detector (Nemo BadUSB Hunter behavior).
void badusbHunterMenu();

#endif

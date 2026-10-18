#pragma once

#include <Arduino.h> // board pins_arduino.h defines HAS_KEYBOARD

#ifdef HAS_KEYBOARD

// Fullscreen main-menu quick search (apps + optional files mode).
// Call only from the main channel grid. Blocks until Esc or a launch.
void quickSearchLoop();

#endif

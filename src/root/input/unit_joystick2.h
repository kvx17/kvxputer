#ifndef __UNIT_JOYSTICK2_H__
#define __UNIT_JOYSTICK2_H__

#include <Arduino.h>

// Optional M5 Unit Joystick2 (I2C 0x63) on Grove PORT.A.
// Safe no-ops when UNIT_JOYSTICK2 is not defined at build time.

bool unitJoystick2Begin(bool quiet = true);
bool unitJoystick2Reconnect();
bool unitJoystick2IsPresent();
void unitJoystick2Poll();
void unitJoystick2ApplyInput();
String unitJoystick2StatusLabel();
// Fills HID-scale deltas (-127..127). Returns true if stick is off-center.
bool unitJoystick2ReadMove(int8_t &dx, int8_t &dy, int sensitivity);
// True on a new button press (edge). Button is active-low on the unit.
bool unitJoystick2ButtonPressed();
// Current button level (pressed = true).
bool unitJoystick2ButtonDown();

#endif

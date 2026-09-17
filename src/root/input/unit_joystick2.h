#ifndef __UNIT_JOYSTICK2_H__
#define __UNIT_JOYSTICK2_H__

#include <Arduino.h>

// Optional M5 Unit Joystick2 (I2C 0x63) on Grove PORT.A.
// Safe no-ops when UNIT_JOYSTICK2 is not defined at build time.

struct UnitJoystick2Debug {
    bool present = false;
    uint16_t adcX = 0;
    uint16_t adcY = 0;
    int centerX = 0;
    int centerY = 0;
    int nx = 0;
    int ny = 0;
    bool inDead = true;
    bool btnDown = false;
    bool holding = false;
    const char *action = "Idle";
    char conn[28] = "n/a";
};

bool unitJoystick2Begin(bool quiet = true);
bool unitJoystick2Reconnect();
bool unitJoystick2IsPresent();
void unitJoystick2Poll();
void unitJoystick2ApplyInput();
UnitJoystick2Debug unitJoystick2DebugSnapshot();
String unitJoystick2StatusLabel();
// Fills HID-scale deltas (-127..127). Returns true if stick is off-center.
bool unitJoystick2ReadMove(int8_t &dx, int8_t &dy, int sensitivity);
// True on a new button press (edge). Button is active-low on the unit.
bool unitJoystick2ButtonPressed();
// Current button level (pressed = true).
bool unitJoystick2ButtonDown();

#endif

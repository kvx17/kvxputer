#include "root/input/unit_joystick2.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include <globals.h>

#if defined(UNIT_JOYSTICK2)
#include <m5_unit_joystick2.hpp>
#include <Wire.h>
#include <stdlib.h>

#ifndef UNIT_JOYSTICK2_ADDR
#define UNIT_JOYSTICK2_ADDR JOYSTICK2_ADDR
#endif

#ifndef UNIT_JOY_HOLD_MS
#define UNIT_JOY_HOLD_MS 450
#endif

#ifndef UNIT_JOY_NAV_THRESHOLD
#define UNIT_JOY_NAV_THRESHOLD 14000
#endif

#ifndef UNIT_JOY_NAV_DEAD
#define UNIT_JOY_NAV_DEAD 8500
#endif

#ifndef UNIT_JOY_NAV_REPEAT_MS
#define UNIT_JOY_NAV_REPEAT_MS 140
#endif

static M5UnitJoystick2 joystick;
static bool present = false;
static bool busHeld = false;
static bool lastBtn = false;
static bool btnDown = false;
static bool longPressFired = false;
static unsigned long btnDownMs = 0;
static bool pendingSel = false;
static bool pendingEsc = false;
static bool pendingPrev = false;
static bool pendingNext = false;
static int navAccumX = 0;
static int navAccumY = 0;
static int centerX = 32768;
static int centerY = 32768;
static uint8_t centerSettlePolls = 0;
static unsigned long lastNavMs = 0;

static uint8_t joySda() {
    if (kvxConfigPins.i2c_bus.sda >= 0) return (uint8_t)kvxConfigPins.i2c_bus.sda;
#ifdef GROVE_SDA
    return (uint8_t)GROVE_SDA;
#else
    return 2;
#endif
}

static uint8_t joyScl() {
    if (kvxConfigPins.i2c_bus.scl >= 0) return (uint8_t)kvxConfigPins.i2c_bus.scl;
#ifdef GROVE_SCL
    return (uint8_t)GROVE_SCL;
#else
    return 1;
#endif
}

static int8_t clampHid(int v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return (int8_t)v;
}

static void releaseJoystickBus() {
    if (busHeld) {
        releaseI2CBusHold();
        busHeld = false;
    }
}

static void calibrateJoystickCenter() {
    long sx = 0;
    long sy = 0;
    const int samples = 24;
    for (int i = 0; i < samples; i++) {
        uint16_t ax = 0;
        uint16_t ay = 0;
        joystick.get_joy_adc_16bits_value_xy(&ax, &ay);
        sx += ax;
        sy += ay;
        delay(2);
    }
    centerX = (int)(sx / samples);
    centerY = (int)(sy / samples);
    navAccumX = 0;
    navAccumY = 0;
    centerSettlePolls = 0;
    lastNavMs = 0;
}

static void clampNavAccum(int &v) {
    const int limit = UNIT_JOY_NAV_THRESHOLD * 2;
    if (v > limit) v = limit;
    if (v < -limit) v = -limit;
}

static void trackJoystickCenter(int nx, int ny, uint16_t adcX, uint16_t adcY) {
    const int dead = UNIT_JOY_NAV_DEAD;
    if (abs(nx) >= dead || abs(ny) >= dead) {
        centerSettlePolls = 0;
        return;
    }
    if (centerSettlePolls < 200) centerSettlePolls++;
    if (centerSettlePolls < 32) return;
    centerX = (centerX * 15 + (int)adcX) / 16;
    centerY = (centerY * 15 + (int)adcY) / 16;
    navAccumX = 0;
    navAccumY = 0;
}

static bool unitJoystick2Probe(bool quiet) {
    present = false;
    lastBtn = false;
    btnDown = false;
    longPressFired = false;
    pendingSel = false;
    pendingEsc = false;
    pendingPrev = false;
    pendingNext = false;
    releaseJoystickBus();

    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevJoystick2));
    uint8_t sda = joySda();
    uint8_t scl = joyScl();
    TwoWire *bus = acquireI2CBus((int8_t)sda, (int8_t)scl);
    if (bus == nullptr) {
        if (!quiet) Serial.println("[Joystick2] I2C bus unavailable");
        return false;
    }
    if (!joystick.begin(bus, UNIT_JOYSTICK2_ADDR, sda, scl, 100000U)) {
        if (!quiet) Serial.println("[Joystick2] begin failed");
        return false;
    }
    present = true;
    holdI2CBus((int8_t)sda, (int8_t)scl);
    busHeld = true;
    calibrateJoystickCenter();
    joystick.set_rgb_color(0x001400);
    lastBtn = joystick.get_button_value() == 0;
    if (!quiet) Serial.println("[Joystick2] connected");
    return true;
}

bool unitJoystick2Begin(bool quiet) { return unitJoystick2Probe(quiet); }

bool unitJoystick2Reconnect() {
    if (unitJoystick2Probe(false)) return true;

    if (pahubDiscoverDevice(PahubDevJoystick2) >= 0 && unitJoystick2Probe(false)) return true;

    pahubDeselect();
    if (unitJoystick2Probe(false)) return true;

    present = false;
    releaseJoystickBus();
    return false;
}

bool unitJoystick2IsPresent() { return present; }

bool unitJoystick2ReadMove(int8_t &dx, int8_t &dy, int sensitivity) {
    dx = 0;
    dy = 0;
    if (!present) return false;
    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevJoystick2));

    uint16_t adcX = 0, adcY = 0;
    joystick.get_joy_adc_16bits_value_xy(&adcX, &adcY);
    int nx = (int)adcX - centerX;
    int ny = (int)adcY - centerY;
    const int dead = 3500;
    if (abs(nx) < dead) nx = 0;
    if (abs(ny) < dead) ny = 0;
    if (nx == 0 && ny == 0) return false;

    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 10) sensitivity = 10;
    dx = clampHid((nx * sensitivity) / 2560);
    dy = clampHid((-ny * sensitivity) / 2560);
    if (kvxConfig.hidRemoteJoyInvertY) dy = (int8_t)(-dy);
    return dx != 0 || dy != 0;
}

bool unitJoystick2ButtonDown() {
    if (!present) return false;
    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevJoystick2));
    bool down = joystick.get_button_value() == 0;
    lastBtn = down;
    return down;
}

bool unitJoystick2ButtonPressed() {
    if (!present) return false;
    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevJoystick2));
    bool down = joystick.get_button_value() == 0;
    bool edge = down && !lastBtn;
    lastBtn = down;
    return edge;
}

void unitJoystick2Poll() {
    if (!present) return;

    if (pahubDeviceOnMux(PahubDevJoystick2)) {
        PahubTryGuard mux(PahubDevJoystick2);
        if (!mux.ok()) return;
    }

    uint16_t adcX = 0;
    uint16_t adcY = 0;
    joystick.get_joy_adc_16bits_value_xy(&adcX, &adcY);
    int nx = (int)adcX - centerX;
    int ny = (int)adcY - centerY;

    trackJoystickCenter(nx, ny, adcX, adcY);
    nx = (int)adcX - centerX;
    ny = (int)adcY - centerY;

    const int dead = UNIT_JOY_NAV_DEAD;
    const bool inDeadX = abs(nx) < dead;
    const bool inDeadY = abs(ny) < dead;

    if (inDeadX && inDeadY) {
        navAccumX = 0;
        navAccumY = 0;
    } else {
        if (!inDeadX) navAccumX += nx;
        if (!inDeadY) navAccumY += ny;
        clampNavAccum(navAccumX);
        clampNavAccum(navAccumY);
    }

    unsigned long now = millis();
    if (now - lastNavMs >= (unsigned long)UNIT_JOY_NAV_REPEAT_MS) {
        const int threshold = UNIT_JOY_NAV_THRESHOLD;
        if (navAccumY <= -threshold) {
            RotaryNetSteps++;
            navAccumY += threshold;
            lastNavMs = now;
        } else if (navAccumY >= threshold) {
            RotaryNetSteps--;
            navAccumY -= threshold;
            lastNavMs = now;
        }
        if (navAccumX <= -threshold) {
            pendingPrev = true;
            navAccumX += threshold;
            lastNavMs = now;
        } else if (navAccumX >= threshold) {
            pendingNext = true;
            navAccumX -= threshold;
            lastNavMs = now;
        }
    }

    bool down = joystick.get_button_value() == 0;
    if (down) {
        if (!btnDown) {
            btnDown = true;
            btnDownMs = now;
            longPressFired = false;
        } else if (!longPressFired && (now - btnDownMs >= (unsigned long)UNIT_JOY_HOLD_MS)) {
            pendingEsc = true;
            longPressFired = true;
        }
        joystick.set_rgb_color(0x404040);
    } else {
        if (btnDown) {
            btnDown = false;
            if (!longPressFired) pendingSel = true;
        }
        joystick.set_rgb_color(0x001400);
    }
    lastBtn = down;
}

void unitJoystick2ApplyInput() {
    if (!present) return;

    if (pendingEsc) {
        EscPress = true;
        AnyKeyPress = true;
        pendingEsc = false;
    }
    if (pendingSel) {
        SelPress = true;
        AnyKeyPress = true;
        pendingSel = false;
    }
    if (pendingPrev) {
        PrevPress = true;
        AnyKeyPress = true;
        pendingPrev = false;
    }
    if (pendingNext) {
        NextPress = true;
        AnyKeyPress = true;
        pendingNext = false;
    }
}

String unitJoystick2StatusLabel() {
    if (!present) return "Unit Joystick: Not found";
    int8_t ch = pahubChannelFor(PahubDevJoystick2);
    if (pahubDeviceOnMux(PahubDevJoystick2)) {
        return String("Unit Joystick: Connected (ch") + ch + ")";
    }
    return "Unit Joystick: Connected (direct)";
}

#else // !UNIT_JOYSTICK2

bool unitJoystick2Begin(bool) { return false; }
bool unitJoystick2Reconnect() { return false; }
bool unitJoystick2IsPresent() { return false; }
void unitJoystick2Poll() {}
void unitJoystick2ApplyInput() {}
String unitJoystick2StatusLabel() { return "Unit Joystick: N/A"; }
bool unitJoystick2ReadMove(int8_t &dx, int8_t &dy, int) {
    dx = 0;
    dy = 0;
    return false;
}
bool unitJoystick2ButtonPressed() { return false; }
bool unitJoystick2ButtonDown() { return false; }

#endif

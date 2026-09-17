#include "root/input/unit_joystick2.h"
#include "root/config/config.h"
#include "root/config/configPins.h"
#include "root/hal/bus_HAL.h"
#include "root/hal/pahub.h"
#include <globals.h>
#include <cstdio>
#include <cstdlib>

#if defined(UNIT_JOYSTICK2)
#include <m5_unit_joystick2.hpp>
#include <Wire.h>

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
#define UNIT_JOY_NAV_DEAD 5500
#endif

#ifndef UNIT_JOY_NAV_REPEAT_MS
#define UNIT_JOY_NAV_REPEAT_MS 200
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
static uint8_t i2cFailStreak = 0;
static uint16_t lastAdcX = 0;
static uint16_t lastAdcY = 0;
static int lastNx = 0;
static int lastNy = 0;
static const char *lastAction = "Idle";
static bool hidLastBtn = false;

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

static void fillConn(char *out, size_t n) {
    if (!present) {
        snprintf(out, n, "not found");
        return;
    }
    int8_t ch = pahubChannelFor(PahubDevJoystick2);
    if (pahubDeviceOnMux(PahubDevJoystick2)) snprintf(out, n, "PaHub ch%d", (int)ch);
    else snprintf(out, n, "direct");
}

static bool adcLooksValid(uint16_t x, uint16_t y) {
    if (x == 0 && y == 0) return false;
    if (x == 0xFFFF && y == 0xFFFF) return false;
    return true;
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
    int samples = 0;
    for (int i = 0; i < 24; i++) {
        uint16_t ax = 0;
        uint16_t ay = 0;
        joystick.get_joy_adc_16bits_value_xy(&ax, &ay);
        if (!adcLooksValid(ax, ay)) {
            delay(2);
            continue;
        }
        sx += ax;
        sy += ay;
        samples++;
        delay(2);
    }
    if (samples > 0) {
        centerX = (int)(sx / samples);
        centerY = (int)(sy / samples);
    } else {
        centerX = 32768;
        centerY = 32768;
    }
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
    hidLastBtn = false;
    btnDown = false;
    longPressFired = false;
    pendingSel = false;
    pendingEsc = false;
    pendingPrev = false;
    pendingNext = false;
    i2cFailStreak = 0;
    lastAction = "Idle";
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
    if (!adcLooksValid(adcX, adcY)) return false;
    int nx = (int)adcX - centerX;
    int ny = (int)adcY - centerY;
    if (kvxConfig.unitJoyInvertX) nx = -nx;
    if (kvxConfig.unitJoyInvertY) ny = -ny;
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
    return joystick.get_button_value() == 0;
}

bool unitJoystick2ButtonPressed() {
    if (!present) return false;
    PahubChannelGuard mux(PahubChannelGuard::forDevice(PahubDevJoystick2));
    bool down = joystick.get_button_value() == 0;
    bool edge = down && !hidLastBtn;
    hidLastBtn = down;
    return edge;
}

void unitJoystick2Poll() {
    if (!present) return;

    PahubTryGuard mux(PahubDevJoystick2);
    if (!mux.ok()) return;

    uint16_t adcX = 0;
    uint16_t adcY = 0;
    joystick.get_joy_adc_16bits_value_xy(&adcX, &adcY);
    lastAdcX = adcX;
    lastAdcY = adcY;
    if (!adcLooksValid(adcX, adcY)) {
        navAccumX = 0;
        navAccumY = 0;
        lastNx = 0;
        lastNy = 0;
        lastAction = "Idle";
        if (i2cFailStreak < 255) i2cFailStreak++;
        if (i2cFailStreak > 25) {
            present = false;
            releaseJoystickBus();
        }
        return;
    }
    i2cFailStreak = 0;

    int nx = (int)adcX - centerX;
    int ny = (int)adcY - centerY;
    if (kvxConfig.unitJoyInvertX) nx = -nx;
    if (kvxConfig.unitJoyInvertY) ny = -ny;

    trackJoystickCenter(nx, ny, adcX, adcY);
    nx = (int)adcX - centerX;
    ny = (int)adcY - centerY;
    if (kvxConfig.unitJoyInvertX) nx = -nx;
    if (kvxConfig.unitJoyInvertY) ny = -ny;
    lastNx = nx;
    lastNy = ny;

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
    lastAction = "Idle";
    if (now - lastNavMs >= (unsigned long)UNIT_JOY_NAV_REPEAT_MS) {
        const int threshold = UNIT_JOY_NAV_THRESHOLD;
        if (navAccumY <= -threshold) {
            RotaryNetSteps++;
            navAccumY += threshold;
            lastNavMs = now;
            lastAction = "Up";
        } else if (navAccumY >= threshold) {
            RotaryNetSteps--;
            navAccumY -= threshold;
            lastNavMs = now;
            lastAction = "Down";
        }
        if (navAccumX <= -threshold) {
            pendingPrev = true;
            navAccumX += threshold;
            lastNavMs = now;
            lastAction = "Left";
        } else if (navAccumX >= threshold) {
            pendingNext = true;
            navAccumX -= threshold;
            lastNavMs = now;
            lastAction = "Right";
        }
    } else if (!inDeadX || !inDeadY) {
        if (abs(ny) >= abs(nx)) lastAction = (ny < 0) ? "Up" : "Down";
        else lastAction = (nx < 0) ? "Left" : "Right";
    }

    bool down = joystick.get_button_value() == 0;
    if (down) {
        if (!btnDown) {
            btnDown = true;
            btnDownMs = now;
            longPressFired = false;
            joystick.set_rgb_color(0x404040);
        } else if (!longPressFired && (now - btnDownMs >= (unsigned long)UNIT_JOY_HOLD_MS)) {
            pendingEsc = true;
            longPressFired = true;
            lastAction = "Back";
        } else if (longPressFired) {
            lastAction = "Back";
        } else {
            lastAction = "Enter";
        }
    } else {
        if (btnDown) {
            btnDown = false;
            joystick.set_rgb_color(0x001400);
            if (!longPressFired) {
                pendingSel = true;
                lastAction = "Enter";
            }
        }
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

UnitJoystick2Debug unitJoystick2DebugSnapshot() {
    UnitJoystick2Debug d;
    d.present = present;
    d.adcX = lastAdcX;
    d.adcY = lastAdcY;
    d.centerX = centerX;
    d.centerY = centerY;
    d.nx = lastNx;
    d.ny = lastNy;
    d.inDead = abs(lastNx) < UNIT_JOY_NAV_DEAD && abs(lastNy) < UNIT_JOY_NAV_DEAD;
    d.btnDown = btnDown;
    d.holding = btnDown && longPressFired;
    d.action = lastAction;
    fillConn(d.conn, sizeof(d.conn));
    return d;
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
UnitJoystick2Debug unitJoystick2DebugSnapshot() { return {}; }
String unitJoystick2StatusLabel() { return "Unit Joystick: N/A"; }
bool unitJoystick2ReadMove(int8_t &dx, int8_t &dy, int) {
    dx = 0;
    dy = 0;
    return false;
}
bool unitJoystick2ButtonPressed() { return false; }
bool unitJoystick2ButtonDown() { return false; }

#endif

#ifndef __LED_CONTROL_H__
#define __LED_CONTROL_H__
#include <globals.h>

#ifdef HAS_RGB_LED
#include <Arduino.h>
#include <FastLED.h>

#define LED_EFFECT_SOLID 0
#define LED_COLOR_BREATHE 1
#define LED_EFFECT_COLOR_CYCLE 2
#define LED_EFFECT_COLOR_WHEEL 3
#define LED_EFFECT_CHASE 4
#define LED_EFFECT_CHASE_TAIL 5
#define LED_EFFECT_RAINBOW_CHASE 6
#define LED_EFFECT_RAINBOW_BREATHE 7
#define LED_EFFECT_DISCO 8
#define LED_EFFECT_FIRE 9
#define LED_EFFECT_BATTERY_STATUS 10

CRGB hsvToRgb(uint16_t h, uint8_t s, uint8_t v);
uint32_t alterOneColorChannel(uint32_t color, uint16_t newR, uint16_t newG, uint16_t newB);
CRGB batteryStatusLedColor(int percent);

void beginLed();
void blinkLed(int blinkTime = 50);

void setLedColor(CRGB color);
void setLedEffect(int effect);
void setLedColorConfig();
void setCustomColorMenu();
void setCustomColorSettingMenuR();
void setCustomColorSettingMenuG();
void setCustomColorSettingMenuB();
void setLedEffectConfig();
void setLedEffectSpeedConfig();
void setLedEffectDirectionConfig();
void ledSetup();
void ledEffects(bool enable);
void ledPauseEffects(bool pause);
void ledPreviewMode(bool enable);
void setLedBrightness(int value);
void setLedBrightnessConfig();

#define LED_STATUS_BOOT 0
#define LED_STATUS_IDLE 1
#define LED_STATUS_BUSY 2
#define LED_STATUS_OFF 3

void ledSetStatus(int status);
void ledBootTick(bool purple);
void ledRestoreStatus();
int ledGetStatus();
void ledSuppressStatus(bool suppress);
bool ledIsStatusSuppressed();
void ledShowApp(uint8_t r, uint8_t g, uint8_t b, uint8_t bright);

#else
inline void blinkLed(int blinkTime = 50) {};
inline void ledSetStatus(int status) { (void)status; }
inline void ledBootTick(bool purple) { (void)purple; }
inline void ledRestoreStatus() {}
inline int ledGetStatus() { return 1; }
inline void ledSuppressStatus(bool suppress) { (void)suppress; }
inline bool ledIsStatusSuppressed() { return false; }
inline void ledEffects(bool enable) { (void)enable; }
inline void ledShowApp(uint8_t r, uint8_t g, uint8_t b, uint8_t bright) {
    (void)r;
    (void)g;
    (void)b;
    (void)bright;
}
#define LED_STATUS_BOOT 0
#define LED_STATUS_IDLE 1
#define LED_STATUS_BUSY 2
#define LED_STATUS_OFF 3
#endif

#endif

#include "root/hal/led_control.h"

#include "root/ui/display.h"
#include "root/app/utils.h"
#include <globals.h>
#include <interface.h>
#ifdef HAS_RGB_LED
#define FASTLED_RMT_BUILTIN_DRIVER 1  // Use the ESP32 RMT built-in driver
#define FASTLED_RMT_MAX_CHANNELS 1    // Maximum number of RMT channels
#define FASTLED_ESP32_RMT_CHANNEL_0 0 // Use RMT channel 0 for FastLED
#include <FastLED.h>
#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

CRGB leds[LED_COUNT];

bool isPreviewLed = false;
CRGB previewLedColor;
int previewLedEffect;
int previewLedEffectSpeed;
int previewLedEffectDirection;

CRGB hsvToRgb(uint16_t h, uint8_t s, uint8_t v) {
    uint8_t f = (h % 60) * 255 / 60;
    uint8_t p = (255 - s) * (uint16_t)v / 255;
    uint8_t q = (255 - f * (uint16_t)s / 255) * (uint16_t)v / 255;
    uint8_t t = (255 - (255 - f) * (uint16_t)s / 255) * (uint16_t)v / 255;
    uint8_t r = 0, g = 0, b = 0;
    switch ((h / 60) % 6) {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        case 5:
            r = v;
            g = p;
            b = q;
            break;
    }

    CRGB c;
    c.red = r;
    c.green = g;
    c.blue = b;
    return c;
}

uint32_t alterOneColorChannel(uint32_t color, uint16_t newR, uint16_t newG, uint16_t newB) {
    uint8_t r = ((color >> 16) & 0xFF);
    uint8_t g = ((color >> 8) & 0xFF);
    uint8_t b = (color & 0xFF);

    if (newR != 256) r = newR;
    if (newG != 256) g = newG;
    if (newB != 256) b = newB;

    return ((r << 16) | (g << 8) | b);
}

static uint8_t lerpU8(uint8_t a, uint8_t b, int t, int span) {
    if (span <= 0) return b;
    if (t <= 0) return a;
    if (t >= span) return b;
    return (uint8_t)(((int)a * (span - t) + (int)b * t) / span);
}

static CRGB lerpRgb(CRGB a, CRGB b, int t, int span) {
    return CRGB(lerpU8(a.r, b.r, t, span), lerpU8(a.g, b.g, t, span), lerpU8(a.b, b.b, t, span));
}

CRGB batteryStatusLedColor(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const CRGB red(255, 0, 0);
    const CRGB orange(255, 140, 0);
    const CRGB yellow(255, 255, 0);
    const CRGB ygreen(80, 255, 0);
    const CRGB green(0, 255, 0);
    const CRGB purple(0x96, 0x00, 0x64);
    if (percent <= 15) return red;
    if (percent <= 50) return lerpRgb(red, orange, percent - 15, 35);
    if (percent <= 62) return lerpRgb(orange, yellow, percent - 50, 12);
    if (percent <= 75) return lerpRgb(yellow, ygreen, percent - 62, 13);
    if (percent <= 90) return lerpRgb(ygreen, green, percent - 75, 15);
    if (percent < 95) return green;
    return purple; // 95–100%
}

TaskHandle_t ledEffectTaskHandle = NULL;
static volatile bool ledEffectsPaused = false;

void ledPauseEffects(bool pause) { ledEffectsPaused = pause; }

static int ledStatusNow = LED_STATUS_IDLE;
static int ledStatusBeforeOff = LED_STATUS_IDLE;
static bool ledStatusHeld = false;

static CRGB ledStatusColor(int /*status*/) {
    return CRGB(0x96, 0x00, 0x64);
}

static CRGB ledColorFromConfig() { return CRGB((uint32_t)kvxConfig.ledColor); }

static void ledForceOff() {
    ledPauseEffects(true);
    fill_solid(leds, LED_COUNT, CRGB::Black);
    FastLED.setBrightness(0);
    FastLED.show();
}

static void ledPreviewBrightness(int value) {
    if (value <= 0) {
        ledForceOff();
        return;
    }
    FastLED.setBrightness(255 * value / 100);
    if (kvxConfig.ledEffect > LED_EFFECT_SOLID) {
        ledPauseEffects(false);
        FastLED.show();
    } else {
        fill_solid(leds, LED_COUNT, ledColorFromConfig());
        FastLED.show();
    }
}

/** Apply saved color / effect / brightness (mainscreen idle, after settings). */
static void ledApplyUserConfig() {
    if (kvxConfig.ledBright == 0) {
        ledForceOff();
        return;
    }
    FastLED.setBrightness(255 * kvxConfig.ledBright / 100);
    if (kvxConfig.ledEffect > LED_EFFECT_SOLID) {
        ledEffects(true);
        ledPauseEffects(false);
    } else {
        ledEffects(false);
        fill_solid(leds, LED_COUNT, ledColorFromConfig());
        FastLED.show();
    }
}

void ledSuppressStatus(bool suppress) {
    ledStatusHeld = suppress;
    if (suppress) ledPauseEffects(true);
}

bool ledIsStatusSuppressed() { return ledStatusHeld; }

void ledShowApp(uint8_t r, uint8_t g, uint8_t b, uint8_t bright) {
    if (isPreviewLed) return;
    ledPauseEffects(true);
    fill_solid(leds, LED_COUNT, CRGB(r, g, b));
    FastLED.setBrightness(bright);
    FastLED.show();
}

void ledSetStatus(int status) {
    if (isPreviewLed) return;

    if (status == LED_STATUS_OFF) {
        if (ledStatusNow != LED_STATUS_OFF) ledStatusBeforeOff = ledStatusNow;
        ledStatusNow = LED_STATUS_OFF;
        ledForceOff();
        return;
    }

    ledStatusNow = status;
    if (kvxConfig.ledBright == 0) {
        ledForceOff();
        return;
    }
    if (ledStatusHeld) return;

    // Idle: honor LED Color / Effect / Brightness from settings.
    // Busy/boot: purple (or green boot tick via ledBootTick) as UI feedback.
    if (status == LED_STATUS_IDLE) {
        ledApplyUserConfig();
        return;
    }

    ledPauseEffects(true);
    fill_solid(leds, LED_COUNT, ledStatusColor(status));
    FastLED.setBrightness(255 * kvxConfig.ledBright / 100);
    FastLED.show();
}

void ledBootTick(bool purple) {
    if (isPreviewLed || kvxConfig.ledBright == 0) return;
    ledPauseEffects(true);
    fill_solid(leds, LED_COUNT, purple ? CRGB(0x96, 0x00, 0x64) : CRGB::Green);
    FastLED.setBrightness(255 * kvxConfig.ledBright / 100);
    FastLED.show();
}

void ledRestoreStatus() {
    int restore = ledStatusBeforeOff;
    if (restore == LED_STATUS_OFF) restore = LED_STATUS_IDLE;
    ledSetStatus(restore);
}

int ledGetStatus() { return ledStatusNow; }

void ledEffectTask(void *pvParameters) {
    short hueStep = 360 / LED_COUNT;
    short offset = 0;
    int currentLED = 0;
    int frame = 0;
    uint64_t start_time = esp_timer_get_time() / 1000;
    while (1) {
        if (ledEffectsPaused) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        CRGB baseColor = isPreviewLed ? previewLedColor : kvxConfig.ledColor;
        int ledEffect = isPreviewLed ? previewLedEffect : kvxConfig.ledEffect;
        int ledEffectSpeed = isPreviewLed ? previewLedEffectSpeed : kvxConfig.ledEffectSpeed;
        int ledEffectDirection = isPreviewLed ? previewLedEffectDirection : kvxConfig.ledEffectDirection;

        if (ledEffect == LED_EFFECT_COLOR_CYCLE || ledEffect == LED_EFFECT_COLOR_WHEEL) {
            short delayMs = 50;

#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0)) {
                offset = (offset + (static_cast<short>(20 / 1000.0f * 360.0f)) * EncoderLedChange) % 360;
                EncoderLedChange = 0;
            } else if (ledEffectSpeed < 11) {
                float speed = 0.2f * ledEffectSpeed;
                offset = (offset + static_cast<short>(speed * delayMs / 1000.0f * 360.0f)) % 360;
            }
#else
            float speed = 0.2f * ledEffectSpeed;
            offset = (offset + static_cast<short>(speed * delayMs / 1000.0f * 360.0f)) % 360;
#endif
            if (ledEffect == LED_EFFECT_COLOR_CYCLE) {
                short hue = ((offset * -ledEffectDirection) % 360 + 360) % 360;
                // Serial.printf("LED Effect Cycle: hue=%d, offset=%d", hue, offset);
                fill_solid(leds, LED_COUNT, hsvToRgb(hue, 255, 255));
            } else if (ledEffect == LED_EFFECT_COLOR_WHEEL) {
                for (uint16_t i = 0; i < LED_COUNT; ++i) {
                    short hue = ((offset + i * -ledEffectDirection * hueStep) % 360 + 360) % 360;
                    // Serial.printf("LED Effect Wheel: i=%d, hue=%d\n", i, hue);
                    leds[i] = hsvToRgb(hue, 255, 255);
                }
            }

        } else if (ledEffect == LED_COLOR_BREATHE) {

            float phase;
#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) || (ledEffectSpeed < 11)) {
                if ((ledEffectSpeed == 11 && EncoderLedChange != 0)) {
                    phase = sinf(frame / 20.0f * PI);
                    frame += EncoderLedChange;
                    EncoderLedChange = 0;
                } else {
                    float time = millis() / 1000.0f;
                    float speed = 0.2f * ledEffectSpeed;
                    phase = sinf(time * speed * PI);
                }
#else
            float time = millis() / 1000.0f;
            float speed = 0.2f * ledEffectSpeed;
            phase = sinf(time * speed * PI);
#endif
                uint8_t value = (uint8_t)((phase + 1.0f) * 127.5f);

                for (int i = 0; i < LED_COUNT; i++) {
                    leds[i] = CRGB(
                        (baseColor.r * value) / 255, (baseColor.g * value) / 255, (baseColor.b * value) / 255
                    );
                }
#ifdef HAS_ENCODER_LED
            }
#endif

        } else if (ledEffect == LED_EFFECT_BATTERY_STATUS) {
            static int cachedPct = 100;
            static unsigned long lastBatMs = 0;
            unsigned long nowMs = millis();
            if (lastBatMs == 0 || nowMs - lastBatMs >= 1000) {
                lastBatMs = nowMs;
                int p = getBattery();
                if (p < 0) p = 0;
                if (p > 100) p = 100;
                cachedPct = p;
            }
            CRGB c = batteryStatusLedColor(cachedPct);
            if (cachedPct <= 5 && ((nowMs / 500) % 2 == 0)) c = CRGB::Black;
            fill_solid(leds, LED_COUNT, c);

#if LED_COUNT > 1
        } else if (ledEffect == LED_EFFECT_CHASE || ledEffect == LED_EFFECT_CHASE_TAIL) {
            uint8_t cycleFrames = 11 - ledEffectSpeed;

#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) ||
                (ledEffectSpeed < 11 && frame % cycleFrames == 0)) {
                if ((ledEffectSpeed == 11 && EncoderLedChange != 0)) {
                    currentLED = (currentLED + EncoderLedChange + LED_COUNT) % LED_COUNT;
                    EncoderLedChange = 0;
                } else {
                    currentLED = (currentLED + ledEffectDirection + LED_COUNT) % LED_COUNT;
                }
#else
            if (frame % cycleFrames == 0) {
                currentLED = (currentLED + ledEffectDirection + LED_COUNT) % LED_COUNT;
#endif

                fill_solid(leds, LED_COUNT, CRGB::Black);

                if (ledEffect == LED_EFFECT_CHASE) {
                    leds[currentLED] = baseColor;
                } else {
                    for (int i = 1; i < LED_COUNT; ++i) {
                        int index = (currentLED - ledEffectDirection * i + LED_COUNT) % LED_COUNT;

                        float fade = powf(0.6f, i);
                        leds[index].r = baseColor.r * fade;
                        leds[index].g = baseColor.g * fade;
                        leds[index].b = baseColor.b * fade;
                    }
                }
            }
            frame++;
#endif
        } else if (ledEffect == LED_EFFECT_RAINBOW_CHASE) {
            uint8_t cycleFrames = 11 - ledEffectSpeed;

#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) ||
                (ledEffectSpeed < 11 && frame % cycleFrames == 0)) {
                if ((ledEffectSpeed == 11 && EncoderLedChange != 0)) {
                    currentLED = (currentLED + EncoderLedChange + LED_COUNT) % LED_COUNT;
                    EncoderLedChange = 0;
                } else {
                    currentLED = (currentLED + ledEffectDirection + LED_COUNT) % LED_COUNT;
                }
#else
            if (frame % cycleFrames == 0) {
                currentLED = (currentLED + ledEffectDirection + LED_COUNT) % LED_COUNT;
#endif
                fill_solid(leds, LED_COUNT, CRGB::Black);

                for (int i = 0; i < LED_COUNT; ++i) {
                    int index = (currentLED + i + LED_COUNT) % LED_COUNT;
                    short hue = (i * 360 / LED_COUNT) % 360;
                    float fade = powf(0.7f, i);
                    CRGB rainbowColor = hsvToRgb(hue, 255, 255);
                    leds[index].r = rainbowColor.r * fade;
                    leds[index].g = rainbowColor.g * fade;
                    leds[index].b = rainbowColor.b * fade;
                }
            }
            frame++;

        } else if (ledEffect == LED_EFFECT_RAINBOW_BREATHE) {
            float phase;
#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) || (ledEffectSpeed < 11)) {
                if ((ledEffectSpeed == 11 && EncoderLedChange != 0)) {
                    phase = sinf(frame / 20.0f * PI);
                    frame += EncoderLedChange;
                    EncoderLedChange = 0;
                } else {
                    float time = millis() / 1000.0f;
                    float speed = 0.2f * ledEffectSpeed;
                    phase = sinf(time * speed * PI);
                    offset = (offset + static_cast<short>(speed * 1.0f)) % 360;
                }
#else
            float time = millis() / 1000.0f;
            float speed = 0.2f * ledEffectSpeed;
            phase = sinf(time * speed * PI);
            offset = (offset + static_cast<short>(speed * 1.0f)) % 360;
#endif
                uint8_t value = (uint8_t)((phase + 1.0f) * 127.5f);

                for (int i = 0; i < LED_COUNT; i++) {
                    short hue = ((offset + i * -ledEffectDirection * hueStep) % 360 + 360) % 360;
                    CRGB rainbowColor = hsvToRgb(hue, 255, 255);
                    leds[i] = CRGB(
                        (rainbowColor.r * value) / 255,
                        (rainbowColor.g * value) / 255,
                        (rainbowColor.b * value) / 255
                    );
                }
#ifdef HAS_ENCODER_LED
            }
#endif

        } else if (ledEffect == LED_EFFECT_DISCO) {
            uint8_t cycleFrames = 11 - ledEffectSpeed;
#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) ||
                (ledEffectSpeed < 11 && frame % cycleFrames == 0)) {
                if (ledEffectSpeed == 11 && EncoderLedChange != 0) { EncoderLedChange = 0; }
#else
            if (frame % cycleFrames == 0) {
#endif
                for (int i = 0; i < LED_COUNT; i++) {
                    short randomHue = random(0, 360);
                    leds[i] = hsvToRgb(randomHue, 255, 255);
                }
            }
            frame++;
        } else if (ledEffect == LED_EFFECT_FIRE) {
            uint8_t cycleFrames = 11 - ledEffectSpeed;
#ifdef HAS_ENCODER_LED
            if ((ledEffectSpeed == 11 && EncoderLedChange != 0) ||
                (ledEffectSpeed < 11 && frame % cycleFrames == 0)) {
                if (ledEffectSpeed == 11 && EncoderLedChange != 0) { EncoderLedChange = 0; }
#else
            if (frame % cycleFrames == 0) {
#endif
                for (int i = 0; i < LED_COUNT; i++) {
                    uint8_t flicker = random(150, 255);
                    uint8_t isRed = random(0, 2);
                    if (isRed) {
                        leds[i] = CRGB(flicker, random(0, flicker / 3), 0);
                    } else {
                        leds[i] = CRGB(255, random(flicker / 2, flicker), 0);
                    }
                }
            }
            frame++;
        }

        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void beginLed() {
#ifdef RGB_LED_CLK
    FastLED.addLeds<LED_TYPE, RGB_LED, RGB_LED_CLK, LED_ORDER>(leds, LED_COUNT);
#else
    FastLED.addLeds<LED_TYPE, RGB_LED, LED_ORDER>(leds, LED_COUNT);
#endif

    /* The default FastLED driver takes over control of the RMT interrupt
     * handler, making it hard to use the RMT device for other
     * (non-FastLED) purposes. You can change it's behavior to use the ESP
     * core driver instead, allowing other RMT applications to
     * co-exist. To switch to this mode, add the following directive
     * before you include FastLED.h:
     *
     *      #define FASTLED_RMT_BUILTIN_DRIVER 1
     *  RMT is also used for RF Spectrum (and for RF readings in the future),
     *  So it is needed to restart the driver in case it had been turned off
     *  by the RF functions, in this case, we are restarting it all the time
     */
    // -- RMT configuration for transmission

    // These configurations made T-Embed (non CC1101) stop working
    // Commented to test if with the FASTLED_RMT_MAX_CHANNELS 1 was sufficient for the other devices to
    // work LED and RF Spectrum and RAW capture and it is working well without it for now.. So I'll keep
    // the code below for the case we find some issue and need to rollback

    /*
        rmt_config_t rmt_tx;
        memset(&rmt_tx, 0, sizeof(rmt_config_t));
        rmt_tx.channel = rmt_channel_t(FASTLED_ESP32_RMT_CHANNEL_0);
        rmt_tx.rmt_mode = RMT_MODE_TX;
        rmt_tx.gpio_num = (gpio_num_t)RGB_LED;
        rmt_tx.mem_block_num = 2;
        rmt_tx.clk_div = 2;
        rmt_tx.tx_config.loop_en = false;
        rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
        rmt_tx.tx_config.carrier_en = false;
        rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
        rmt_tx.tx_config.idle_output_en = true;

        // -- Apply the configuration
        rmt_config(&rmt_tx);
        rmt_driver_uninstall(rmt_channel_t(FASTLED_ESP32_RMT_CHANNEL_0));
        rmt_driver_install(rmt_channel_t(FASTLED_ESP32_RMT_CHANNEL_0), 0, 0);
    */
    ledSetup();

    setLedBrightness(kvxConfig.ledBright);
}

void blinkLed(int blinkTime) {
    if (!kvxConfig.ledBlinkEnabled) return;

    int ledBrightFrom = kvxConfig.ledBright;
    int ledBrightTo = ledBrightFrom > 0 ? 0 : 50;

    beginLed();
    setLedBrightness(ledBrightTo);
    ioExpander.turnPinOnOff(IO_EXP_VIBRO, HIGH);
    delay(blinkTime);
    setLedBrightness(ledBrightFrom);
    ioExpander.turnPinOnOff(IO_EXP_VIBRO, LOW);
}

void setLedColor(CRGB color) {
    if (isPreviewLed) previewLedColor = color;
    if (isPreviewLed && previewLedEffect != LED_EFFECT_SOLID) {
#ifdef HAS_ENCODER_LED
        EncoderLedChange = 1;
#endif
    } else {
        for (int i = 0; i < LED_COUNT; i++) leds[i] = color;
        FastLED.show();
    }
}

void setLedEffect(int effect) {
    previewLedEffect = effect;
#ifdef HAS_ENCODER_LED
    if (isPreviewLed && previewLedEffect != LED_EFFECT_SOLID) { EncoderLedChange = 1; }
#endif
}

void setLedBrightness(int value) {
    value = max(0, min(100, value));
    int bright = 255 * value / 100;
    FastLED.setBrightness(bright);
    FastLED.show();
}

#define BrucePurple 9830500 // Custom purple color for Bruce
// TODO: 3852441 -> 3849837
void setLedColorConfig() {
    ledPreviewMode(true);

    struct ColorMapping {
        const char *name;
        CRGB color;
    };

    constexpr ColorMapping colorMappings[] = {
        {"OFF",        CRGB::Black    },
        {"Default",    BrucePurple    },
        {"White",      CRGB::White    },
        {"Red",        CRGB::Red      },
        {"Orange",     CRGB::OrangeRed},
        {"Yellow",     CRGB::Yellow   },
        {"Lime Green", CRGB::LawnGreen},
        {"Green",      CRGB::Green    },
        {"Cyan",       CRGB::Cyan     },
        {"Blue",       CRGB::Blue     },
        {"Magenta",    CRGB::Magenta  },
        {"Pink",       CRGB::DeepPink },
    };

    while (1) {
        options.clear();
        int idx = sizeof(colorMappings) / sizeof(colorMappings[0]);
        int i = 0;
        static CRGB colorStorage[12];
        for (const auto &mapping : colorMappings) {
            const uint32_t mapped =
                ((uint32_t)mapping.color.r << 16) | ((uint32_t)mapping.color.g << 8) |
                ((uint32_t)mapping.color.b);
            if (kvxConfig.ledColor == mapped) { idx = i; }
            colorStorage[i] = mapping.color;

            options.emplace_back(
                mapping.name,
                [mapped, color = mapping.color]() {
                    kvxConfig.setLedColor(mapped);
                    setLedColor(color);
                },
                idx == i,
                [](void *pointer, bool shouldRender) {
                    setLedColor(*(CRGB *)(pointer));
                    return false;
                },
                &colorStorage[i]
            );
            ++i;
        }

        options.push_back(
            {"Custom Color",
             [=]() { setCustomColorMenu(); },
             idx == sizeof(colorMappings) / sizeof(colorMappings[0]),
             [](void *pointer, bool shouldRender) {
                 setLedColor(ledColorFromConfig());
                 return false;
             }}
        );

        addOptionToMainMenu();

        int selectedOption = loopOptions(options, idx);
        if (selectedOption == -1 || selectedOption == options.size() - 1) {
            ledPreviewMode(false);
            ledSetup();
            return;
        }
    }
}

void setCustomColorMenu() {
    while (1) {
        options = {
            {"Red Channel",   setCustomColorSettingMenuR},
            {"Green Channel", setCustomColorSettingMenuG},
            {"Blue Channel",  setCustomColorSettingMenuB},
            {"Back",          [=]() {}                  },
        };

        int selectedOption = loopOptions(options);
        if (selectedOption == -1 || selectedOption == options.size() - 1) return;
    }
}

void setCustomColorSettingMenu(int rgb, std::function<uint32_t(uint32_t, int)> colorGenerator) {
    uint32_t originalColor = kvxConfig.ledColor;

    options.clear();

    static auto hoverFunction = [](void *pointer, bool shouldRender) -> bool {
        uint32_t colorToSet = *static_cast<uint32_t *>(pointer);
        setLedColor(colorToSet);
        previewLedColor = CRGB(colorToSet);
        return false;
    };

    static uint32_t colorStorage[(int)(255 / LED_COLOR_STEP) + 1];
    short selectedIndex = 0;
    short colorPart = 0;
    short i = 0;
    short index = 0;

    if (rgb == 1) {
        colorPart = (originalColor >> 16) & 0xFF;
    } else if (rgb == 2) {
        colorPart = (originalColor >> 8) & 0xFF;
    } else {
        colorPart = originalColor & 0xFF;
    }

    while (i <= 255) {
        if (i % LED_COLOR_STEP == 0 || i == 255) {
            uint32_t updatedColor = colorGenerator(originalColor, i);
            colorStorage[index] = updatedColor;

            // Select nearest color step rounding down
            if (colorPart >= i && colorPart < i + LED_COLOR_STEP) selectedIndex = index;

            options.emplace_back(
                String(i),
                [updatedColor]() {
                    kvxConfig.setLedColor(updatedColor);
                    setLedColor(CRGB(updatedColor));
                    previewLedColor = CRGB(updatedColor);
                },
                selectedIndex == index,
                hoverFunction,
                &colorStorage[index]
            );
            ++index;
        }
        ++i;
    }

    addOptionToMainMenu();

    int selectedOption = loopOptions(options, MENU_TYPE_SUBMENU, "", selectedIndex);
    if (selectedOption == -1 || selectedOption == options.size() - 1) {
        setLedColor(originalColor);
        return;
    }
}

void setCustomColorSettingMenuR() {
    setCustomColorSettingMenu(1, [](uint32_t baseColor, int i) {
        return alterOneColorChannel(baseColor, i, 256, 256);
    });
}

void setCustomColorSettingMenuG() {
    setCustomColorSettingMenu(2, [](uint32_t baseColor, int i) {
        return alterOneColorChannel(baseColor, 256, i, 256);
    });
}

void setCustomColorSettingMenuB() {
    setCustomColorSettingMenu(3, [](uint32_t baseColor, int i) {
        return alterOneColorChannel(baseColor, 256, 256, i);
    });
}

void setLedEffectConfig() {
    ledPreviewMode(true);

    while (1) {
        auto applyEffect = [](int effect) {
            kvxConfig.setLedEffect(effect);
            setLedEffect(effect);
            if (effect == LED_EFFECT_SOLID) setLedColor(ledColorFromConfig());
        };

        options = {
            {"Solid Color",
             [=]() { applyEffect(LED_EFFECT_SOLID); },
             kvxConfig.ledEffect == LED_EFFECT_SOLID,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_SOLID);
                 setLedColor(ledColorFromConfig());
                 return false;
             }},
            {"Breathe",
             [=]() { applyEffect(LED_COLOR_BREATHE); },
             kvxConfig.ledEffect == LED_COLOR_BREATHE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_COLOR_BREATHE);
                 return false;
             }},
            {"Color Cycle",
             [=]() { applyEffect(LED_EFFECT_COLOR_CYCLE); },
             kvxConfig.ledEffect == LED_EFFECT_COLOR_CYCLE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_COLOR_CYCLE);
                 return false;
             }},
            {"Battery Status",
             [=]() { applyEffect(LED_EFFECT_BATTERY_STATUS); },
             kvxConfig.ledEffect == LED_EFFECT_BATTERY_STATUS,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_BATTERY_STATUS);
                 return false;
             }},
#if LED_COUNT > 1
            {"Color Wheel",
             [=]() { applyEffect(LED_EFFECT_COLOR_WHEEL); },
             kvxConfig.ledEffect == LED_EFFECT_COLOR_WHEEL,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_COLOR_WHEEL);
                 return false;
             }},
            {"Chase",
             [=]() { applyEffect(LED_EFFECT_CHASE); },
             kvxConfig.ledEffect == LED_EFFECT_CHASE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_CHASE);
                 return false;
             }},
            {"Chase Tail",
             [=]() { applyEffect(LED_EFFECT_CHASE_TAIL); },
             kvxConfig.ledEffect == LED_EFFECT_CHASE_TAIL,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_CHASE_TAIL);
                 return false;
             }},
            {"Rainbow Chase",
             [=]() { applyEffect(LED_EFFECT_RAINBOW_CHASE); },
             kvxConfig.ledEffect == LED_EFFECT_RAINBOW_CHASE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_RAINBOW_CHASE);
                 return false;
             }},
            {"Rainbow Breathe",
             [=]() { applyEffect(LED_EFFECT_RAINBOW_BREATHE); },
             kvxConfig.ledEffect == LED_EFFECT_RAINBOW_BREATHE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_RAINBOW_BREATHE);
                 return false;
             }},
            {"Disco",
             [=]() { applyEffect(LED_EFFECT_DISCO); },
             kvxConfig.ledEffect == LED_EFFECT_DISCO,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_DISCO);
                 return false;
             }},
            {"Fire",
             [=]() { applyEffect(LED_EFFECT_FIRE); },
             kvxConfig.ledEffect == LED_EFFECT_FIRE,
             [](void *pointer, bool shouldRender) {
                 setLedEffect(LED_EFFECT_FIRE);
                 return false;
             }},
#endif
            {"Config - Speed",
             setLedEffectSpeedConfig,
             false,
             [](void *pointer, bool shouldRender) {
                 previewLedEffect = kvxConfig.ledEffect;
                 previewLedEffectSpeed = kvxConfig.ledEffectSpeed;
                 previewLedEffectDirection = kvxConfig.ledEffectDirection;
                 return false;
             }},
            {"Config - Direction",
             setLedEffectDirectionConfig,
             false,
             [](void *pointer, bool shouldRender) {
                 previewLedEffect = kvxConfig.ledEffect;
                 previewLedEffectSpeed = kvxConfig.ledEffectSpeed;
                 previewLedEffectDirection = kvxConfig.ledEffectDirection;
                 return false;
             }},
        };

        addOptionToMainMenu();

        int selectedOption = loopOptions(options, kvxConfig.ledEffect);
        if (selectedOption == -1 || selectedOption == options.size() - 1) {
            ledPreviewMode(false);
            ledSetup();
            return;
        }
    }
}

void setLedEffectSpeedConfig() {
    options.clear();

    static auto hoverFunction = [](void *pointer, bool shouldRender) -> bool {
        int speedToSet = *static_cast<int *>(pointer);
        previewLedEffectSpeed = speedToSet + 1;
        return false;
    };

#ifdef HAS_ENCODER_LED
    static int speedStorage[11];
#else
    static int speedStorage[10];
#endif
    int i = 0;
    while (i < 10) {
        speedStorage[i] = i;

        String label = String(i + 1);
        bool isSelected = (kvxConfig.ledEffectSpeed == i + 1);

        options.emplace_back(
            label,
            [i]() {
                kvxConfig.setLedEffectSpeed(i + 1);
                previewLedEffectSpeed = i + 1;
            },
            (kvxConfig.ledEffectSpeed == i + 1),
            hoverFunction,
            &speedStorage[i]
        );
        ++i;
    }

#ifdef HAS_ENCODER_LED
    speedStorage[10] = 11;
    options.emplace_back(
        "Sync To Encoder",
        []() {
            kvxConfig.setLedEffectSpeed(11);
            previewLedEffectSpeed = 11;
        },
        (kvxConfig.ledEffectSpeed == 11),
        hoverFunction,
        &speedStorage[10]
    );
#endif

    addOptionToMainMenu();

    int selectedOption = loopOptions(options, kvxConfig.ledEffectSpeed - 1);
    if (selectedOption == -1 || selectedOption == options.size() - 1) {
        previewLedEffectSpeed = kvxConfig.ledEffectSpeed;
        return;
    }
}

void setLedEffectDirectionConfig() {
    options = {
        {"Clockwise",
         [=]() {
             kvxConfig.setLedEffectDirection(1);
             previewLedEffectDirection = 1;
         },
         kvxConfig.ledEffectDirection == 1,
         [](void *pointer, bool shouldRender) {
             previewLedEffectDirection = 1;
             return false;
         }},
        {"Anti-Clockwise",
         [=]() {
             kvxConfig.setLedEffectDirection(-1);
             previewLedEffectDirection = -1;
         },
         kvxConfig.ledEffectDirection == -1,
         [](void *pointer, bool shouldRender) {
             previewLedEffectDirection = -1;
             return false;
         }},
    };

    addOptionToMainMenu();

    int selectedOption = loopOptions(options, (kvxConfig.ledEffectDirection == 1) ? 0 : 1);
    if (selectedOption == -1 || selectedOption == options.size() - 1) {
        previewLedEffectDirection = kvxConfig.ledEffectDirection;
        return;
    }
}

void ledSetup() { ledApplyUserConfig(); }

void ledEffects(bool enable) {
    if (enable) {
        if (ledEffectTaskHandle == NULL) {
            xTaskCreate(ledEffectTask, "LedEffect", 2048, NULL, 1, &ledEffectTaskHandle);
        }
        ledPauseEffects(false);
    } else {
        if (ledEffectTaskHandle != NULL) {
            vTaskDelete(ledEffectTaskHandle);
            ledEffectTaskHandle = NULL;
        }
    }
}

void ledPreviewMode(bool enable) {
    isPreviewLed = enable;
    if (enable) {
        previewLedColor = ledColorFromConfig();
        previewLedEffect = kvxConfig.ledEffect;
        previewLedEffectSpeed = kvxConfig.ledEffectSpeed;
        previewLedEffectDirection = kvxConfig.ledEffectDirection;
        ledEffects(true);
        if (previewLedEffect == LED_EFFECT_SOLID) setLedColor(previewLedColor);
    } else {
        // Leave effect task state to ledSetup() / ledSetStatus(IDLE).
        ledEffects(kvxConfig.ledEffect > LED_EFFECT_SOLID && kvxConfig.ledBright > 0);
    }
}

void setLedBrightnessConfig() {
    // Block status overrides so click applies brightness immediately.
    const bool wasPreview = isPreviewLed;
    isPreviewLed = true;

    int idx = 0;
    if (kvxConfig.ledBright == 0) idx = 0;
    else if (kvxConfig.ledBright == 10) idx = 1;
    else if (kvxConfig.ledBright == 25) idx = 2;
    else if (kvxConfig.ledBright == 50) idx = 3;
    else if (kvxConfig.ledBright == 75) idx = 4;
    else if (kvxConfig.ledBright == 100) idx = 5;

    auto applyBright = [](int value) {
        kvxConfig.setLedBright(value);
        ledPreviewBrightness(value);
    };

    ledPreviewBrightness(kvxConfig.ledBright);

    options = {
        {"OFF",
         [=]() { applyBright(0); },
         kvxConfig.ledBright == 0,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(0);
             return false;
         }},
        {"10 %",
         [=]() { applyBright(10); },
         kvxConfig.ledBright == 10,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(10);
             return false;
         }},
        {"25 %",
         [=]() { applyBright(25); },
         kvxConfig.ledBright == 25,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(25);
             return false;
         }},
        {"50 %",
         [=]() { applyBright(50); },
         kvxConfig.ledBright == 50,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(50);
             return false;
         }},
        {"75 %",
         [=]() { applyBright(75); },
         kvxConfig.ledBright == 75,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(75);
             return false;
         }},
        {"100%",
         [=]() { applyBright(100); },
         kvxConfig.ledBright == 100,
         [](void *pointer, bool shouldRender) {
             ledPreviewBrightness(100);
             return false;
         }},
    };
    addOptionToMainMenu();

    loopOptions(options, idx);
    isPreviewLed = wasPreview;
    ledApplyUserConfig();
}
#endif

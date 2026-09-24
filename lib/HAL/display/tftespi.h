#ifndef LIB_HAL_TFTESPI_H
#define LIB_HAL_TFTESPI_H
#include <pins_arduino.h>
#ifdef USE_TFT_ESPI
#include <TFT_eSPI.h>

#include "tft_defines.h"
class tft_display : private TFT_eSPI {
public:
    explicit tft_display(int16_t _W = TFT_WIDTH, int16_t _H = TFT_HEIGHT);
    friend class tft_sprite;
    friend class tft_logger;

    using TFT_eSPI::begin;
    using TFT_eSPI::color565;
    using TFT_eSPI::init;
    using TFT_eSPI::invertDisplay;
    using TFT_eSPI::print;
    using TFT_eSPI::printf;
    using TFT_eSPI::println;
    using TFT_eSPI::sleep;
    using TFT_eSPI::writecommand;

#ifdef TOUCH_CS
#if !defined(TFT_PARALLEL_8_BIT) && !defined(RP2040_PIO_INTERFACE) && !defined(TFT_PARALLEL_16_BIT)
    // Touchscreen Functions
    using TFT_eSPI::calibrateTouch;
    using TFT_eSPI::getTouch;
    using TFT_eSPI::getTouchRaw;
    using TFT_eSPI::setTouch;
#endif
#endif

#if !defined(TFT_PARALLEL_8_BIT) && !defined(TFT_PARALLEL_16_BIT)
    using TFT_eSPI::getSPIinstance;
#endif

    // Offscreen canvas (TFT_eSprite, same idea as M5Canvas). Draw into RAM, then
    // endFrame() blits once so fillScreen/redraw is not visible on the panel.
    // On no-PSRAM boards the pixel buffer is freed after present so BLE/WiFi
    // still have a contiguous internal-DMA block.
    bool beginFrame();
    void endFrame(bool present = true);
    void abortFrame();
    bool isFraming() const { return _buffering; }
    void releaseCanvas();
    void suppressCanvas(bool suppress);
    bool isCanvasSuppressed() const { return _canvasSuppressed; }

    void setRotation(uint8_t r);
    void drawPixel(int32_t x, int32_t y, uint32_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void fillRectHGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2);
    void fillRectVGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2);
    void fillScreen(uint32_t color);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
    void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
    void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
    void drawEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color);
    void fillEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color);
    void drawArc(
        int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle,
        uint32_t fg_color, uint32_t bg_color, bool smoothArc = true
    );
    void drawWideLine(
        float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF
    );
    void drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color);
    void drawXBitmap(
        int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg
    );
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transparent);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data, uint16_t transparent);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8 = true, uint16_t *cmap = nullptr);
    void
    pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8, uint16_t *cmap = nullptr);

    void setSwapBytes(bool swap);
    bool getSwapBytes();

    int16_t textWidth(const String &s);
    int16_t textWidth(const String &s, uint8_t font);
    int16_t textWidth(const char *s);
    int16_t textWidth(const char *s, uint8_t font);
    int16_t fontHeight();
    int16_t fontHeight(int16_t font);

    void setCursor(int16_t x, int16_t y);
    int16_t getCursorX();
    int16_t getCursorY();
    void setTextSize(uint8_t s);
    void setTextColor(uint16_t c);
    void setTextColor(uint16_t c, uint16_t b, bool bgfill = false);
    void setTextDatum(uint8_t d);
    uint8_t getTextDatum();
    void setTextFont(uint8_t f);
    void setTextWrap(bool wrapX, bool wrapY = false);
    int16_t drawString(const String &string, int32_t x, int32_t y, uint8_t font = 1);
    int16_t drawCentreString(const String &string, int32_t x, int32_t y, uint8_t font = 1);
    int16_t drawRightString(const String &string, int32_t x, int32_t y, uint8_t font = 1);

    size_t write(uint8_t c);
    size_t write(const uint8_t *buffer, size_t size);

    int16_t width();
    int16_t height();

    uint32_t getTextColor() const;
    uint32_t getTextBgColor() const;
    uint8_t getTextSize() const;
    uint8_t getRotation();
    TFT_eSPI *native();

private:
    bool ensureCanvas();
    void syncCanvasState();
    void markDirty(int32_t x, int32_t y, int32_t w, int32_t h);
    void markTextDirty(int32_t x, int32_t y, int32_t w, uint8_t font);
    void resetDirty();
    void presentDirty();
    bool canvasInInternalRam() const;

    TFT_eSprite *_fb = nullptr;
    bool _buffering = false;
    bool _canvasSuppressed = false;
    uint8_t _frameDepth = 0;
    int16_t _dx0 = 32767;
    int16_t _dy0 = 32767;
    int16_t _dx1 = -32768;
    int16_t _dy1 = -32768;
};

class tft_sprite : private TFT_eSprite {
public:
    explicit tft_sprite(tft_display *parent);
    ~tft_sprite() = default;

    using TFT_eSprite::drawCircle;
    using TFT_eSprite::drawLine;
    using TFT_eSprite::drawPixel;
    using TFT_eSprite::drawRect;
    using TFT_eSprite::drawRoundRect;
    using TFT_eSprite::drawString;
    using TFT_eSprite::drawXBitmap;
    using TFT_eSprite::fillCircle;
    using TFT_eSprite::fillRect;
    using TFT_eSprite::fillRectHGradient;
    using TFT_eSprite::fillRectVGradient;
    using TFT_eSprite::fillRoundRect;
    using TFT_eSprite::height;
    using TFT_eSprite::pushImage;
    using TFT_eSprite::setCursor;
    using TFT_eSprite::setTextColor;
    using TFT_eSprite::setTextDatum;
    using TFT_eSprite::setTextSize;
    using TFT_eSprite::width;

    void *createSprite(int16_t w, int16_t h, uint8_t frames = 1);
    void deleteSprite();
    void setColorDepth(uint8_t depth);

    void fillScreen(uint32_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color);
    void fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);

    void pushSprite(int32_t x, int32_t y, uint32_t transparent = TFT_TRANSPARENT);
    void pushToSprite(tft_sprite *dest, int32_t x, int32_t y, uint32_t transparent = TFT_TRANSPARENT);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8, uint16_t *cmap);
    void
    pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8, uint16_t *cmap);

    TFT_eSprite *nativeSprite();
};
#endif
#endif // LIB_HAL_TFTESPI_H

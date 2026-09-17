#include "tft.h"

#if defined(USE_TFT_ESPI)
// This library uses Mutex locally! don't need to rewrite the whole thing!!

tft_display::tft_display(int16_t _W, int16_t _H) : TFT_eSPI(_W, _H) {}

bool tft_display::ensureCanvas() {
    const int16_t w = TFT_eSPI::width();
    const int16_t h = TFT_eSPI::height();
    if (w <= 0 || h <= 0) return false;

    if (!_fb) _fb = new TFT_eSprite(this);
    if (!_fb) return false;

    if (_fb->created() && _fb->width() == w && _fb->height() == h) return true;

    _fb->deleteSprite();
    _fb->setColorDepth(16);
    return _fb->createSprite(w, h) != nullptr;
}

void tft_display::syncCanvasState() {
    if (!_fb) return;
    _fb->setSwapBytes(TFT_eSPI::getSwapBytes());
    _fb->setTextSize(TFT_eSPI::textsize);
    _fb->setTextColor(TFT_eSPI::textcolor, TFT_eSPI::textbgcolor);
    _fb->setTextDatum(TFT_eSPI::getTextDatum());
    _fb->setTextFont(TFT_eSPI::textfont);
    _fb->setCursor(TFT_eSPI::getCursorX(), TFT_eSPI::getCursorY());
}

bool tft_display::beginFrame() {
    if (_buffering) {
        if (_frameDepth < 255) _frameDepth++;
        return true;
    }
    if (!ensureCanvas()) return false;
    syncCanvasState();
    _buffering = true;
    _frameDepth = 1;
    return true;
}

void tft_display::endFrame(bool present) {
    if (!_buffering) return;
    if (_frameDepth > 0) _frameDepth--;
    if (_frameDepth > 0) return;
    _buffering = false;
    if (present && _fb && _fb->created()) _fb->pushSprite(0, 0);
}

void tft_display::setRotation(uint8_t r) {
    TFT_eSPI::setRotation(r);
    if (_fb) _fb->deleteSprite();
}

void tft_display::drawPixel(int32_t x, int32_t y, uint32_t color) {
    if (_buffering && _fb) _fb->drawPixel(x, y, color);
    else TFT_eSPI::drawPixel(x, y, color);
}

void tft_display::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (_buffering && _fb) _fb->drawLine(x0, y0, x1, y1, color);
    else TFT_eSPI::drawLine(x0, y0, x1, y1, color);
}

void tft_display::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    if (_buffering && _fb) _fb->drawFastHLine(x, y, w, color);
    else TFT_eSPI::drawFastHLine(x, y, w, color);
}

void tft_display::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    if (_buffering && _fb) _fb->drawFastVLine(x, y, h, color);
    else TFT_eSPI::drawFastVLine(x, y, h, color);
}

void tft_display::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (_buffering && _fb) _fb->drawRect(x, y, w, h, color);
    else TFT_eSPI::drawRect(x, y, w, h, color);
}

void tft_display::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (_buffering && _fb) _fb->fillRect(x, y, w, h, color);
    else TFT_eSPI::fillRect(x, y, w, h, color);
}

void tft_display::fillRectHGradient(
    int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2
) {
    if (_buffering && _fb) _fb->fillRectHGradient(x, y, w, h, color1, color2);
    else TFT_eSPI::fillRectHGradient(x, y, w, h, color1, color2);
}

void tft_display::fillRectVGradient(
    int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2
) {
    if (_buffering && _fb) _fb->fillRectVGradient(x, y, w, h, color1, color2);
    else TFT_eSPI::fillRectVGradient(x, y, w, h, color1, color2);
}

void tft_display::fillScreen(uint32_t color) {
    if (_buffering && _fb) _fb->fillSprite(color);
    else TFT_eSPI::fillScreen(color);
}

void tft_display::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    if (_buffering && _fb) _fb->drawRoundRect(x, y, w, h, r, color);
    else TFT_eSPI::drawRoundRect(x, y, w, h, r, color);
}

void tft_display::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    if (_buffering && _fb) _fb->fillRoundRect(x, y, w, h, r, color);
    else TFT_eSPI::fillRoundRect(x, y, w, h, r, color);
}

void tft_display::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    if (_buffering && _fb) _fb->drawCircle(x0, y0, r, color);
    else TFT_eSPI::drawCircle(x0, y0, r, color);
}

void tft_display::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    if (_buffering && _fb) _fb->fillCircle(x0, y0, r, color);
    else TFT_eSPI::fillCircle(x0, y0, r, color);
}

void tft_display::drawTriangle(
    int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color
) {
    if (_buffering && _fb) _fb->drawTriangle(x0, y0, x1, y1, x2, y2, color);
    else TFT_eSPI::drawTriangle(x0, y0, x1, y1, x2, y2, color);
}

void tft_display::fillTriangle(
    int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color
) {
    if (_buffering && _fb) _fb->fillTriangle(x0, y0, x1, y1, x2, y2, color);
    else TFT_eSPI::fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void tft_display::drawEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color) {
    if (_buffering && _fb) _fb->drawEllipse(x0, y0, rx, ry, color);
    else TFT_eSPI::drawEllipse(x0, y0, rx, ry, color);
}

void tft_display::fillEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color) {
    if (_buffering && _fb) _fb->fillEllipse(x0, y0, rx, ry, color);
    else TFT_eSPI::fillEllipse(x0, y0, rx, ry, color);
}

void tft_display::drawArc(
    int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color,
    uint32_t bg_color, bool smoothArc
) {
    if (_buffering && _fb) _fb->drawArc(x, y, r, ir, startAngle, endAngle, fg_color, bg_color, smoothArc);
    else TFT_eSPI::drawArc(x, y, r, ir, startAngle, endAngle, fg_color, bg_color, smoothArc);
}

void tft_display::drawWideLine(
    float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color
) {
    if (_buffering && _fb) _fb->drawWideLine(ax, ay, bx, by, wd, fg_color, bg_color);
    else TFT_eSPI::drawWideLine(ax, ay, bx, by, wd, fg_color, bg_color);
}

void tft_display::drawXBitmap(
    int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color
) {
    if (_buffering && _fb) _fb->drawXBitmap(x, y, bitmap, w, h, color);
    else TFT_eSPI::drawXBitmap(x, y, bitmap, w, h, color);
}

void tft_display::drawXBitmap(
    int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color, uint16_t bg
) {
    if (_buffering && _fb) _fb->drawXBitmap(x, y, bitmap, w, h, color, bg);
    else TFT_eSPI::drawXBitmap(x, y, bitmap, w, h, color, bg);
}

void tft_display::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    if (_buffering && _fb) _fb->pushImage(x, y, w, h, data);
    else TFT_eSPI::pushImage(x, y, w, h, data);
}

void tft_display::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    if (_buffering && _fb) _fb->pushImage(x, y, w, h, data);
    else TFT_eSPI::pushImage(x, y, w, h, data);
}

void tft_display::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transparent) {
    if (_buffering && _fb) _fb->pushImage(x, y, w, h, data);
    else TFT_eSPI::pushImage(x, y, w, h, data, transparent);
}

void tft_display::pushImage(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data, uint16_t transparent
) {
    if (_buffering && _fb) _fb->pushImage(x, y, w, h, data);
    else TFT_eSPI::pushImage(x, y, w, h, data, transparent);
}

void tft_display::pushImage(
    int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8, uint16_t *cmap
) {
    if (_buffering && _fb) {
        if (!data) return;
        if (bpp8 && cmap) {
            for (int32_t row = 0; row < h; ++row) {
                for (int32_t col = 0; col < w; ++col) {
                    _fb->drawPixel(x + col, y + row, cmap[data[row * w + col]]);
                }
            }
        }
        return;
    }
    TFT_eSPI::pushImage(x, y, w, h, data, bpp8, cmap);
}

void tft_display::pushImage(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8, uint16_t *cmap
) {
    pushImage(x, y, w, h, const_cast<uint8_t *>(data), bpp8, cmap);
}

void tft_display::setSwapBytes(bool swap) {
    TFT_eSPI::setSwapBytes(swap);
    if (_fb) _fb->setSwapBytes(swap);
}

bool tft_display::getSwapBytes() { return TFT_eSPI::getSwapBytes(); }

int16_t tft_display::textWidth(const String &s) {
    if (_buffering && _fb) return _fb->textWidth(s);
    return TFT_eSPI::textWidth(s);
}

int16_t tft_display::textWidth(const String &s, uint8_t font) {
    if (_buffering && _fb) return _fb->textWidth(s, font);
    return TFT_eSPI::textWidth(s, font);
}

int16_t tft_display::textWidth(const char *s) {
    if (_buffering && _fb) return _fb->textWidth(s);
    return TFT_eSPI::textWidth(s);
}

int16_t tft_display::textWidth(const char *s, uint8_t font) {
    if (_buffering && _fb) return _fb->textWidth(s, font);
    return TFT_eSPI::textWidth(s, font);
}

int16_t tft_display::fontHeight() {
    if (_buffering && _fb) return _fb->fontHeight();
    return TFT_eSPI::fontHeight();
}

int16_t tft_display::fontHeight(int16_t font) {
    if (_buffering && _fb) return _fb->fontHeight(font);
    return TFT_eSPI::fontHeight(font);
}

void tft_display::setCursor(int16_t x, int16_t y) {
    TFT_eSPI::setCursor(x, y);
    if (_fb) _fb->setCursor(x, y);
}

int16_t tft_display::getCursorX() {
    if (_buffering && _fb) return _fb->getCursorX();
    return TFT_eSPI::getCursorX();
}

int16_t tft_display::getCursorY() {
    if (_buffering && _fb) return _fb->getCursorY();
    return TFT_eSPI::getCursorY();
}

void tft_display::setTextSize(uint8_t s) {
    TFT_eSPI::setTextSize(s);
    if (_fb) _fb->setTextSize(s);
}

void tft_display::setTextColor(uint16_t c) {
    TFT_eSPI::setTextColor(c);
    if (_fb) _fb->setTextColor(c);
}

void tft_display::setTextColor(uint16_t c, uint16_t b, bool bgfill) {
    TFT_eSPI::setTextColor(c, b, bgfill);
    if (_fb) _fb->setTextColor(c, b, bgfill);
}

void tft_display::setTextDatum(uint8_t d) {
    TFT_eSPI::setTextDatum(d);
    if (_fb) _fb->setTextDatum(d);
}

uint8_t tft_display::getTextDatum() {
    if (_buffering && _fb) return _fb->getTextDatum();
    return TFT_eSPI::getTextDatum();
}

void tft_display::setTextFont(uint8_t f) {
    TFT_eSPI::setTextFont(f);
    if (_fb) _fb->setTextFont(f);
}

void tft_display::setTextWrap(bool wrapX, bool wrapY) {
    TFT_eSPI::setTextWrap(wrapX, wrapY);
    if (_fb) _fb->setTextWrap(wrapX, wrapY);
}

int16_t tft_display::drawString(const String &string, int32_t x, int32_t y, uint8_t font) {
    if (_buffering && _fb) return _fb->drawString(string, x, y, font);
    return TFT_eSPI::drawString(string, x, y, font);
}

int16_t tft_display::drawCentreString(const String &string, int32_t x, int32_t y, uint8_t font) {
    if (_buffering && _fb) return _fb->drawCentreString(string, x, y, font);
    return TFT_eSPI::drawCentreString(string, x, y, font);
}

int16_t tft_display::drawRightString(const String &string, int32_t x, int32_t y, uint8_t font) {
    if (_buffering && _fb) return _fb->drawRightString(string, x, y, font);
    return TFT_eSPI::drawRightString(string, x, y, font);
}

size_t tft_display::write(uint8_t c) {
    if (_buffering && _fb) return _fb->write(c);
    return TFT_eSPI::write(c);
}

size_t tft_display::write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) n += write(buffer[i]);
    return n;
}

int16_t tft_display::width() { return TFT_eSPI::width(); }

int16_t tft_display::height() { return TFT_eSPI::height(); }

uint32_t tft_display::getTextColor() const {
    return (_buffering && _fb) ? _fb->textcolor : TFT_eSPI::textcolor;
}

uint32_t tft_display::getTextBgColor() const {
    return (_buffering && _fb) ? _fb->textbgcolor : TFT_eSPI::textbgcolor;
}

uint8_t tft_display::getTextSize() const {
    return (_buffering && _fb) ? _fb->textsize : TFT_eSPI::textsize;
}

uint8_t tft_display::getRotation() { return TFT_eSPI::getRotation(); }

TFT_eSPI *tft_display::native() { return static_cast<TFT_eSPI *>(this); }

tft_sprite::tft_sprite(tft_display *parent) : TFT_eSprite(static_cast<TFT_eSPI *>(parent)) {}

void *tft_sprite::createSprite(int16_t w, int16_t h, uint8_t frames) {
    return TFT_eSprite::createSprite(w, h, frames);
}

void tft_sprite::deleteSprite() { TFT_eSprite::deleteSprite(); }

void tft_sprite::setColorDepth(uint8_t depth) { TFT_eSprite::setColorDepth(depth); }

void tft_sprite::fillScreen(uint32_t color) { TFT_eSprite::fillSprite(color); }

void tft_sprite::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    TFT_eSprite::fillRect(x, y, w, h, color);
}

void tft_sprite::fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    TFT_eSprite::fillCircle(x, y, r, color);
}

void tft_sprite::fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color) {
    TFT_eSprite::fillEllipse(x, y, rx, ry, color);
}

void tft_sprite::fillTriangle(
    int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color
) {
    TFT_eSprite::fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void tft_sprite::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    TFT_eSprite::drawFastVLine(x, y, h, color);
}

void tft_sprite::pushSprite(int32_t x, int32_t y, uint32_t transparent) {
    TFT_eSprite::pushSprite(x, y, transparent);
}

void tft_sprite::pushToSprite(tft_sprite *dest, int32_t x, int32_t y, uint32_t transparent) {
    TFT_eSprite::pushToSprite(static_cast<TFT_eSprite *>(dest), x, y, transparent);
}

void tft_sprite::pushImage(
    int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8, uint16_t *cmap
) {
    if (!data || !bpp8 || !cmap) return;
    for (int32_t row = 0; row < h; ++row) {
        for (int32_t col = 0; col < w; ++col) { drawPixel(x + col, y + row, cmap[data[row * w + col]]); }
    }
}

void tft_sprite::pushImage(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8, uint16_t *cmap
) {
    pushImage(x, y, w, h, const_cast<uint8_t *>(data), bpp8, cmap);
}

TFT_eSprite *tft_sprite::nativeSprite() { return static_cast<TFT_eSprite *>(this); }

#endif

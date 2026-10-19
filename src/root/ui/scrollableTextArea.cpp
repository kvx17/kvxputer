#include "root/ui/scrollableTextArea.h"
#define _scrollBuffer tft
ScrollableTextArea::ScrollableTextArea(const String &title)
    : firstVisibleLine{0}, _redraw{true}, _title(title), _fontSize(FP), _startX(BORDER_PAD_X),
      _startY(BORDER_PAD_Y), _width(tftWidth - 2 * BORDER_PAD_X),
      _height(tftHeight - 4 - BORDER_PAD_X - BORDER_PAD_Y) {
    drawMainBorder();

    if (!_title.isEmpty()) {
        printTitle(_title);
        _startY = tft.getCursorY();
        _height -= (_startY - BORDER_PAD_Y);
    }

    setup();
}

ScrollableTextArea::ScrollableTextArea(
    uint8_t fontSize, int16_t startX, int16_t startY, int32_t width, int32_t height, bool drawBorders,
    bool indentWrappedLines
)
    : firstVisibleLine{0}, _redraw{true}, _title(""), _fontSize(fontSize), _startX(startX), _startY(startY),
      _width(width), _height(height), _indentWrappedLines(indentWrappedLines) {
    if (drawBorders) { drawMainBorder(); }
    setup();
}

ScrollableTextArea::~ScrollableTextArea() {
    // We don't use Sprites for big things, unfortunetly theres no much RAM in all devices
}

uint16_t ScrollableTextArea::maxCharsForFont(uint8_t fontSize) const {
    int charW = LW * max(1, (int)fontSize);
    int measured = _scrollBuffer.textWidth("W", fontSize);
    if (measured > 0 && measured <= charW * 2) charW = measured;
    return max(8, (int)(_width / charW));
}

int32_t ScrollableTextArea::linePixels(size_t idx) const {
    uint8_t sz = (idx < lineFontSizes.size()) ? lineFontSizes[idx] : _fontSize;
    sz = max((uint8_t)1, sz);
    return (int32_t)sz * LH + 2;
}

bool ScrollableTextArea::viewShowsLastLine() const {
    if (linesBuffer.empty()) return true;
    int32_t y = 0;
    for (size_t idx = firstVisibleLine; idx < linesBuffer.size(); idx++) {
        y += linePixels(idx);
        if (y > _height) return false;
    }
    return true;
}

void ScrollableTextArea::setup() {
    _scrollBuffer.setTextColor(kvxConfig.priColor);
    _scrollBuffer.setTextSize(_fontSize);
    _scrollBuffer.fillRect(_startX, _startY, _width, _height, kvxConfig.bgColor);

    _maxCharactersPerLine = maxCharsForFont(_fontSize);
    _pixelsPerLine = max(LH * max(1, (int)_fontSize), (int)_scrollBuffer.fontHeight()) + 2;
    _maxVisibleLines = max(1, (int)(_height / _pixelsPerLine));
}

void ScrollableTextArea::scrollUp() {
    if (firstVisibleLine) {
        firstVisibleLine--;
        _redraw = true;
    }
}

void ScrollableTextArea::scrollDown() {
    if (linesBuffer.empty() || viewShowsLastLine()) return;
    if (firstVisibleLine + 1 < linesBuffer.size()) {
        firstVisibleLine++;
        _redraw = true;
    }
}

void ScrollableTextArea::scrollToLine(size_t lineNumber) {
    if (linesBuffer.empty()) return; // Ensure there's content to scroll

    if (lineNumber > linesBuffer.size() - _maxVisibleLines) {
        firstVisibleLine =
            (linesBuffer.size() > _maxVisibleLines) ? linesBuffer.size() - _maxVisibleLines : 0;
    } else {
        firstVisibleLine = lineNumber;
    }
}

String ScrollableTextArea::getLine(size_t lineNumber) {
    return linesBuffer[(lineNumber >= linesBuffer.size()) ? linesBuffer.size() : lineNumber];
}

size_t ScrollableTextArea::getMaxLines() { return linesBuffer.size(); }

bool ScrollableTextArea::show(bool force) {
    draw(force);

    // Only the first paint may be forced. Passing force into the idle loop
    // caused constant body flicker (fillRect + redraw every yield).
    while (check(SelPress)) {
        update(false);
        yield();
    }
    for (;;) {
        if (returnToMenu || forceHome) return false;
        if (check(SelPress)) return true;
        if (check(EscPress)) return false;
        update(false);
        yield();
    }
}

uint32_t ScrollableTextArea::getMaxVisibleTextLength() { return _maxVisibleLines * _maxCharactersPerLine; }

void ScrollableTextArea::update(bool force) {
#ifdef HAS_ENCODER
    int32_t rotarySteps = drainRotarySteps();
    if (rotarySteps != 0) {
        check(PrevPress);
        check(NextPress);
        check(UpPress);
        check(DownPress);
        while (rotarySteps > 0) {
            scrollUp();
            rotarySteps--;
        }
        while (rotarySteps < 0) {
            scrollDown();
            rotarySteps++;
        }
        vTaskDelay(4 / portTICK_PERIOD_MS);
        PrevPress = false;
        NextPress = false;
        UpPress = false;
        DownPress = false;
    }
#endif

    if (check(PrevPress) || check(UpPress)) scrollUp();
    else if (check(NextPress) || check(DownPress)) scrollDown();

    draw(force);
}

void ScrollableTextArea::updateSoft(bool force) {
#ifdef HAS_ENCODER
    int32_t rotarySteps = drainRotarySteps();
    if (rotarySteps != 0) {
        while (rotarySteps > 0) {
            scrollUp();
            rotarySteps--;
        }
        while (rotarySteps < 0) {
            scrollDown();
            rotarySteps++;
        }
        PrevPress = false;
        NextPress = false;
        UpPress = false;
        DownPress = false;
    }
#endif

    // Direct flag reads — avoid check()'s vTaskSuspend + delay(10), which
    // starves a same-core NFC emulate task and can drop the RF session.
    if (PrevPress || UpPress) {
        PrevPress = false;
        UpPress = false;
        scrollUp();
    } else if (NextPress || DownPress) {
        NextPress = false;
        DownPress = false;
        scrollDown();
    }

    draw(force);
}

void ScrollableTextArea::fromFile(File file) {
    clear();
    while (file.available()) addLine(file.readStringUntil('\n'));

    draw(true);
    delay(100);
    draw(true);
}

void ScrollableTextArea::clear() {
    firstVisibleLine = 0;
    linesBuffer.clear();
    lineColors.clear();
    lineFontSizes.clear();
}

void ScrollableTextArea::fromString(const String &text) {
    clear();
    int startIdx = 0;
    int endIdx = 0;

    while (endIdx < text.length()) {
        if (text[endIdx] == '\n') {
            addLine(text.substring(startIdx, endIdx));
            startIdx = endIdx + 1;
        }

        endIdx++;
    }

    // Add the last line if there's remaining text (text does not ends with \n)
    if (startIdx < text.length()) { addLine(text.substring(startIdx, endIdx)); }
}

// for devices it will act as a scrollable text area
void ScrollableTextArea::addLine(const String &text) { addLine(text, kvxConfig.priColor, _fontSize); }

void ScrollableTextArea::addLine(const String &text, uint16_t color) {
    addLine(text, color, _fontSize);
}

void ScrollableTextArea::addLine(const String &text, uint16_t color, uint8_t fontSize) {
    const uint8_t sz = fontSize ? fontSize : _fontSize;
    const uint16_t maxChars = maxCharsForFont(sz);

    if (text.isEmpty()) {
        linesBuffer.emplace_back("");
        lineColors.emplace_back(color);
        lineFontSizes.emplace_back(sz);
        _redraw = true;
        return;
    }

    String buff;
    size_t start = 0;
    bool firstLine = true;

    // Automatically split into multiple lines
    while (start < text.length()) {
        size_t len = maxChars;

        if (!firstLine && _indentWrappedLines) {
            buff = " " + text.substring(start, start + len - 1); // Reduce length for space
            start += len - 1;
        } else {
            buff = text.substring(start, start + len);
            start += len;
        }
        if (buff.endsWith("\r")) buff.remove(buff.length() - 1);

        linesBuffer.emplace_back(buff);
        lineColors.emplace_back(color);
        lineFontSizes.emplace_back(sz);
        firstLine = false;
    }

    _redraw = true;
}

void ScrollableTextArea::draw(bool force) {
    if (!_redraw && !force) return;

    _scrollBuffer.fillRect(_startX, _startY, _width, _height, kvxConfig.bgColor);
    const uint8_t prevSize = tft.getTextSize();

    int32_t yOffset = 0;
    size_t lines = 0;
    size_t idx{firstVisibleLine};
    while (idx < linesBuffer.size()) {
        const int32_t rowH = linePixels(idx);
        if (yOffset + rowH > _height) break;

        const uint16_t color =
            (idx < lineColors.size()) ? lineColors[idx] : kvxConfig.priColor;
        const uint8_t sz =
            (idx < lineFontSizes.size()) ? lineFontSizes[idx] : _fontSize;
        tft.setTextSize(sz);
        _scrollBuffer.setTextColor(color);
        _scrollBuffer.drawString(linesBuffer[idx], 0 + _startX, _startY + yOffset);
        yOffset += rowH;
        lines++;
        idx++;
    }

    lastVisibleLine = firstVisibleLine + lines;
    tft.setTextSize(prevSize);

    _redraw = false;
}

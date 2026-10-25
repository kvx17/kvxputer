#include "root/ui/scrollableTextArea.h"
#include "root/ui/kvx_ui.h"
#define _scrollBuffer tft
ScrollableTextArea::ScrollableTextArea(const String &title)
    : firstVisibleLine{0}, _redraw{true}, _title(title), _fontSize((uint8_t)uiBodyFont()),
      _startX(BORDER_PAD_X), _startY(KVX_TOPBAR_H + 4), _width(tftWidth - 2 * BORDER_PAD_X),
      _height(tftHeight - (KVX_TOPBAR_H + 4) - BORDER_PAD_X) {
    {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        if (!_title.isEmpty()) drawKvxTopBar(_title.c_str());
        else drawKvxTopBar(kvxTopBarAppName());
    }
    setup();
}

ScrollableTextArea::ScrollableTextArea(
    uint8_t fontSize, int16_t startX, int16_t startY, int32_t width, int32_t height, bool drawBorders,
    bool indentWrappedLines
)
    : firstVisibleLine{0}, _redraw{true}, _title(""),
      _fontSize(fontSize ? fontSize : (uint8_t)uiBodyFont()), _startX(startX), _startY(startY),
      _width(width), _height(height), _indentWrappedLines(indentWrappedLines) {
    if (drawBorders) {
        TftFrame frame;
        tft.fillScreen(kvxConfig.bgColor);
        drawKvxTopBar(kvxTopBarAppName());
    }
    setup();
}

ScrollableTextArea::~ScrollableTextArea() {
    // We don't use Sprites for big things, unfortunetly theres no much RAM in all devices
}

uint16_t ScrollableTextArea::maxCharsForFont(uint8_t fontSize) const {
    int charW = uiCharW(fontSize);
    // Measure with the active face. Do not pass UI_FONT_BODY as a TFT font index.
    const uint8_t prev = tft.getTextSize();
    _scrollBuffer.setTextSize(fontSize);
    int measured = _scrollBuffer.textWidth("W");
    _scrollBuffer.setTextSize(prev);
    if (measured > 0 && measured <= charW * 2) charW = measured;
    return max(8, (int)(_width / max(1, charW)));
}

int32_t ScrollableTextArea::linePixels(size_t idx) const {
    uint8_t sz = (idx < lineFontSizes.size()) ? lineFontSizes[idx] : _fontSize;
    if (!sz) sz = (uint8_t)uiBodyFont();
    return (int32_t)uiLineH(sz) + 2;
}

bool ScrollableTextArea::viewShowsLastLine() const {
    if (linesBuffer.empty()) return true;
    int32_t y = 0;
    for (size_t idx = firstVisibleLine; idx < linesBuffer.size(); idx++) {
        y += linePixels(idx);
        if (y > _height) return false;
    }
    // Require one blank pad row after the last text line before treating the
    // view as "at the end" — scroll until last row of text + 1.
    const uint8_t padSz = _fontSize ? _fontSize : (uint8_t)uiBodyFont();
    y += uiLineH(padSz) + 2;
    return y <= _height;
}

void ScrollableTextArea::setup() {
    _scrollBuffer.setTextColor(kvxConfig.priColor);
    _scrollBuffer.setTextSize(_fontSize);
    _scrollBuffer.fillRect(_startX, _startY, _width, _height, kvxConfig.bgColor);

    _maxCharactersPerLine = maxCharsForFont(_fontSize);
    _pixelsPerLine = max(uiLineH(_fontSize), (int)_scrollBuffer.fontHeight()) + 2;
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
    firstVisibleLine++;
    _redraw = true;
}

void ScrollableTextArea::scrollToLine(size_t lineNumber) {
    if (linesBuffer.empty()) return;

    // Clamp to the deepest scroll that still keeps last-text + 1 pad in view.
    size_t maxFirst = 0;
    firstVisibleLine = 0;
    if (!viewShowsLastLine()) {
        while (true) {
            size_t next = firstVisibleLine + 1;
            if (next >= linesBuffer.size()) break;
            firstVisibleLine = next;
            maxFirst = next;
            if (viewShowsLastLine()) break;
        }
    }
    firstVisibleLine = (lineNumber > maxFirst) ? maxFirst : lineNumber;
    _redraw = true;
}

String ScrollableTextArea::getLine(size_t lineNumber) {
    return linesBuffer[(lineNumber >= linesBuffer.size()) ? linesBuffer.size() : lineNumber];
}

size_t ScrollableTextArea::getMaxLines() { return linesBuffer.size(); }

bool ScrollableTextArea::show(bool force) {
    // Guarantee a reachable blank after the last text row (last + 1).
    if (!linesBuffer.empty() && !linesBuffer.back().isEmpty()) addLine("");

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
    lineRightText.clear();
    lineRightColors.clear();
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

    auto pushLine = [&](const String &s) {
        linesBuffer.emplace_back(s);
        lineColors.emplace_back(color);
        lineFontSizes.emplace_back(sz);
        lineRightText.emplace_back("");
        lineRightColors.emplace_back(0);
    };

    if (text.isEmpty()) {
        pushLine("");
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

        pushLine(buff);
        firstLine = false;
    }

    _redraw = true;
}

void ScrollableTextArea::addField(const String &label, const String &value) {
    const uint8_t sz = _fontSize ? _fontSize : (uint8_t)uiBodyFont();
    const uint16_t labelColor = kvxConfig.priColor;
    const uint16_t valueColor = kvxConfig.secColor;

    const uint8_t prev = tft.getTextSize();
    _scrollBuffer.setTextSize(sz);
    const int gap = uiCharW(sz);
    const int labelPx = _scrollBuffer.textWidth(label);
    int avail = _width - labelPx - gap;
    if (avail < uiCharW(sz) * 2) avail = uiCharW(sz) * 2;

    // Fit as many value characters as possible on the first row.
    String remain = value;
    String firstVal;
    while (remain.length()) {
        // Grow firstVal while it still fits in avail.
        String trial = firstVal + remain.substring(0, 1);
        if (_scrollBuffer.textWidth(trial) > avail && firstVal.length()) break;
        if (_scrollBuffer.textWidth(trial) > avail && !firstVal.length()) {
            // Single oversized char — take it anyway and clip visually.
            firstVal = remain.substring(0, 1);
            remain.remove(0, 1);
            break;
        }
        firstVal = trial;
        remain.remove(0, 1);
        if (_scrollBuffer.textWidth(firstVal) >= avail) break;
    }
    _scrollBuffer.setTextSize(prev);

    linesBuffer.emplace_back(label);
    lineColors.emplace_back(labelColor);
    lineFontSizes.emplace_back(sz);
    lineRightText.emplace_back(firstVal);
    lineRightColors.emplace_back(valueColor);

    // Overflow continuation lines in accent color.
    const uint16_t maxChars = maxCharsForFont(sz);
    while (remain.length()) {
        String chunk = remain.substring(0, maxChars);
        remain.remove(0, chunk.length());
        linesBuffer.emplace_back(chunk);
        lineColors.emplace_back(valueColor);
        lineFontSizes.emplace_back(sz);
        lineRightText.emplace_back("");
        lineRightColors.emplace_back(0);
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
        const int y = _startY + yOffset;
        _scrollBuffer.setTextColor(color);
        _scrollBuffer.drawString(linesBuffer[idx], _startX, y);

        if (idx < lineRightText.size() && lineRightText[idx].length()) {
            const uint16_t rc =
                (idx < lineRightColors.size()) ? lineRightColors[idx] : kvxConfig.secColor;
            const int lx = _startX + _scrollBuffer.textWidth(linesBuffer[idx]) + uiCharW(sz);
            _scrollBuffer.setTextColor(rc);
            _scrollBuffer.drawString(lineRightText[idx], lx, y);
        }

        yOffset += rowH;
        lines++;
        idx++;
    }

    lastVisibleLine = firstVisibleLine + lines;
    tft.setTextSize(prevSize);

    _redraw = false;
}

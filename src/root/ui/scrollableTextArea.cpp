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
    // Average-ish advance for estimates only. Wrapping uses pixel width (see addLine).
    return max(8, (int)(_width / max(1, uiCharW(fontSize))));
}

/** Largest end index in [start, text.length()] whose substring fits in maxPx. */
static size_t fitEndByPixels(const String &text, size_t start, int32_t maxPx) {
    if (start >= text.length() || maxPx <= 0) return start;
    size_t lo = 1;
    size_t hi = text.length() - start;
    size_t best = 1;
    while (lo <= hi) {
        size_t mid = (lo + hi) / 2;
        if (_scrollBuffer.textWidth(text.substring(start, start + mid)) <= maxPx) {
            best = mid;
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }
    size_t end = start + best;
    // Prefer breaking at a space when the line is truncated mid-word.
    if (end < text.length() && best > 1) {
        int sp = -1;
        for (size_t i = end; i > start; i--) {
            if (text[i - 1] == ' ') {
                sp = (int)i;
                break;
            }
        }
        // Only use the space if it still leaves a useful chunk.
        if (sp > (int)start + 1) end = (size_t)sp;
    }
    return end;
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

    const uint8_t prev = tft.getTextSize();
    _scrollBuffer.setTextSize(sz);

    size_t start = 0;
    bool firstLine = true;
    while (start < text.length()) {
        int32_t avail = _width;
        size_t contentStart = start;
        String prefix;
        if (!firstLine && _indentWrappedLines) {
            prefix = " ";
            avail -= _scrollBuffer.textWidth(prefix);
            if (avail < uiCharW(sz)) avail = uiCharW(sz);
        }

        size_t end = fitEndByPixels(text, contentStart, avail);
        if (end <= contentStart) end = contentStart + 1; // always advance
        String buff = prefix + text.substring(contentStart, end);
        if (buff.endsWith("\r")) buff.remove(buff.length() - 1);
        pushLine(buff);
        start = end;
        firstLine = false;
    }

    _scrollBuffer.setTextSize(prev);
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

    // Overflow continuation lines in accent color (pixel-fit wrap).
    if (remain.length()) addLine(remain, valueColor, sz);
    else _redraw = true;
}

void ScrollableTextArea::draw(bool force) {
    if (!_redraw && !force) return;

    // Build the visible set first so an unchanged scroll can patch lines only.
    struct VisLine {
        size_t idx;
        int y;
        int rowH;
        uint8_t sz;
        uint16_t color;
        uint16_t rc;
        const char *left;
        const char *right;
    };
    VisLine vis[32];
    size_t lines = 0;
    int32_t yOffset = 0;
    size_t idx = firstVisibleLine;
    while (idx < linesBuffer.size() && lines < 32) {
        const int32_t rowH = linePixels(idx);
        if (yOffset + rowH > _height) break;
        VisLine &v = vis[lines];
        v.idx = idx;
        v.y = _startY + yOffset;
        v.rowH = rowH;
        v.sz = (idx < lineFontSizes.size()) ? lineFontSizes[idx] : _fontSize;
        v.color = (idx < lineColors.size()) ? lineColors[idx] : kvxConfig.priColor;
        v.rc = (idx < lineRightColors.size()) ? lineRightColors[idx] : kvxConfig.secColor;
        v.left = linesBuffer[idx].c_str();
        v.right = (idx < lineRightText.size()) ? lineRightText[idx].c_str() : "";
        yOffset += rowH;
        lines++;
        idx++;
    }

    const bool scrollChanged =
        force || firstVisibleLine != _lastFirstVisible || lines != _lastLineCount ||
        _lastLeft.size() != lines;
    const uint8_t prevSize = tft.getTextSize();
    const uint16_t bg = kvxConfig.bgColor;

    if (scrollChanged) {
        _scrollBuffer.fillRect(_startX, _startY, _width, _height, bg);
        _lastLeft.clear();
        _lastRight.clear();
        _lastLeft.reserve(lines);
        _lastRight.reserve(lines);
    }

    for (size_t i = 0; i < lines; i++) {
        const VisLine &v = vis[i];
        const bool leftChanged =
            scrollChanged || i >= _lastLeft.size() || _lastLeft[i] != v.left;
        const bool rightChanged =
            scrollChanged || i >= _lastRight.size() || _lastRight[i] != v.right;
        if (!leftChanged && !rightChanged) continue;

        tft.setTextSize(v.sz);
        // Opaque overwrite of the line strip — no full viewport clear on a text change.
        if (!scrollChanged) _scrollBuffer.fillRect(_startX, v.y, _width, v.rowH, bg);
        _scrollBuffer.setTextColor(v.color, bg, true);
        _scrollBuffer.drawString(v.left, _startX, v.y);
        if (v.right && v.right[0]) {
            const int lx = _startX + _scrollBuffer.textWidth(v.left) + uiCharW(v.sz);
            _scrollBuffer.setTextColor(v.rc, bg, true);
            _scrollBuffer.drawString(v.right, lx, v.y);
        }

        if (i < _lastLeft.size()) {
            _lastLeft[i] = v.left;
            _lastRight[i] = v.right ? v.right : "";
        } else {
            _lastLeft.push_back(v.left);
            _lastRight.push_back(v.right ? v.right : "");
        }
    }

    // Drop stale cache entries when the visible count shrank.
    if (_lastLeft.size() > lines) {
        _lastLeft.resize(lines);
        _lastRight.resize(lines);
    }

    _lastFirstVisible = firstVisibleLine;
    _lastLineCount = lines;
    lastVisibleLine = firstVisibleLine + lines;
    tft.setTextSize(prevSize);
    _redraw = false;
}

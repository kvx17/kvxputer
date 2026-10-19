#pragma once
#include "root/ui/display.h"

class ScrollableTextArea {
public:
    ScrollableTextArea(const String &title = "");

    ScrollableTextArea(
        uint8_t fontSize, int16_t startX, int16_t startY, int32_t width, int32_t height,
        bool drawBorders = true, bool indentWrappedLines = false
    );

    ~ScrollableTextArea();

    void scrollUp();

    void scrollDown();

    void scrollToLine(size_t lineNumber);

    String getLine(size_t lineNumber);
    size_t getMaxLines();

    void addLine(const String &text);
    void addLine(const String &text, uint16_t color);
    void addLine(const String &text, uint16_t color, uint8_t fontSize);

    void clear();

    void fromString(const String &text);

    void fromFile(File file);

    void draw(bool force = false);

    /** Handle scroll keys / encoder and redraw when needed. */
    void update(bool force = false);

    /**
     * Scroll via Prev/Next/Up/Down flags without check()'s debounce delay.
     * Use while a background RF task must keep running (e.g. NFC emulate).
     */
    void updateSoft(bool force = false);

    /** Scroll until OK (returns true) or Esc/home (returns false). */
    bool show(bool force = false);

    uint32_t getMaxVisibleTextLength();

    size_t firstVisibleLine;
    size_t lastVisibleLine;
    /// TODO: Change to std::vector<char *> and alloc to PSRAM if possible
    std::vector<String> linesBuffer;
    std::vector<uint16_t> lineColors;
    std::vector<uint8_t> lineFontSizes;

private:
    bool _redraw;
    String _title;
    uint8_t _fontSize;
    int16_t _startX, _startY;
    int32_t _width, _height;
    int32_t _pixelsPerLine;
    size_t _maxVisibleLines;
    uint16_t _maxCharactersPerLine;
    bool _indentWrappedLines;

    void setup();
    int32_t linePixels(size_t idx) const;
    uint16_t maxCharsForFont(uint8_t fontSize) const;
    bool viewShowsLastLine() const;
};

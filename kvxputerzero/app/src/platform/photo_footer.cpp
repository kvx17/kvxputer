#include "photo_footer.h"

#include <algorithm>
#include <ctime>
#include <string>

namespace platform {
namespace {

// 5x7 glyphs, bit 4 is the leftmost pixel. Drawn for this footer, not copied from FXCam.
struct Glyph {
    char ch;
    uint8_t row[7];
};

const Glyph* glyph_for(char ch) {
    static const Glyph kGlyphs[] = {
        {' ', {0, 0, 0, 0, 0, 0, 0}},
        {'-', {0, 0, 0, 0x1F, 0, 0, 0}},
        {'.', {0, 0, 0, 0, 0, 0x0C, 0x0C}},
        {'/', {0x01, 0x02, 0x04, 0x08, 0x10, 0, 0}},
        {':', {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
        {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
        {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
        {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
        {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'a', {0, 0, 0x0E, 0x01, 0x0F, 0x11, 0x0F}},
        {'b', {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}},
        {'c', {0, 0, 0x0E, 0x10, 0x10, 0x11, 0x0E}},
        {'d', {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}},
        {'e', {0, 0, 0x0E, 0x11, 0x1F, 0x10, 0x0E}},
        {'f', {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08}},
        {'g', {0, 0, 0x0F, 0x11, 0x0F, 0x01, 0x0E}},
        {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
        {'i', {0x04, 0, 0x0C, 0x04, 0x04, 0x04, 0x0E}},
        {'j', {0x02, 0, 0x06, 0x02, 0x02, 0x12, 0x0C}},
        {'k', {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
        {'l', {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'m', {0, 0, 0x1A, 0x15, 0x15, 0x15, 0x15}},
        {'n', {0, 0, 0x16, 0x19, 0x11, 0x11, 0x11}},
        {'o', {0, 0, 0x0E, 0x11, 0x11, 0x11, 0x0E}},
        {'p', {0, 0, 0x1E, 0x11, 0x1E, 0x10, 0x10}},
        {'q', {0, 0, 0x0F, 0x11, 0x0F, 0x01, 0x01}},
        {'r', {0, 0, 0x16, 0x19, 0x10, 0x10, 0x10}},
        {'s', {0, 0, 0x0F, 0x10, 0x0E, 0x01, 0x1E}},
        {'t', {0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06}},
        {'u', {0, 0, 0x11, 0x11, 0x11, 0x13, 0x0D}},
        {'v', {0, 0, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'w', {0, 0, 0x11, 0x11, 0x15, 0x15, 0x0A}},
        {'x', {0, 0, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
        {'y', {0, 0, 0x11, 0x11, 0x0F, 0x01, 0x0E}},
        {'z', {0, 0, 0x1F, 0x02, 0x04, 0x08, 0x1F}},
    };
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.ch == ch) return &glyph;
    }
    return nullptr;
}

void plot(std::vector<uint8_t>& rgb, int width, int height, int x, int y, uint8_t r, uint8_t g,
          uint8_t b) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    uint8_t* p = rgb.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

void draw_text(std::vector<uint8_t>& rgb, int width, int height, int x, int y, int scale,
               const std::string& text, uint8_t red, uint8_t green, uint8_t blue) {
    int cursor = x;
    for (unsigned char character : text) {
        const Glyph* glyph = glyph_for(static_cast<char>(character));
        if (!glyph) {
            cursor += 6 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph->row[row] & (1u << (4 - column))) == 0) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        plot(rgb, width, height, cursor + column * scale + dx, y + row * scale + dy, red,
                             green, blue);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

} // namespace

void apply_photo_footer(std::vector<uint8_t>& rgb, int width, int height, const char* filter_name) {
    if (width <= 0 || height <= 0 ||
        rgb.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
        return;
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);

    const std::string lines[3] = {
        "Cardputer Zero",
        stamp,
        std::string("kvxputer  F/2.0  ") + (filter_name ? filter_name : ""),
    };
    const int scale = std::max(1, std::min(4, (std::min(width, height) + 240) / 480));
    constexpr int kMargin = 30;
    const int advance = 9 * scale;
    size_t longest = 0;
    for (const auto& line : lines) longest = std::max(longest, line.size());
    const int text_h = advance * 2 + 7 * scale;
    const int x = std::min(kMargin, std::max(0, width - 8));
    const int y = std::max(0, height - kMargin - text_h);
    for (int i = 0; i < 3; ++i) {
        const int ly = y + i * advance;
        draw_text(rgb, width, height, x + scale, ly + scale, scale, lines[i], 0, 0, 0);
        draw_text(rgb, width, height, x, ly, scale, lines[i], 255, 255, 255);
    }
}

void plot_i420(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane, int width, int height, int stride, int x,
               int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= width || y >= height || stride < width) return;
    y_plane[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x)] =
        (r > 200 && g > 200 && b > 200) ? 255 : 0;
    const int uv_stride = stride / 2;
    u_plane[static_cast<size_t>(y / 2) * static_cast<size_t>(uv_stride) + static_cast<size_t>(x / 2)] = 128;
    v_plane[static_cast<size_t>(y / 2) * static_cast<size_t>(uv_stride) + static_cast<size_t>(x / 2)] = 128;
}

void draw_text_i420(uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane, int width, int height, int stride, int x,
                    int y, int scale, const std::string& text, uint8_t red, uint8_t green, uint8_t blue) {
    int cursor = x;
    for (unsigned char character : text) {
        const Glyph* glyph = glyph_for(static_cast<char>(character));
        if (!glyph) {
            cursor += 6 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph->row[row] & (1u << (4 - column))) == 0) continue;
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        plot_i420(y_plane, u_plane, v_plane, width, height, stride,
                                  cursor + column * scale + dx, y + row * scale + dy, red, green, blue);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

void apply_photo_footer_i420(std::vector<uint8_t>& i420, int width, int height, int stride,
                             const char* filter_name) {
    if (width <= 0 || height <= 0 || stride < width || (stride & 1) || (height & 1)) return;
    const size_t y_size = static_cast<size_t>(stride) * static_cast<size_t>(height);
    const size_t uv_size = static_cast<size_t>(stride / 2) * static_cast<size_t>(height / 2);
    if (i420.size() < y_size + uv_size * 2) return;

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    const std::string lines[3] = {
        "Cardputer Zero",
        stamp,
        std::string("kvxputer  F/2.0  ") + (filter_name ? filter_name : ""),
    };
    const int scale = std::max(1, std::min(4, (std::min(width, height) + 240) / 480));
    constexpr int kMargin = 30;
    const int advance = 9 * scale;
    const int text_h = advance * 2 + 7 * scale;
    const int x = std::min(kMargin, std::max(0, width - 8));
    const int y = std::max(0, height - kMargin - text_h);
    uint8_t* y_plane = i420.data();
    uint8_t* u_plane = y_plane + y_size;
    uint8_t* v_plane = u_plane + uv_size;
    for (int i = 0; i < 3; ++i) {
        const int ly = y + i * advance;
        draw_text_i420(y_plane, u_plane, v_plane, width, height, stride, x + scale, ly + scale, scale, lines[i], 0,
                       0, 0);
        draw_text_i420(y_plane, u_plane, v_plane, width, height, stride, x, ly, scale, lines[i], 255, 255, 255);
    }
}

} // namespace platform

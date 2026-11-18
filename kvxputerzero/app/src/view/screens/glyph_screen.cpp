#include "glyph_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "theme.h"

#include <algorithm>
#include <cstring>

namespace screen {
namespace {

constexpr int kDrawW = 320;
// Content minus screen header (18) and caption (~14).
constexpr int kDrawH = view::kContentHeight - 18 - 14;

} // namespace

GlyphScreen::GlyphScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

GlyphScreen::~GlyphScreen() {
    // pixels_ owns the buffer; dsc_ points into it.
}

void GlyphScreen::ensure_buffer(int w, int h) {
    buf_w_ = w;
    buf_h_ = h;
    pixels_.assign(static_cast<size_t>(w * h * 4), 0);
    std::memset(&dsc_, 0, sizeof(dsc_));
    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc_.header.w = static_cast<uint32_t>(w);
    dsc_.header.h = static_cast<uint32_t>(h);
    dsc_.header.stride = static_cast<uint32_t>(w * 4);
    dsc_.data_size = static_cast<uint32_t>(pixels_.size());
    dsc_.data = pixels_.data();
}

void GlyphScreen::paint() {
    ensure_buffer(kDrawW, kDrawH);
    const uint32_t dark = 0xFF000000u;
    const uint32_t light = 0xFFFFFFFFu;
    const bool invert = view_model().glyph_invert();
    const uint32_t on = invert ? light : dark;
    const uint32_t off = invert ? dark : light;

    // Clear
    for (int y = 0; y < buf_h_; ++y) {
        for (int x = 0; x < buf_w_; ++x) {
            const lv_color32_t c = lv_color32_make((off >> 16) & 0xff, (off >> 8) & 0xff, off & 0xff, 0xff);
            uint8_t* p = pixels_.data() + (static_cast<size_t>(y * buf_w_ + x) * 4);
            std::memcpy(p, &c, 4);
        }
    }

    if (view_model().glyph_kind() == model::GlyphKind::Qr) {
        const int size = view_model().qr_size();
        const auto& mods = view_model().qr_modules();
        if (size > 0 && static_cast<int>(mods.size()) >= size * size) {
            const int margin = 2;
            const int usable = std::min(buf_w_, buf_h_) - margin * 2;
            const int scale = std::max(1, usable / size);
            const int drawn = scale * size;
            const int ox = (buf_w_ - drawn) / 2;
            const int oy = (buf_h_ - drawn) / 2;
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const bool bit = mods[static_cast<size_t>(y * size + x)] != 0;
                    const uint32_t col = bit ? on : off;
                    const lv_color32_t c =
                        lv_color32_make((col >> 16) & 0xff, (col >> 8) & 0xff, col & 0xff, 0xff);
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            const int px = ox + x * scale + dx;
                            const int py = oy + y * scale + dy;
                            if (px < 0 || py < 0 || px >= buf_w_ || py >= buf_h_) continue;
                            uint8_t* p = pixels_.data() + (static_cast<size_t>(py * buf_w_ + px) * 4);
                            std::memcpy(p, &c, 4);
                        }
                    }
                }
            }
        }
        if (caption_) lv_label_set_text(caption_, "Space invert · Esc back");
    } else if (view_model().glyph_kind() == model::GlyphKind::Barcode) {
        const std::string& bits = view_model().glyph_bits();
        if (!bits.empty()) {
            const int n = static_cast<int>(bits.size());
            const int scale = std::max(1, buf_w_ / n);
            const int drawn = scale * n;
            const int ox = (buf_w_ - drawn) / 2;
            const int bar_top = 10;
            const int bar_h = buf_h_ - 28;
            for (int i = 0; i < n; ++i) {
                const bool bar = bits[static_cast<size_t>(i)] == '1';
                const uint32_t col = bar ? on : off;
                const lv_color32_t c =
                    lv_color32_make((col >> 16) & 0xff, (col >> 8) & 0xff, col & 0xff, 0xff);
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = ox + i * scale + dx;
                    for (int py = bar_top; py < bar_top + bar_h; ++py) {
                        if (px < 0 || px >= buf_w_ || py < 0 || py >= buf_h_) continue;
                        uint8_t* p = pixels_.data() + (static_cast<size_t>(py * buf_w_ + px) * 4);
                        std::memcpy(p, &c, 4);
                    }
                }
            }
        }
        if (caption_) {
            const std::string& label = view_model().glyph_label();
            lv_label_set_text(caption_, label.empty() ? "Esc back" : label.c_str());
        }
    }

    if (image_) {
        lv_image_set_src(image_, &dsc_);
        lv_obj_invalidate(image_);
    }
}

void GlyphScreen::build() {
    make_header(content(), view_model().glyph_kind() == model::GlyphKind::Qr ? "QR" : "Barcode");

    image_ = lv_image_create(content());
    lv_obj_set_size(image_, kDrawW, kDrawH);
    lv_obj_align(image_, LV_ALIGN_TOP_MID, 0, 18);

    caption_ = lv_label_create(content());
    auto* font = assets().load_standard_font(12);
    lv_obj_set_style_text_font(caption_, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(caption_, tokens().ink_dim, 0);
    lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, -2);

    paint();
}

bool GlyphScreen::handle_key(uint32_t key) {
    if (key == ' ' && view_model().glyph_kind() == model::GlyphKind::Qr) {
        view_model().set_glyph_invert(!view_model().glyph_invert());
        paint();
        return true;
    }
    return KvxScreen::handle_key(key);
}

} // namespace screen

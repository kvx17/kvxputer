#pragma once

#include "kvx_screen.h"

#include <cstdint>
#include <vector>

namespace screen {

// Draws a QR matrix or barcode bit string full-screen (320x170).
class GlyphScreen final : public KvxScreen {
public:
    GlyphScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~GlyphScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;

private:
    void paint();
    void ensure_buffer(int w, int h);

    lv_obj_t* image_{nullptr};
    lv_obj_t* caption_{nullptr};
    lv_image_dsc_t dsc_{};
    std::vector<uint8_t> pixels_;
    int buf_w_ = 0;
    int buf_h_ = 0;
};

} // namespace screen

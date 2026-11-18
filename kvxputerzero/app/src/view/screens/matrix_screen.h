#pragma once

#include "kvx_screen.h"
#include "matrix_rain.h"

namespace screen {

class MatrixScreen final : public KvxScreen {
public:
    MatrixScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~MatrixScreen() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;

private:
    static void on_tick(lv_timer_t* timer);
    void finish();

    view::MatrixRain rain_;
    lv_timer_t* tick_ = nullptr;
    bool finished_ = false;
};

} // namespace screen

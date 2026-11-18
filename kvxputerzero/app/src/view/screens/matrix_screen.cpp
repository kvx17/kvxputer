#include "matrix_screen.h"

namespace screen {

MatrixScreen::MatrixScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
    rain_.mount(root());
    tick_ = lv_timer_create(on_tick, 20, this);
}

MatrixScreen::~MatrixScreen() {
    if (tick_) {
        lv_timer_delete(tick_);
        tick_ = nullptr;
    }
}

void MatrixScreen::build() {}

void MatrixScreen::on_tick(lv_timer_t* timer) {
    auto* self = static_cast<MatrixScreen*>(lv_timer_get_user_data(timer));
    if (!self || self->finished_) {
        return;
    }
    self->rain_.step();
    if (self->rain_.finished()) {
        self->finish();
    }
}

void MatrixScreen::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    view_model().go_back();
}

bool MatrixScreen::handle_key(uint32_t key) {
    (void)key;
    finish();
    return true;
}

} // namespace screen

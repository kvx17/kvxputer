#pragma once

#include "kvx_screen.h"

#include <functional>
#include <string>

namespace screen {

class PromptScreen final : public KvxScreen {
public:
    using ConfirmFn = std::function<void(const std::string& value)>;

    PromptScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
                 std::string hint, std::string initial, ConfirmFn on_confirm);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;

private:
    void confirm();

    std::string title_;
    std::string hint_;
    std::string initial_;
    ConfirmFn on_confirm_;
    lv_obj_t* input_ta_{nullptr};
};

} // namespace screen

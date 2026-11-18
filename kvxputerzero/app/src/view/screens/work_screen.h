#pragma once

#include "kvx_screen.h"

#include <functional>
#include <string>

namespace screen {

class WorkScreen final : public KvxScreen {
public:
    using RunFn = std::function<std::string(const std::string& input, bool& ok)>;

    WorkScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               std::string title,
               std::string hint,
               RunFn run);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    const char* typing_hint() const override;

private:
    void run_now();

    std::string title_;
    std::string hint_;
    RunFn run_;
    lv_obj_t* input_ta_{nullptr};
    lv_obj_t* output_label_{nullptr};
};

} // namespace screen

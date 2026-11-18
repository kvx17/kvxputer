#pragma once

#include "kvx_screen.h"

#include <string>
#include <vector>

namespace screen {

// Read-only document: one wrapped body on a surface panel. No selectable rows.
// Markup (line-oriented):
//   # Heading
//   - bullet
//   `code line`
//   blank line = paragraph break
class DocScreen final : public KvxScreen {
public:
    DocScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
              std::string body);
    DocScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, std::string title,
              std::vector<std::string> lines);

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;

private:
    void scroll_by(int px);
    void update_scroll_hint();
    static std::string render_markup(const std::string& raw);

    std::string title_;
    std::string body_;
    lv_obj_t* panel_{nullptr};
    lv_obj_t* body_label_{nullptr};
    lv_obj_t* scroll_hint_{nullptr};
};

} // namespace screen

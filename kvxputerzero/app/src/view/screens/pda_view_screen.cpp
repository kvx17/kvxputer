#include "pda_view_screen.h"

#include "asset_manager.h"
#include "kvx_style.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

PdaViewScreen::PdaViewScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

void PdaViewScreen::build() {
    const char* title = "View";
    switch (view_model().model().pda_kind()) {
    case model::PdaKind::Notes:
        title = "Note";
        break;
    case model::PdaKind::Memos:
        title = "Memo";
        break;
    case model::PdaKind::Contacts:
        title = "Contact";
        break;
    case model::PdaKind::Alarms:
        title = "Alarm";
        break;
    case model::PdaKind::Todo:
        title = "Task";
        break;
    }
    make_header(content(), title);

    const auto tok = tokens();
    body_ = lv_label_create(content());
    lv_obj_set_size(body_, view::kScreenWidth - 12, view::kContentHeight - 22);
    lv_obj_align(body_, LV_ALIGN_TOP_LEFT, 6, 20);
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(body_, tok.ink, 0);
    auto* font = assets().load_standard_font(14);
    lv_obj_set_style_text_font(body_, font ? font : &lv_font_montserrat_14, 0);
    const std::string& text = view_model().work_output();
    lv_label_set_text(body_, text.empty() ? "(empty)" : text.c_str());
    if (text.empty()) {
        lv_obj_set_style_text_color(body_, tok.ink_dim, 0);
    }
}

void PdaViewScreen::open_edit() {
    auto& model = view_model().model();
    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        model.set_work_input(model.work_output());
        view_model().open_pda_edit(model::PdaEditMode::Edit, true);
        break;
    case model::PdaKind::Memos:
        model.set_work_input(model.work_output());
        view_model().open_pda_edit(model::PdaEditMode::Edit, false);
        break;
    case model::PdaKind::Alarms:
        model.set_work_input(""); // will load label in edit from item — set from alarm
        {
            std::vector<tools::pda::Alarm> alarms;
            std::string error;
            tools::pda::load_alarms(alarms, error);
            const int idx = model.pda_item_index();
            if (idx >= 0 && idx < static_cast<int>(alarms.size())) {
                model.set_work_input(alarms[static_cast<size_t>(idx)].label);
            }
        }
        view_model().open_pda_edit(model::PdaEditMode::Edit, false);
        break;
    case model::PdaKind::Contacts: {
        model.set_pda_path("__edit_contact__");
        model.set_pda_edit_field(0);
        std::vector<tools::pda::Contact> contacts;
        std::string error;
        tools::pda::load_contacts(contacts, error);
        const int idx = model.pda_item_index();
        std::string name, phone, addr;
        if (idx >= 0 && idx < static_cast<int>(contacts.size())) {
            name = contacts[static_cast<size_t>(idx)].name;
            phone = contacts[static_cast<size_t>(idx)].phone;
            addr = contacts[static_cast<size_t>(idx)].address;
        }
        model.set_work_output(std::string("__contact__") + name + "|" + phone + "|" + addr);
        model.set_work_input(name);
        view_model().open_pda_edit(model::PdaEditMode::PromptField, false);
        break;
    }
    default:
        break;
    }
}

void PdaViewScreen::do_delete() {
    auto& model = view_model().model();
    std::string error;
    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        tools::pda::delete_note(model.pda_path(), error);
        break;
    case model::PdaKind::Memos: {
        std::vector<std::string> lines;
        tools::pda::load_memos(lines, error);
        const int idx = model.pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(lines.size())) {
            lines.erase(lines.begin() + idx);
            tools::pda::save_memos(lines, error);
        }
        break;
    }
    case model::PdaKind::Alarms: {
        std::vector<tools::pda::Alarm> alarms;
        tools::pda::load_alarms(alarms, error);
        const int idx = model.pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(alarms.size())) {
            alarms.erase(alarms.begin() + idx);
            tools::pda::save_alarms(alarms, error);
        }
        break;
    }
    case model::PdaKind::Contacts: {
        std::vector<tools::pda::Contact> contacts;
        tools::pda::load_contacts(contacts, error);
        const int idx = model.pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(contacts.size())) {
            contacts.erase(contacts.begin() + idx);
            tools::pda::save_contacts(contacts, error);
        }
        break;
    }
    default:
        break;
    }
    view_model().open_pda_list(model.pda_kind());
}

bool PdaViewScreen::handle_key(uint32_t key) {
    if (key == 'e' || key == 'E') {
        open_edit();
        return true;
    }
    if (key == 'x' || key == 'X' || key == LV_KEY_DEL) {
        do_delete();
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ') {
        open_edit();
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaViewScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {},
        {view::ICON_CHECK, [this]() { open_edit(); }},
        {},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

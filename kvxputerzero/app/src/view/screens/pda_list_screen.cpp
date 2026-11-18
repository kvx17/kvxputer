#include "pda_list_screen.h"

#include "asset_manager.h"
#include "theme.h"
#include "ui_const.h"

#include <cctype>
#include <cstdio>

namespace screen {
namespace {

const char* title_for(model::PdaKind kind) {
    switch (kind) {
    case model::PdaKind::Notes:
        return "Notes";
    case model::PdaKind::Memos:
        return "Memos";
    case model::PdaKind::Todo:
        return "To-Do";
    case model::PdaKind::Contacts:
        return "Contacts";
    case model::PdaKind::Alarms:
        return "Alarms";
    }
    return "PDA";
}

} // namespace

PdaListScreen::PdaListScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    const std::string& stash = view_model.model().work_output();
    if (stash.rfind("__filter__", 0) == 0) {
        filter_ = stash.substr(10);
        view_model.model().set_work_output("");
    }
    init();
}

void PdaListScreen::build() {
    make_header(content(), title_for(view_model().model().pda_kind()));

    list_ = lv_obj_create(content());
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, view::kScreenWidth, view::kContentHeight - 18);
    lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 2, 0);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);

    reload();
}

void PdaListScreen::reload() {
    labels_.clear();
    rows_.clear();
    lv_obj_clean(list_);

    std::string error;
    auto& model = view_model().model();
    selected_ = model.pda_item_index();

    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        notes_.clear();
        tools::pda::list_notes(notes_, error);
        for (const auto& n : notes_) labels_.push_back(n.title.empty() ? "(untitled)" : n.title);
        break;
    case model::PdaKind::Memos:
        memos_.clear();
        tools::pda::load_memos(memos_, error);
        for (const auto& m : memos_) {
            std::string s = m;
            if (s.size() > 36) s = s.substr(0, 36) + "...";
            labels_.push_back(s.empty() ? "(empty)" : s);
        }
        break;
    case model::PdaKind::Todo:
        tasks_.clear();
        tools::pda::load_tasks(tasks_, error);
        for (const auto& t : tasks_) {
            std::string s = t.done ? "[x] " : "[ ] ";
            s += t.text;
            if (!t.due.empty()) s += " (" + t.due + ")";
            if (s.size() > 36) s = s.substr(0, 36) + "...";
            labels_.push_back(s);
        }
        break;
    case model::PdaKind::Contacts: {
        contacts_.clear();
        tools::pda::load_contacts(contacts_, error);
        std::string f = filter_;
        for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (const auto& c : contacts_) {
            if (!f.empty()) {
                std::string n = c.name;
                for (char& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (n.find(f) == std::string::npos) continue;
            }
            std::string s = c.name;
            if (!c.phone.empty()) s += " " + c.phone;
            if (s.size() > 36) s = s.substr(0, 36) + "...";
            labels_.push_back(s);
        }
        break;
    }
    case model::PdaKind::Alarms:
        alarms_.clear();
        tools::pda::load_alarms(alarms_, error);
        for (const auto& a : alarms_) labels_.push_back(tools::pda::alarm_label(a));
        break;
    }

    if (labels_.empty()) labels_.push_back("(empty — key 8 New)");

    auto* font = assets().load_standard_font(14);
    for (size_t i = 0; i < labels_.size(); ++i) {
        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, view::kScreenWidth - 8, 20);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        auto* label = lv_label_create(row);
        lv_label_set_text(label, labels_[i].c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, view::kScreenWidth - 20);
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
        rows_.push_back(row);
    }
    refresh();
}

void PdaListScreen::refresh() {
    if (rows_.empty()) return;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = static_cast<int>(rows_.size()) - 1;
    view_model().model().set_pda_item_index(selected_);
    for (size_t i = 0; i < rows_.size(); ++i) {
        const bool on = static_cast<int>(i) == selected_;
        lv_obj_set_style_bg_color(rows_[i], on ? pri_color() : lv_color_hex(0x1A1A22), 0);
        auto* label = lv_obj_get_child(rows_[i], 0);
        if (label) lv_obj_set_style_text_color(label, on ? lv_color_hex(0xFFFFFF) : sec_color(), 0);
    }
    lv_obj_scroll_to_view(rows_[static_cast<size_t>(selected_)], LV_ANIM_OFF);
}

void PdaListScreen::move_selection(int delta) {
    if (rows_.empty()) return;
    const int count = static_cast<int>(rows_.size());
    selected_ = (selected_ + delta + count) % count;
    refresh();
}

void PdaListScreen::create_new() {
    auto& model = view_model().model();
    model.set_work_input("");
    model.set_pda_path("");
    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        view_model().open_pda_edit(model::PdaEditMode::New, true);
        break;
    case model::PdaKind::Memos:
        view_model().open_pda_edit(model::PdaEditMode::New, false);
        break;
    case model::PdaKind::Todo:
        view_model().open_pda_edit(model::PdaEditMode::New, false);
        break;
    case model::PdaKind::Contacts:
        model.set_pda_edit_field(0);
        view_model().open_pda_edit(model::PdaEditMode::PromptField, false);
        break;
    case model::PdaKind::Alarms: {
        const auto now = tools::pda::local_now();
        model.set_cal_date(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
        model.set_work_input(buf);
        view_model().open_pda_edit(model::PdaEditMode::PromptDate, false);
        break;
    }
    }
}

void PdaListScreen::open_item_actions() {
    auto& model = view_model().model();
    const bool empty_placeholder = labels_.size() == 1 && labels_[0].rfind("(empty", 0) == 0;
    if (empty_placeholder) {
        create_new();
        return;
    }

    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        if (selected_ < 0 || selected_ >= static_cast<int>(notes_.size())) return;
        model.set_pda_path(notes_[static_cast<size_t>(selected_)].path);
        {
            std::string body, error;
            tools::pda::read_note(model.pda_path(), body, error);
            model.set_work_input(body);
            model.set_work_output(body);
        }
        view_model().open_pda_view();
        break;
    case model::PdaKind::Memos:
        if (selected_ < 0 || selected_ >= static_cast<int>(memos_.size())) return;
        model.set_work_input(memos_[static_cast<size_t>(selected_)]);
        model.set_work_output(memos_[static_cast<size_t>(selected_)]);
        view_model().open_pda_view();
        break;
    case model::PdaKind::Todo:
        if (selected_ < 0 || selected_ >= static_cast<int>(tasks_.size())) return;
        // Toggle done on activate (quick); long path via edit uses 'e'
        tasks_[static_cast<size_t>(selected_)].done = !tasks_[static_cast<size_t>(selected_)].done;
        {
            std::string error;
            tools::pda::save_tasks(tasks_, error);
        }
        reload();
        break;
    case model::PdaKind::Contacts:
        if (selected_ < 0 || selected_ >= static_cast<int>(labels_.size())) return;
        // Map filtered selection back by label match — store absolute index in path as index string
        {
            // Rebuild filtered list to find contact
            std::string f = filter_;
            for (char& c : f) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            int shown = -1;
            for (size_t i = 0; i < contacts_.size(); ++i) {
                if (!f.empty()) {
                    std::string n = contacts_[i].name;
                    for (char& ch : n) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    if (n.find(f) == std::string::npos) continue;
                }
                ++shown;
                if (shown == selected_) {
                    model.set_pda_item_index(static_cast<int>(i));
                    model.set_work_output(contacts_[i].name + "\n" + contacts_[i].phone + "\n" + contacts_[i].address);
                    model.set_work_input(contacts_[i].name);
                    view_model().open_pda_view();
                    return;
                }
            }
        }
        break;
    case model::PdaKind::Alarms:
        if (selected_ < 0 || selected_ >= static_cast<int>(alarms_.size())) return;
        model.set_pda_item_index(selected_);
        model.set_work_output(tools::pda::alarm_label(alarms_[static_cast<size_t>(selected_)]));
        view_model().open_pda_view();
        break;
    }
}

void PdaListScreen::activate() {
    open_item_actions();
}

bool PdaListScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_DOWN) {
        move_selection(1);
        return true;
    }
    if (key == LV_KEY_UP) {
        move_selection(-1);
        return true;
    }
    if (key == LV_KEY_ENTER || key == ' ' || key == '\n') {
        activate();
        return true;
    }
    if (key == 'n' || key == 'N') {
        create_new();
        return true;
    }
    if (key == 'e' || key == 'E') {
        auto& model = view_model().model();
        const bool empty_placeholder = labels_.size() == 1 && labels_[0].rfind("(empty", 0) == 0;
        if (empty_placeholder) return true;
        switch (model.pda_kind()) {
        case model::PdaKind::Notes:
            if (selected_ < static_cast<int>(notes_.size())) {
                model.set_pda_path(notes_[static_cast<size_t>(selected_)].path);
                std::string body, error;
                tools::pda::read_note(model.pda_path(), body, error);
                model.set_work_input(body);
                view_model().open_pda_edit(model::PdaEditMode::Edit, true);
            }
            break;
        case model::PdaKind::Memos:
            if (selected_ < static_cast<int>(memos_.size())) {
                model.set_work_input(memos_[static_cast<size_t>(selected_)]);
                view_model().open_pda_edit(model::PdaEditMode::Edit, false);
            }
            break;
        case model::PdaKind::Todo:
            if (selected_ < static_cast<int>(tasks_.size())) {
                model.set_work_input(tasks_[static_cast<size_t>(selected_)].text);
                view_model().open_pda_edit(model::PdaEditMode::Edit, false);
            }
            break;
        case model::PdaKind::Contacts:
            open_item_actions();
            break;
        case model::PdaKind::Alarms:
            if (selected_ < static_cast<int>(alarms_.size())) {
                model.set_work_input(alarms_[static_cast<size_t>(selected_)].label);
                view_model().open_pda_edit(model::PdaEditMode::Edit, false);
            }
            break;
        }
        return true;
    }
    if (key == LV_KEY_DEL || key == 127 || key == 'x' || key == 'X') {
        auto& model = view_model().model();
        std::string error;
        switch (model.pda_kind()) {
        case model::PdaKind::Notes:
            if (selected_ < static_cast<int>(notes_.size())) {
                tools::pda::delete_note(notes_[static_cast<size_t>(selected_)].path, error);
            }
            break;
        case model::PdaKind::Memos:
            if (selected_ < static_cast<int>(memos_.size())) {
                memos_.erase(memos_.begin() + selected_);
                tools::pda::save_memos(memos_, error);
            }
            break;
        case model::PdaKind::Todo:
            if (selected_ < static_cast<int>(tasks_.size())) {
                tasks_.erase(tasks_.begin() + selected_);
                tools::pda::save_tasks(tasks_, error);
            }
            break;
        case model::PdaKind::Contacts:
            if (selected_ < static_cast<int>(contacts_.size()) && filter_.empty()) {
                contacts_.erase(contacts_.begin() + selected_);
                tools::pda::save_contacts(contacts_, error);
            }
            break;
        case model::PdaKind::Alarms:
            if (selected_ < static_cast<int>(alarms_.size())) {
                alarms_.erase(alarms_.begin() + selected_);
                tools::pda::save_alarms(alarms_, error);
            }
            break;
        }
        reload();
        return true;
    }
    if ((key == 'f' || key == 'F') && view_model().model().pda_kind() == model::PdaKind::Contacts) {
        view_model().model().set_work_input(filter_);
        view_model().open_pda_edit(model::PdaEditMode::PromptFilter, false);
        return true;
    }
    if ((key == 'd' || key == 'D') && view_model().model().pda_kind() == model::PdaKind::Todo) {
        if (selected_ < static_cast<int>(tasks_.size())) {
            view_model().model().set_work_input(tasks_[static_cast<size_t>(selected_)].due);
            view_model().open_pda_edit(model::PdaEditMode::PromptDue, false);
        }
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaListScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { move_selection(-1); }},
        {view::ICON_CHECK, [this]() { activate(); }},
        {view::ICON_CARET_DOWN, [this]() { move_selection(1); }},
        {view::ICON_PLUS, [this]() { create_new(); }},
    }};
}

} // namespace screen

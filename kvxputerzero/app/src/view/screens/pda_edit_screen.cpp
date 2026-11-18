#include "pda_edit_screen.h"

#include "asset_manager.h"
#include "pda_lib.h"
#include "theme.h"
#include "ui_const.h"

namespace screen {

PdaEditScreen::PdaEditScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    multiline_ = view_model.model().pda_multiline();
    const auto mode = view_model.model().pda_edit_mode();
    switch (mode) {
    case model::PdaEditMode::PromptDate:
        title_ = "Date YYYYMMDD";
        break;
    case model::PdaEditMode::PromptTime:
        title_ = "Time HHMM";
        break;
    case model::PdaEditMode::PromptFilter:
        title_ = "Filter";
        break;
    case model::PdaEditMode::PromptDue:
        title_ = "Due YYYY-MM-DD";
        break;
    case model::PdaEditMode::PromptField: {
        const int f = view_model.model().pda_edit_field();
        if (view_model.model().pda_kind() == model::PdaKind::Contacts) {
            title_ = f == 0 ? "Name" : (f == 1 ? "Phone" : "Address");
        } else {
            title_ = "Edit";
        }
        break;
    }
    default:
        switch (view_model.model().pda_kind()) {
        case model::PdaKind::Notes:
            title_ = multiline_ ? "Note" : "Note";
            break;
        case model::PdaKind::Memos:
            title_ = "Memo";
            break;
        case model::PdaKind::Todo:
            title_ = "Task";
            break;
        case model::PdaKind::Alarms:
            title_ = "Label";
            break;
        default:
            title_ = "Edit";
            break;
        }
        break;
    }
    // Calendar event title uses work_output as marker
    if (view_model.model().work_output() == "__cal_event__") title_ = "Event title";
    init();
}

void PdaEditScreen::build() {
    make_header(content(), title_.c_str());

    input_ta_ = lv_textarea_create(content());
    lv_obj_set_size(input_ta_, view::kScreenWidth - 8, view::kContentHeight - 22);
    lv_obj_align(input_ta_, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_text(input_ta_, view_model().work_input().c_str());
    if (!multiline_) {
        lv_textarea_set_one_line(input_ta_, true);
    }
    lv_obj_set_style_bg_color(input_ta_, lv_color_hex(0x1A1A22), 0);
    lv_obj_set_style_bg_opa(input_ta_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input_ta_, sec_color(), 0);
    auto* font = assets().load_standard_font(14);
    lv_obj_set_style_text_font(input_ta_, font ? font : &lv_font_montserrat_14, 0);
    lv_obj_add_state(input_ta_, LV_STATE_FOCUSED);
}

const char* PdaEditScreen::typing_hint() const {
    return multiline_ ? "Type  6=Save  Esc=Cancel" : "Type  Enter=OK  Esc=Cancel";
}

bool PdaEditScreen::commit_notes() {
    std::string path = view_model().model().pda_path();
    const std::string body = lv_textarea_get_text(input_ta_);
    std::string error;
    if (!tools::pda::save_note(path, body, error)) return false;
    view_model().model().set_pda_path(path);
    return true;
}

bool PdaEditScreen::commit_memos() {
    std::vector<std::string> lines;
    std::string error;
    tools::pda::load_memos(lines, error);
    const std::string text = tools::pda::strip_pipes(lv_textarea_get_text(input_ta_));
    if (text.empty()) return false;
    if (view_model().model().pda_edit_mode() == model::PdaEditMode::Edit) {
        const int idx = view_model().model().pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(lines.size())) lines[static_cast<size_t>(idx)] = text;
        else lines.push_back(text);
    } else {
        lines.push_back(text);
    }
    return tools::pda::save_memos(lines, error);
}

bool PdaEditScreen::commit_todo() {
    std::vector<tools::pda::Task> tasks;
    std::string error;
    tools::pda::load_tasks(tasks, error);
    const std::string text = tools::pda::strip_pipes(lv_textarea_get_text(input_ta_));
    if (text.empty()) return false;
    if (view_model().model().pda_edit_mode() == model::PdaEditMode::Edit) {
        const int idx = view_model().model().pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(tasks.size())) {
            tasks[static_cast<size_t>(idx)].text = text;
        } else {
            tools::pda::Task t;
            t.text = text;
            tasks.push_back(t);
        }
    } else {
        tools::pda::Task t;
        t.text = text;
        tasks.push_back(t);
    }
    tools::pda::sort_tasks(tasks);
    return tools::pda::save_tasks(tasks, error);
}

bool PdaEditScreen::commit_contacts_field() {
    auto& model = view_model().model();
    const std::string text = tools::pda::strip_pipes(lv_textarea_get_text(input_ta_));
    const int field = model.pda_edit_field();
    // Stash fields in work_output as name|phone|address progressively
    std::string stash = model.work_output();
    if (stash.rfind("__contact__", 0) != 0) stash = "__contact__||";
    // parse after marker
    std::string payload = stash.substr(11); // after __contact__
    std::string parts[3] = {"", "", ""};
    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const auto p = payload.find('|', start);
        if (p == std::string::npos) {
            parts[i] = payload.substr(start);
            break;
        }
        parts[i] = payload.substr(start, p - start);
        start = p + 1;
    }
    parts[field] = text;
    if (field == 0 && text.empty()) return false;

    if (field < 2) {
        model.set_work_output(std::string("__contact__") + parts[0] + "|" + parts[1] + "|" + parts[2]);
        model.set_pda_edit_field(field + 1);
        model.set_work_input("");
        view_model().open_pda_edit(model::PdaEditMode::PromptField, false);
        return true; // stay in edit flow
    }

    std::vector<tools::pda::Contact> contacts;
    std::string error;
    tools::pda::load_contacts(contacts, error);
    tools::pda::Contact c;
    c.name = parts[0];
    c.phone = parts[1];
    c.address = parts[2];
    const int idx = model.pda_item_index();
    if (model.pda_edit_mode() == model::PdaEditMode::Edit ||
        (idx >= 0 && idx < static_cast<int>(contacts.size()) && model.work_output().rfind("__contact__", 0) == 0 &&
         !contacts.empty() && model.pda_path() == "__edit_contact__")) {
        if (idx >= 0 && idx < static_cast<int>(contacts.size())) contacts[static_cast<size_t>(idx)] = c;
        else contacts.push_back(c);
    } else if (model.pda_path() == "__edit_contact__" && idx >= 0 && idx < static_cast<int>(contacts.size())) {
        contacts[static_cast<size_t>(idx)] = c;
    } else {
        contacts.push_back(c);
    }
    tools::pda::save_contacts(contacts, error);
    model.set_work_output("");
    model.set_pda_path("");
    view_model().open_pda_list(model::PdaKind::Contacts);
    return true;
}

bool PdaEditScreen::commit_alarm_step() {
    auto& model = view_model().model();
    const auto mode = model.pda_edit_mode();
    const std::string text = lv_textarea_get_text(input_ta_);

    if (mode == model::PdaEditMode::PromptDate) {
        int y, m, d;
        if (!tools::pda::parse_yyyymmdd(text, y, m, d)) return false;
        model.set_cal_date(y, m, d);
        const auto now = tools::pda::local_now();
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d%02d", now.tm_hour, now.tm_min);
        model.set_work_input(buf);
        view_model().open_pda_edit(model::PdaEditMode::PromptTime, false);
        return true;
    }
    if (mode == model::PdaEditMode::PromptTime) {
        int h, mi;
        if (!tools::pda::parse_hhmm(text, h, mi)) return false;
        // stash time in work_output
        model.set_work_output(tools::pda::format_time(h, mi));
        model.set_work_input("");
        view_model().open_pda_edit(model::PdaEditMode::New, false); // label
        // Keep kind Alarms; title becomes Label via constructor on next screen
        return true;
    }
    // Label commit
    std::vector<tools::pda::Alarm> alarms;
    std::string error;
    tools::pda::load_alarms(alarms, error);
    if (mode == model::PdaEditMode::Edit) {
        const int idx = model.pda_item_index();
        if (idx >= 0 && idx < static_cast<int>(alarms.size())) {
            alarms[static_cast<size_t>(idx)].label = tools::pda::strip_pipes(text);
            tools::pda::save_alarms(alarms, error);
        }
        view_model().open_pda_list(model::PdaKind::Alarms);
        return true;
    }
    tools::pda::Alarm a;
    a.triggered = false;
    a.year = model.cal_year();
    a.month = model.cal_month();
    a.day = model.cal_day();
    int h = 0, mi = 0;
    tools::pda::parse_time(model.work_output(), h, mi);
    a.hour = h;
    a.minute = mi;
    a.label = tools::pda::strip_pipes(text);
    alarms.push_back(a);
    tools::pda::save_alarms(alarms, error);
    model.set_work_output("");
    view_model().open_pda_list(model::PdaKind::Alarms);
    return true;
}

bool PdaEditScreen::commit_filter() {
    // Store filter in work_output; list will read... actually list uses local filter_.
    // Use model work_output as filter stash.
    view_model().model().set_work_output(std::string("__filter__") + lv_textarea_get_text(input_ta_));
    view_model().open_pda_list(model::PdaKind::Contacts);
    return true;
}

bool PdaEditScreen::commit_due() {
    std::vector<tools::pda::Task> tasks;
    std::string error;
    tools::pda::load_tasks(tasks, error);
    const int idx = view_model().model().pda_item_index();
    if (idx < 0 || idx >= static_cast<int>(tasks.size())) return false;
    tasks[static_cast<size_t>(idx)].due = tools::pda::strip_pipes(lv_textarea_get_text(input_ta_));
    tools::pda::sort_tasks(tasks);
    tools::pda::save_tasks(tasks, error);
    view_model().open_pda_list(model::PdaKind::Todo);
    return true;
}

bool PdaEditScreen::commit_calendar_event() {
    auto& model = view_model().model();
    if (model.pda_edit_mode() == model::PdaEditMode::PromptTime) {
        int h, mi;
        if (!tools::pda::parse_hhmm(lv_textarea_get_text(input_ta_), h, mi)) return false;
        model.set_work_output(std::string("__cal_event__") + tools::pda::format_time(h, mi));
        model.set_work_input("");
        model.set_pda_edit_mode(model::PdaEditMode::New);
        model.set_pda_multiline(false);
        view_model().show_page(model::AppPage::PdaEdit);
        return true;
    }
    // Title
    std::string stash = model.work_output();
    std::string time = "00:00";
    if (stash.rfind("__cal_event__", 0) == 0) time = stash.substr(13);
    const std::string title = tools::pda::strip_pipes(lv_textarea_get_text(input_ta_));
    if (title.empty()) return false;
    std::vector<tools::pda::Event> events;
    std::string error;
    tools::pda::load_events(events, error);
    tools::pda::Event e;
    e.date = tools::pda::format_date(model.cal_year(), model.cal_month(), model.cal_day());
    e.time = time;
    e.title = title;
    events.push_back(e);
    tools::pda::save_events(events, error);
    model.set_work_output("");
    view_model().open_pda_day();
    return true;
}

void PdaEditScreen::commit() {
    auto& model = view_model().model();
    view_model().set_work_input(lv_textarea_get_text(input_ta_));

    if (model.work_output().rfind("__cal_event__", 0) == 0 ||
        (model.current_page() == model::AppPage::PdaEdit && model.pda_kind() == model::PdaKind::Notes &&
         false)) {
        // handled below
    }

    if (model.work_output().rfind("__cal_event__", 0) == 0 ||
        model.pda_edit_mode() == model::PdaEditMode::PromptTime && model.work_output().empty() &&
            model.pda_kind() != model::PdaKind::Alarms) {
        // Calendar time→title path sets __cal_event__; alarm uses PromptTime with empty marker differently.
    }

    // Calendar flow detected by marker or by coming from PdaDay (kind not list kinds alone).
    if (model.work_output().rfind("__cal_event__", 0) == 0 ||
        (model.pda_edit_mode() == model::PdaEditMode::PromptTime && model.pda_path() == "__calendar__")) {
        commit_calendar_event();
        return;
    }

    switch (model.pda_edit_mode()) {
    case model::PdaEditMode::PromptFilter:
        commit_filter();
        return;
    case model::PdaEditMode::PromptDue:
        commit_due();
        return;
    case model::PdaEditMode::PromptField:
        commit_contacts_field();
        return;
    case model::PdaEditMode::PromptDate:
    case model::PdaEditMode::PromptTime:
        if (model.pda_path() == "__calendar__") {
            commit_calendar_event();
            return;
        }
        commit_alarm_step();
        return;
    default:
        break;
    }

    switch (model.pda_kind()) {
    case model::PdaKind::Notes:
        if (commit_notes()) view_model().open_pda_list(model::PdaKind::Notes);
        break;
    case model::PdaKind::Memos:
        if (commit_memos()) view_model().open_pda_list(model::PdaKind::Memos);
        break;
    case model::PdaKind::Todo:
        if (commit_todo()) view_model().open_pda_list(model::PdaKind::Todo);
        break;
    case model::PdaKind::Alarms:
        commit_alarm_step();
        break;
    case model::PdaKind::Contacts:
        commit_contacts_field();
        break;
    }
}

bool PdaEditScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        view_model().go_back();
        return true;
    }
    if (!multiline_ && (key == LV_KEY_ENTER || key == '\n')) {
        commit();
        return true;
    }
    // Multiline: key 6 / Controlless 'save' via ActionBar; also Ctrl not available — use F5-like: key == 6 ASCII?
    // ActionBar slot 2 handles save. Enter inserts newline via textarea default if we don't consume.
    if (multiline_ && key == LV_KEY_ENTER) {
        return false; // let default typing path... but we are in handle_key for screen keys
    }
    // Printable characters go to textarea
    if (key >= 32 && key < 127) {
        lv_textarea_add_char(input_ta_, key);
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == 8) {
        lv_textarea_delete_char(input_ta_);
        return true;
    }
    if (key == LV_KEY_DEL || key == 127) {
        lv_textarea_delete_char_forward(input_ta_);
        return true;
    }
    if (key == LV_KEY_LEFT) {
        lv_textarea_cursor_left(input_ta_);
        return true;
    }
    if (key == LV_KEY_RIGHT) {
        lv_textarea_cursor_right(input_ta_);
        return true;
    }
    if (key == LV_KEY_UP) {
        lv_textarea_cursor_up(input_ta_);
        return true;
    }
    if (key == LV_KEY_DOWN) {
        lv_textarea_cursor_down(input_ta_);
        return true;
    }
    if (multiline_ && (key == LV_KEY_ENTER || key == '\n')) {
        lv_textarea_add_char(input_ta_, '\n');
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> PdaEditScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {},
        {view::ICON_CHECK, [this]() { commit(); }},
        {},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen

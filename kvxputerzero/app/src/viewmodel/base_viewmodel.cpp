#include "base_viewmodel.h"

#include "launch_catalog.h"

#include <cstdio>
#include <ctime>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      greeting_subject_(""),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      channel_index_subject_(model_.channel_index()),
      menu_index_subject_(model_.menu_index()),
      pri_color_subject_(static_cast<int>(model_.pri_color())),
      sec_color_subject_(static_cast<int>(model_.sec_color())),
      bg_color_subject_(static_cast<int>(model_.bg_color())),
      counter_subject_(0),
      bold_text_subject_(false),
      info_visible_subject_(false),
      quit_requested_subject_(false) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::greeting_subject() {
    return greeting_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::channel_index_subject() {
    return channel_index_subject_.native();
}

lv_subject_t* BaseViewModel::menu_index_subject() {
    return menu_index_subject_.native();
}

lv_subject_t* BaseViewModel::pri_color_subject() {
    return pri_color_subject_.native();
}

lv_subject_t* BaseViewModel::sec_color_subject() {
    return sec_color_subject_.native();
}

lv_subject_t* BaseViewModel::bg_color_subject() {
    return bg_color_subject_.native();
}

lv_subject_t* BaseViewModel::counter_subject() {
    return counter_subject_.native();
}

lv_subject_t* BaseViewModel::bold_text_subject() {
    return bold_text_subject_.native();
}

lv_subject_t* BaseViewModel::info_visible_subject() {
    return info_visible_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    // KvxScreens paint from pri/sec/bg tokens, not LVGL's dark flag alone.
    if (enabled) {
        if (model_.bg_color() == 0xFFFFFF || model_.bg_color() == 0xF0F0F0 ||
            model_.bg_color() == 0xF5F5F5) {
            model_.set_bg_color(model::kBgDefault);
        }
    } else if (model_.bg_color() == 0x000000 || model_.bg_color() == model::kBgDefault) {
        model_.set_bg_color(0xF0F0F0);
    }
    model_.save_user_config();
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    set_dark_mode(!model_.dark_mode());
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(model::AppPage page) {
    model_.set_current_page(page);
    publish_all();
}

void BaseViewModel::go_home() {
    show_page(model::AppPage::ChannelGrid);
}

void BaseViewModel::go_back() {
    switch (model_.current_page()) {
    case model::AppPage::ChannelGrid:
        request_quit();
        break;
    case model::AppPage::CodecWork:
        show_page(model::AppPage::CodecMenu);
        break;
    case model::AppPage::EngCalcWork:
    case model::AppPage::LifeCounter:
        show_page(model_.eng_calc_group() >= 0 ? model::AppPage::EngCalcGroup
                                                : model::AppPage::EngCalcMenu);
        break;
    case model::AppPage::EngCalcGroup:
        show_page(model::AppPage::EngCalcMenu);
        break;
    case model::AppPage::ColorHex:
        show_page(model::AppPage::ColorPicker);
        break;
    case model::AppPage::ColorPicker:
    case model::AppPage::About:
        show_page(model::AppPage::ConfigMenu);
        break;
    case model::AppPage::TotpShow:
    case model::AppPage::TotpAdd:
    case model::AppPage::TotpHelp:
        show_page(model::AppPage::TotpMenu);
        break;
    case model::AppPage::ComboWork:
    case model::AppPage::ComboHelp:
        show_page(model::AppPage::ComboMenu);
        break;
    case model::AppPage::QrCustom:
    case model::AppPage::QrSave:
    case model::AppPage::QrPix:
    case model::AppPage::QrView:
    case model::AppPage::QrRemove:
    case model::AppPage::QrUsefulLinks:
        show_page(model::AppPage::QrMenu);
        break;
    case model::AppPage::BarcodeWork:
    case model::AppPage::BarcodeView:
        show_page(model::AppPage::BarcodeMenu);
        break;
    case model::AppPage::PdaEdit:
        if (model_.pda_path() == "__calendar__" || model_.work_output().rfind("__cal_event__", 0) == 0) {
            model_.set_work_output("");
            model_.set_pda_path("");
            show_page(model::AppPage::PdaDay);
        } else {
            show_page(model::AppPage::PdaList);
        }
        break;
    case model::AppPage::PdaView:
        show_page(model::AppPage::PdaList);
        break;
    case model::AppPage::PdaDay:
        show_page(model::AppPage::PdaCalendar);
        break;
    case model::AppPage::PdaList:
    case model::AppPage::PdaCalendar:
    case model::AppPage::PdaWorldClock:
    case model::AppPage::PdaRebind:
        show_page(model::AppPage::PdaHub);
        break;
    case model::AppPage::Calculator:
        if (model_.pda_path() == "__from_pda__") {
            model_.set_pda_path("");
            show_page(model::AppPage::PdaHub);
        } else {
            show_page(model::AppPage::ChannelMenu);
        }
        break;
    case model::AppPage::Brightness:
    case model::AppPage::DimTime:
    case model::AppPage::SoundMenu:
    case model::AppPage::PowerMenu:
        show_page(model::AppPage::ConfigMenu);
        break;
    case model::AppPage::PdaHub:
        show_page(model::AppPage::ChannelMenu);
        break;
    case model::AppPage::ClockSubMenu:
        show_page(model::AppPage::ClockFace);
        break;
    case model::AppPage::Timer:
        if (model_.current_channel().kind == model::ChannelKind::ClockFill) {
            show_page(model::AppPage::ClockSubMenu);
        } else {
            show_page(model::AppPage::ChannelMenu);
        }
        break;
    case model::AppPage::ClockFace:
    case model::AppPage::ChargeFace:
    case model::AppPage::Camera:
    case model::AppPage::Gallery:
        show_page(model::AppPage::ChannelGrid);
        break;
    case model::AppPage::Matrix:
        show_page(model::AppPage::ChannelMenu);
        break;
    case model::AppPage::ToolRun:
    case model::AppPage::ToolPrompt: {
        // Nested tool steps store stage in tool_param; Esc returns to the channel tool menu
        // (or Tools / Discovery), not the previous sub-step.
        if (model_.current_channel().kind == model::ChannelKind::DiscoveryFill) {
            show_page(model::AppPage::Discovery);
        } else if (model_.current_channel().kind == model::ChannelKind::ToolsFill) {
            show_page(model::AppPage::ChannelMenu);
        } else {
            show_page(model::AppPage::ToolMenu);
        }
        break;
    }
    case model::AppPage::ToolMenu:
    case model::AppPage::Discovery:
    case model::AppPage::Search:
        show_page(model::AppPage::ChannelGrid);
        break;
    case model::AppPage::ShortcutCapture:
    case model::AppPage::ShortcutPick:
        show_page(model::AppPage::ShortcutMenu);
        break;
    case model::AppPage::ShortcutMenu:
        show_page(model::AppPage::ConfigMenu);
        break;
    case model::AppPage::CodecMenu:
    case model::AppPage::EngCalcMenu:
    case model::AppPage::Passgen:
    case model::AppPage::TotpMenu:
    case model::AppPage::ComboMenu:
    case model::AppPage::QrMenu:
    case model::AppPage::BarcodeMenu:
        show_page(model::AppPage::ChannelMenu);
        break;
    case model::AppPage::ConfigMenu:
    case model::AppPage::ChannelMenu:
    case model::AppPage::Placeholder:
        show_page(model::AppPage::ChannelGrid);
        break;
    default:
        show_page(model::AppPage::ChannelGrid);
        break;
    }
}

int BaseViewModel::channel_index() const {
    return model_.channel_index();
}

void BaseViewModel::set_channel_index(int index) {
    model_.set_channel_index(index);
    channel_index_subject_.set(model_.channel_index());
}

void BaseViewModel::move_channel(int delta) {
    const int count = static_cast<int>(model::kChannelCount);
    int next = (model_.channel_index() + delta) % count;
    if (next < 0) next += count;
    set_channel_index(next);
}

void BaseViewModel::open_selected_channel() {
    const auto& ch = model_.current_channel();
    switch (ch.kind) {
    case model::ChannelKind::ToolsFill:
        show_page(model::AppPage::ChannelMenu);
        break;
    case model::ChannelKind::ConfigFill:
        show_page(model::AppPage::ConfigMenu);
        break;
    case model::ChannelKind::ClockFill:
        show_page(model::AppPage::ClockFace);
        break;
    case model::ChannelKind::ChargeFill:
        show_page(model::AppPage::ChargeFace);
        break;
    case model::ChannelKind::CameraFill:
        show_page(model::AppPage::Camera);
        break;
    case model::ChannelKind::GalleryFill:
        show_page(model::AppPage::Gallery);
        break;
    case model::ChannelKind::ChannelFill:
        open_tool_menu();
        break;
    case model::ChannelKind::DiscoveryFill:
        model_.set_discovery_filter("");
        open_discovery();
        break;
    case model::ChannelKind::ComingSoon:
    case model::ChannelKind::UseStock:
        show_page(model::AppPage::Placeholder);
        break;
    }
}

void BaseViewModel::open_tool_menu() {
    show_page(model::AppPage::ToolMenu);
}

void BaseViewModel::open_tool(model::ToolId id, std::string param) {
    if (id == model::ToolId::Calculator) {
        open_calculator();
        return;
    }
    // Tool screens all use ToolRun/ToolPrompt. ScreenManager skips a rebuild when the
    // AppPage enum is unchanged, so nested tool→tool opens (NFC Classic… → Dict, etc.)
    // must bounce between ToolRun and ToolPrompt to force a new screen.
    const auto prev = model_.current_page();
    model_.set_active_tool_id(id);
    model_.set_tool_param(std::move(param));
    if (prev == model::AppPage::ToolRun) {
        show_page(model::AppPage::ToolPrompt);
        return;
    }
    if (prev == model::AppPage::ToolPrompt) {
        show_page(model::AppPage::ToolRun);
        return;
    }
    show_page(model::AppPage::ToolRun);
}

void BaseViewModel::open_discovery() {
    show_page(model::AppPage::Discovery);
}

void BaseViewModel::open_search() {
    show_page(model::AppPage::Search);
}

void BaseViewModel::open_shortcut_menu() {
    show_page(model::AppPage::ShortcutMenu);
}

void BaseViewModel::begin_shortcut_bind(bool hold) {
    model_.set_shortcut_bind_hold(hold);
    model_.set_shortcut_bind_target("");
    show_page(model::AppPage::ShortcutPick);
}

void BaseViewModel::choose_shortcut_target(std::string id) {
    model_.set_shortcut_bind_target(std::move(id));
    show_page(model::AppPage::ShortcutCapture);
}

bool BaseViewModel::commit_shortcut(uint32_t key) {
    if (model_.shortcut_bind_target().empty()) return false;
    model_.set_shortcut(key, model_.shortcut_bind_hold(), model_.shortcut_bind_target());
    model_.save_user_config();
    show_page(model::AppPage::ShortcutMenu);
    return true;
}

void BaseViewModel::remove_shortcut(uint32_t key, bool hold) {
    model_.set_shortcut(key, hold, "");
    model_.save_user_config();
    show_page(model::AppPage::ConfigMenu);
    show_page(model::AppPage::ShortcutMenu);
}

void BaseViewModel::clear_shortcuts() {
    model_.clear_shortcuts();
    model_.save_user_config();
    show_page(model::AppPage::ConfigMenu);
    show_page(model::AppPage::ShortcutMenu);
}

bool BaseViewModel::launch_target_id(const std::string& id) {
    const auto* target = model::find_launch_target(id);
    if (!target) return false;
    if (target->channel_index >= 0) set_channel_index(target->channel_index);
    switch (target->kind) {
    case model::LaunchKind::Channel:
        open_selected_channel();
        break;
    case model::LaunchKind::Tool:
        open_tool(target->tool);
        break;
    case model::LaunchKind::Page:
        if (target->page == model::AppPage::ColorPicker) {
            auto which = model::ColorTarget::Primary;
            if (target->aux == 1) which = model::ColorTarget::Accent;
            else if (target->aux == 2) which = model::ColorTarget::Background;
            open_color_picker(which);
        } else if (target->page == model::AppPage::Calculator) {
            open_calculator();
        } else if (target->page == model::AppPage::PdaHub) {
            open_pda_hub();
        } else {
            show_page(target->page);
        }
        break;
    }
    return true;
}

void BaseViewModel::remember_search(const std::string& id) {
    model_.push_recent_search(id);
    model_.save_user_config();
}

model::ToolId BaseViewModel::active_tool_id() const {
    return model_.active_tool_id();
}

const std::string& BaseViewModel::tool_param() const {
    return model_.tool_param();
}

const std::string& BaseViewModel::discovery_filter() const {
    return model_.discovery_filter();
}

void BaseViewModel::set_discovery_filter(std::string value) {
    model_.set_discovery_filter(std::move(value));
}

int BaseViewModel::menu_index() const {
    return model_.menu_index();
}

void BaseViewModel::set_menu_index(int index) {
    model_.set_menu_index(index);
    menu_index_subject_.set(model_.menu_index());
}

void BaseViewModel::move_menu(int delta) {
    set_menu_index(model_.menu_index() + delta);
}

void BaseViewModel::open_codec_menu() {
    show_page(model::AppPage::CodecMenu);
}

void BaseViewModel::open_codec_op(int op) {
    model_.set_codec_op(op);
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::CodecWork);
}

void BaseViewModel::open_eng_calc_menu() {
    model_.set_eng_calc_group(-1);
    show_page(model::AppPage::EngCalcMenu);
}

void BaseViewModel::open_eng_calc_group(int group) {
    model_.set_eng_calc_group(group);
    show_page(model::AppPage::EngCalcGroup);
}

void BaseViewModel::open_eng_calc_op(int op) {
    model_.set_eng_calc_op(op);
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::EngCalcWork);
}

void BaseViewModel::open_life_counter(int game) {
    model_.set_eng_calc_op(game);
    show_page(model::AppPage::LifeCounter);
}

void BaseViewModel::open_timer() {
    show_page(model::AppPage::Timer);
}

void BaseViewModel::open_matrix() {
    show_page(model::AppPage::Matrix);
}

void BaseViewModel::open_config_menu() {
    show_page(model::AppPage::ConfigMenu);
}

void BaseViewModel::open_color_picker(model::ColorTarget target) {
    model_.set_color_target(target);
    show_page(model::AppPage::ColorPicker);
}

void BaseViewModel::open_color_hex() {
    char buf[16];
    uint32_t rgb = model_.pri_color();
    if (model_.color_target() == model::ColorTarget::Accent) {
        rgb = model_.sec_color();
    } else if (model_.color_target() == model::ColorTarget::Background) {
        rgb = model_.bg_color();
    }
    std::snprintf(buf, sizeof(buf), "%06X", rgb & 0xFFFFFFu);
    model_.set_work_input(buf);
    model_.set_work_output("");
    show_page(model::AppPage::ColorHex);
}

void BaseViewModel::open_about() {
    show_page(model::AppPage::About);
}

void BaseViewModel::reset_colors() {
    model_.reset_colors();
    pri_color_subject_.set(static_cast<int>(model_.pri_color()));
    sec_color_subject_.set(static_cast<int>(model_.sec_color()));
    bg_color_subject_.set(static_cast<int>(model_.bg_color()));
    persist_theme();
}

void BaseViewModel::open_passgen() {
    show_page(model::AppPage::Passgen);
}

void BaseViewModel::open_totp_menu() {
    show_page(model::AppPage::TotpMenu);
}

void BaseViewModel::open_totp_show() {
    show_page(model::AppPage::TotpShow);
}

void BaseViewModel::open_totp_add() {
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::TotpAdd);
}

void BaseViewModel::open_totp_help() {
    show_page(model::AppPage::TotpHelp);
}

void BaseViewModel::open_combo_menu() {
    show_page(model::AppPage::ComboMenu);
}

void BaseViewModel::open_combo_work() {
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::ComboWork);
}

void BaseViewModel::open_combo_help() {
    show_page(model::AppPage::ComboHelp);
}

void BaseViewModel::open_qr_menu() {
    show_page(model::AppPage::QrMenu);
}

void BaseViewModel::open_qr_custom() {
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::QrCustom);
}

void BaseViewModel::open_qr_save() {
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::QrSave);
}

void BaseViewModel::open_qr_pix() {
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::QrPix);
}

void BaseViewModel::open_qr_remove() {
    show_page(model::AppPage::QrRemove);
}

void BaseViewModel::open_qr_view() {
    show_page(model::AppPage::QrView);
}

void BaseViewModel::open_qr_useful_links() {
    show_page(model::AppPage::QrUsefulLinks);
}

void BaseViewModel::open_barcode_menu() {
    show_page(model::AppPage::BarcodeMenu);
}

void BaseViewModel::open_barcode_work(int type) {
    model_.set_barcode_type(type);
    model_.set_work_input("");
    model_.set_work_output("");
    show_page(model::AppPage::BarcodeWork);
}

void BaseViewModel::open_barcode_view() {
    show_page(model::AppPage::BarcodeView);
}

void BaseViewModel::open_pda_hub() {
    model_.set_work_output("");
    show_page(model::AppPage::PdaHub);
}

void BaseViewModel::open_pda_channel(int channel) {
    switch (channel) {
    case 0:
        open_pda_list(model::PdaKind::Notes);
        break;
    case 1:
        open_pda_list(model::PdaKind::Memos);
        break;
    case 2:
        open_pda_list(model::PdaKind::Todo);
        break;
    case 3:
        open_pda_calendar();
        break;
    case 4:
        open_pda_list(model::PdaKind::Contacts);
        break;
    case 5:
        open_pda_list(model::PdaKind::Alarms);
        break;
    case 6:
        open_pda_world_clock();
        break;
    case 7:
        open_pda_stock_calc();
        break;
    default:
        break;
    }
}

void BaseViewModel::open_pda_list(model::PdaKind kind) {
    model_.set_pda_kind(kind);
    model_.set_pda_item_index(0);
    show_page(model::AppPage::PdaList);
}

void BaseViewModel::open_pda_edit(model::PdaEditMode mode, bool multiline) {
    model_.set_pda_edit_mode(mode);
    model_.set_pda_multiline(multiline);
    show_page(model::AppPage::PdaEdit);
}

void BaseViewModel::open_pda_view() {
    show_page(model::AppPage::PdaView);
}

void BaseViewModel::open_pda_calendar() {
    const std::time_t now_t = std::time(nullptr);
    std::tm now{};
#if defined(_WIN32)
    localtime_s(&now, &now_t);
#else
    localtime_r(&now_t, &now);
#endif
    int y = now.tm_year + 1900;
    int m = now.tm_mon + 1;
    int d = now.tm_mday;
    if (y < 2000) {
        y = 2025;
        m = 1;
        d = 1;
    }
    model_.set_cal_date(y, m, d);
    show_page(model::AppPage::PdaCalendar);
}

void BaseViewModel::open_pda_day() {
    show_page(model::AppPage::PdaDay);
}

void BaseViewModel::open_pda_world_clock() {
    show_page(model::AppPage::PdaWorldClock);
}

void BaseViewModel::open_pda_rebind() {
    model_.set_work_output("");
    show_page(model::AppPage::PdaRebind);
}

void BaseViewModel::open_pda_stock_calc() {
    model_.set_pda_path("__from_pda__");
    show_page(model::AppPage::Calculator);
}

void BaseViewModel::open_calculator() {
    model_.set_pda_path("");
    show_page(model::AppPage::Calculator);
}

void BaseViewModel::open_brightness() {
    show_page(model::AppPage::Brightness);
}

void BaseViewModel::open_dim_time() {
    show_page(model::AppPage::DimTime);
}

void BaseViewModel::open_sound_menu() {
    show_page(model::AppPage::SoundMenu);
}

void BaseViewModel::open_power_menu() {
    show_page(model::AppPage::PowerMenu);
}

void BaseViewModel::persist_pda_config() {
    model_.save_user_config();
}

void BaseViewModel::set_qr_view_data(std::vector<uint8_t> modules, int size, std::string payload) {
    model_.set_glyph_kind(model::GlyphKind::Qr);
    model_.set_glyph_payload(std::move(payload));
    model_.set_glyph_invert(true);
    model_.set_qr_modules(std::move(modules), size);
}

void BaseViewModel::set_barcode_view_data(std::string bits, std::string label, std::string payload) {
    model_.set_glyph_kind(model::GlyphKind::Barcode);
    model_.set_glyph_payload(std::move(payload));
    model_.set_glyph_bits(std::move(bits));
    model_.set_glyph_label(std::move(label));
    model_.set_glyph_invert(true);
}

model::ColorTarget BaseViewModel::color_target() const {
    return model_.color_target();
}

uint32_t BaseViewModel::pri_color() const {
    return model_.pri_color();
}

uint32_t BaseViewModel::sec_color() const {
    return model_.sec_color();
}

uint32_t BaseViewModel::bg_color() const {
    return model_.bg_color();
}

void BaseViewModel::set_pri_color(uint32_t rgb) {
    model_.set_pri_color(rgb);
    pri_color_subject_.set(static_cast<int>(model_.pri_color()));
}

void BaseViewModel::set_sec_color(uint32_t rgb) {
    model_.set_sec_color(rgb);
    sec_color_subject_.set(static_cast<int>(model_.sec_color()));
}

void BaseViewModel::set_bg_color(uint32_t rgb) {
    model_.set_bg_color(rgb);
    bg_color_subject_.set(static_cast<int>(model_.bg_color()));
}

void BaseViewModel::persist_theme() {
    model_.save_user_config();
}

int BaseViewModel::codec_op() const {
    return model_.codec_op();
}

int BaseViewModel::eng_calc_op() const {
    return model_.eng_calc_op();
}

int BaseViewModel::eng_calc_group() const {
    return model_.eng_calc_group();
}

int BaseViewModel::timer_seconds() const {
    return model_.timer_seconds();
}

void BaseViewModel::set_timer_seconds(int seconds) {
    model_.set_timer_seconds(seconds);
}

const std::string& BaseViewModel::work_input() const {
    return model_.work_input();
}

void BaseViewModel::set_work_input(std::string value) {
    model_.set_work_input(std::move(value));
}

const std::string& BaseViewModel::work_output() const {
    return model_.work_output();
}

void BaseViewModel::set_work_output(std::string value) {
    model_.set_work_output(std::move(value));
}

int BaseViewModel::barcode_type() const {
    return model_.barcode_type();
}

model::GlyphKind BaseViewModel::glyph_kind() const {
    return model_.glyph_kind();
}

const std::string& BaseViewModel::glyph_payload() const {
    return model_.glyph_payload();
}

const std::string& BaseViewModel::glyph_bits() const {
    return model_.glyph_bits();
}

const std::string& BaseViewModel::glyph_label() const {
    return model_.glyph_label();
}

bool BaseViewModel::glyph_invert() const {
    return model_.glyph_invert();
}

void BaseViewModel::set_glyph_invert(bool invert) {
    model_.set_glyph_invert(invert);
}

const std::vector<uint8_t>& BaseViewModel::qr_modules() const {
    return model_.qr_modules();
}

int BaseViewModel::qr_size() const {
    return model_.qr_size();
}

int BaseViewModel::totp_index() const {
    return model_.totp_index();
}

void BaseViewModel::set_totp_index(int index) {
    model_.set_totp_index(index);
}

const model::ChannelDesc& BaseViewModel::current_channel() const {
    return model_.current_channel();
}

model::BaseModel& BaseViewModel::model() {
    return model_;
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::show_apple_page() {
    go_home();
}

void BaseViewModel::show_butter_page() {
    go_home();
}

void BaseViewModel::toggle_page() {
    go_home();
}

void BaseViewModel::increment_counter() {}

void BaseViewModel::decrement_counter() {}

void BaseViewModel::toggle_bold_text() {
    bold_text_subject_.toggle();
}

void BaseViewModel::toggle_info() {
    info_visible_subject_.toggle();
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
    channel_index_subject_.set(model_.channel_index());
    menu_index_subject_.set(model_.menu_index());
    pri_color_subject_.set(static_cast<int>(model_.pri_color()));
    sec_color_subject_.set(static_cast<int>(model_.sec_color()));
    bg_color_subject_.set(static_cast<int>(model_.bg_color()));
}

} // namespace viewmodel

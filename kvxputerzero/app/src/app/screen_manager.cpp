#include "screen_manager.h"

#include "barcode_lib.h"
#include "backlight.h"
#include "calculator_screen.h"
#include "camera_screen.h"
#include "channel_grid_screen.h"
#include "channel_registry.h"
#include "clock_face_screen.h"
#include "gallery_screen.h"
#include "codec_lib.h"
#include "about_screen.h"
#include "combo_lib.h"
#include "doc_screen.h"
#include "eng_calc_lib.h"
#include "eng_calc_screen.h"
#include "life_counter_screen.h"
#include "glyph_screen.h"
#include "kvx_style.h"
#include "launch_catalog.h"
#include "menu_icons.h"
#include "linux_input.h"
#include "search_screen.h"
#include "shortcut_capture_screen.h"
#include "shortcut_pick_screen.h"
#include "matrix_screen.h"
#include "flashlight_screen.h"
#include "keys_screen.h"
#include "counter_screen.h"
#include "metronome_screen.h"
#include "audio_screen.h"
#include "qr_scan_screen.h"
#include "level_screen.h"
#include "motion_screen.h"
#include "system_screen.h"
#include "options_list_screen.h"
#include "passgen_screen.h"
#include "pda_calendar_screen.h"
#include "pda_edit_screen.h"
#include "pda_hub_screen.h"
#include "pda_lib.h"
#include "pda_list_screen.h"
#include "pda_view_screen.h"
#include "pda_world_clock_screen.h"
#include "power_ctrl.h"
#include "preset_links.h"
#include "qr_lib.h"
#include "timer_screen.h"
#include "tool_screens.h"
#include "totp_lib.h"
#include "totp_show_screen.h"
#include "ui_const.h"
#include "work_screen.h"

#include <cstdio>

#include <cctype>
#include <sstream>
#include <vector>

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) manager->flush_requested_page();
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) return;
    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

bool split_two(const std::string& in, std::string& a, std::string& b) {
    const auto sp = in.find(' ');
    if (sp == std::string::npos) return false;
    a = trim_copy(in.substr(0, sp));
    b = trim_copy(in.substr(sp + 1));
    return !a.empty() && !b.empty();
}

bool open_qr(viewmodel::BaseViewModel& vm, const std::string& payload, std::string& error) {
    tools::qr::Matrix matrix;
    if (!tools::qr::encode_text(payload, matrix, error)) return false;
    vm.set_qr_view_data(std::move(matrix.modules), matrix.size, payload);
    vm.open_qr_view();
    return true;
}

bool open_barcode(viewmodel::BaseViewModel& vm, std::string& error) {
    using tools::barcode::BarcodeType;
    const auto type = static_cast<BarcodeType>(vm.barcode_type());
    std::string bits;
    std::string label;
    if (!tools::barcode::encode(type, vm.work_input(), bits, label, error)) return false;
    vm.set_barcode_view_data(std::move(bits), std::move(label), vm.work_input());
    vm.open_barcode_view();
    return true;
}

bool parse_hex_color(std::string in, uint32_t& out) {
    in = trim_copy(std::move(in));
    if (!in.empty() && in[0] == '#') in.erase(0, 1);
    if (in.size() == 8 && (in[0] == '0') && (in[1] == 'x' || in[1] == 'X')) in = in.substr(2);
    if (in.size() != 6) return false;
    for (char c : in) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        out = static_cast<uint32_t>(std::stoul(in, nullptr, 16)) & 0xFFFFFFu;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) return;
    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer, this, nullptr);
    // Load before the main loop. The boot splash removes the active screen, and the
    // first input tick would otherwise touch a null screen and halt on the LVGL assert.
    request_page(view_model_.current_page());
    flush_requested_page();
}

void ScreenManager::request_page(model::AppPage page) {
    if (page_switch_scheduled_ && page != requested_page_) {
        // e.g. theme bounce ConfigMenu → home coalesces to home; still rebuild.
        force_reload_ = true;
    }
    requested_page_ = page;
    if (page_switch_scheduled_) return;
    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;
    if (has_loaded_page_ && requested_page_ == loaded_page_ && !force_reload_) return;
    force_reload_ = false;
    load_screen(make_screen(requested_page_));
    loaded_page_ = requested_page_;
    has_loaded_page_ = true;
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

void ScreenManager::load_screen(std::unique_ptr<screen::KvxScreen> next_screen) {
    if (!next_screen || !next_screen->root()) return;
    lv_screen_load(next_screen->root());
    // Assign first so the previous screen's destructor runs (closes listeners) before
    // the new screen starts workers in on_shown().
    current_screen_ = std::move(next_screen);
    platform::set_text_entry(current_screen_->accepts_text());
    current_screen_->on_shown();
}

std::unique_ptr<screen::KvxScreen> ScreenManager::make_screen(model::AppPage page) {
    using screen::MenuEntry;
    using screen::OptionsListScreen;

    switch (page) {
    case model::AppPage::ChannelGrid:
        return std::make_unique<screen::ChannelGridScreen>(view_model_, assets_);

    case model::AppPage::ChannelMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Calculator", [this]() { view_model_.open_calculator(); }, view::ICON_MENU_CALCS});
        entries.push_back({"Calculators", [this]() { view_model_.open_eng_calc_menu(); }, view::ICON_MENU_CALCS});
        entries.push_back({"Vendor MAC Search",
                           [this]() {
                               view_model_.open_standalone_eng_calc(
                                   static_cast<int>(tools::engcalc::Op::Oui));
                           },
                           view::ICON_CH_ETH});
        entries.push_back({"Codec / Hash", [this]() { view_model_.open_codec_menu(); }, view::ICON_MENU_CODEC});
        entries.push_back({"Passgen", [this]() { view_model_.open_passgen(); }, view::ICON_MENU_PASSGEN});
        entries.push_back({"TOTP", [this]() { view_model_.open_totp_menu(); }, view::ICON_MENU_TOTP});
        entries.push_back({"Combo", [this]() { view_model_.open_combo_menu(); }, view::ICON_MENU_COMBO});
        entries.push_back({"QRCodes", [this]() { view_model_.open_qr_menu(); }, view::ICON_MENU_QR});
        entries.push_back({"Barcode", [this]() { view_model_.open_barcode_menu(); }, view::ICON_MENU_BARCODE});
        entries.push_back({"PDA", [this]() { view_model_.open_pda_hub(); }, view::ICON_MENU_PDA});
        entries.push_back({"Media Player", [this]() { view_model_.open_tool(model::ToolId::MediaPlayer); },
                           view::tool_icon(model::ToolId::MediaPlayer)});
        entries.push_back({"Timer", [this]() { view_model_.open_timer(); }, view::ICON_MENU_TIMER});
        entries.push_back({"Matrix", [this]() { view_model_.open_matrix(); }, view::ICON_CH_SCRIPTS, 0x39FF14});
        entries.push_back({"Flashlight", [this]() { view_model_.open_flashlight(); }, view::ICON_INFO});
        entries.push_back({"Keys", [this]() { view_model_.open_keys(); }, view::ICON_CH_TOOLS});
        entries.push_back({"Counter", [this]() { view_model_.open_counter(); }, view::ICON_MENU_COMBO});
        entries.push_back({"Metronome", [this]() { view_model_.open_metronome(); }, view::ICON_MENU_TIMER});
        entries.push_back({"Audio", [this]() { view_model_.open_audio(); }, view::tool_icon(model::ToolId::Microphone)});
        entries.push_back({"QR Scan", [this]() { view_model_.open_qr_scan(); }, view::ICON_MENU_QR});
        entries.push_back({"Level", [this]() { view_model_.open_level(); }, view::ICON_COMPASS});
        entries.push_back({"Motion", [this]() { view_model_.open_motion(); }, view::ICON_CH_TOOLS});
        entries.push_back({"System", [this]() { view_model_.open_system_info(); }, view::ICON_CH_CONFIG});
        entries.push_back({"Tone Dialer", [this]() { view_model_.open_tool(model::ToolId::Dtmf); },
                           view::tool_icon(model::ToolId::Dtmf)});
        entries.push_back({"Morse", [this]() { view_model_.open_tool(model::ToolId::Morse); },
                           view::tool_icon(model::ToolId::Morse)});
        entries.push_back({"Microphone", [this]() { view_model_.open_tool(model::ToolId::Microphone); },
                           view::tool_icon(model::ToolId::Microphone)});
        entries.push_back({"MQTT", [this]() { view_model_.open_tool(model::ToolId::Mqtt); },
                           view::tool_icon(model::ToolId::Mqtt)});
        entries.push_back({"LLM Chat", [this]() { view_model_.open_tool(model::ToolId::LlmChat); },
                           view::tool_icon(model::ToolId::LlmChat)});
        entries.push_back({"Drone ID", [this]() { view_model_.open_tool(model::ToolId::DroneId); },
                           view::tool_icon(model::ToolId::DroneId)});
        entries.push_back({"iButton", [this]() { view_model_.open_tool(model::ToolId::IButtonHint); },
                           view::tool_icon(model::ToolId::IButtonHint)});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Tools", std::move(entries));
    }

    case model::AppPage::ToolMenu:
        return make_channel_tool_menu(view_model_, assets_);

    case model::AppPage::Discovery:
        return make_discovery_screen(view_model_, assets_);

    case model::AppPage::Search:
        return std::make_unique<screen::SearchScreen>(view_model_, assets_);

    case model::AppPage::ShortcutMenu: {
        std::vector<MenuEntry> entries;
        for (const auto& binding : view_model_.model().shortcuts()) {
            const auto* target = model::find_launch_target(binding.target_id);
            const std::string name = target ? target->label : binding.target_id;
            const std::string label = (binding.hold ? "Hold " : "Tap ") +
                                      platform::shortcut_key_label(binding.key) + "  " + name;
            const uint32_t key = binding.key;
            const bool hold = binding.hold;
            entries.push_back({label, [this, key, hold]() { view_model_.remove_shortcut(key, hold); }});
        }
        entries.push_back({"Add tap shortcut", [this]() { view_model_.begin_shortcut_bind(false); },
                           view::ICON_PLUS});
        entries.push_back({"Add hold shortcut", [this]() { view_model_.begin_shortcut_bind(true); },
                           view::ICON_PLAY});
        if (!view_model_.model().shortcuts().empty()) {
            entries.push_back({"Clear shortcuts", [this]() { view_model_.clear_shortcuts(); }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Shortcuts", std::move(entries));
    }

    case model::AppPage::ShortcutPick:
        return std::make_unique<screen::ShortcutPickScreen>(view_model_, assets_);

    case model::AppPage::ShortcutCapture:
        return std::make_unique<screen::ShortcutCaptureScreen>(view_model_, assets_);

    case model::AppPage::ToolRun:
    case model::AppPage::ToolPrompt:
        return make_tool_run_screen(view_model_, assets_);

    case model::AppPage::Placeholder: {
        const auto& ch = view_model_.current_channel();
        const char* msg = ch.placeholder ? ch.placeholder : "Coming soon";
        return std::make_unique<screen::DocScreen>(view_model_, assets_, ch.label, std::string(msg));
    }

    case model::AppPage::CodecMenu: {
        std::vector<MenuEntry> entries;
        for (int i = 0; i <= static_cast<int>(tools::codec::Op::Sha256); ++i) {
            auto op = static_cast<tools::codec::Op>(i);
            entries.push_back({tools::codec::op_label(op), [this, i]() { view_model_.open_codec_op(i); }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Codec", std::move(entries));
    }

    case model::AppPage::CodecWork: {
        auto op = static_cast<tools::codec::Op>(view_model_.codec_op());
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, tools::codec::op_label(op), "Typed text · Enter runs",
            [op](const std::string& in, bool& ok) { return tools::codec::run(op, in, ok); });
    }

    case model::AppPage::EngCalcMenu: {
        std::vector<MenuEntry> entries;
        for (int i = 0; i < static_cast<int>(tools::engcalc::Group::Count); ++i) {
            const auto group = static_cast<tools::engcalc::Group>(i);
            entries.push_back({tools::engcalc::group_label(group),
                               [this, i]() { view_model_.open_eng_calc_group(i); }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Calcs", std::move(entries));
    }

    case model::AppPage::EngCalcGroup: {
        const auto group = static_cast<tools::engcalc::Group>(view_model_.eng_calc_group());
        const int group_i = view_model_.eng_calc_group();
        std::vector<MenuEntry> entries;
        if (group_i >= 0 && group_i < static_cast<int>(tools::engcalc::Group::Count)) {
            const int n = tools::engcalc::group_size(group);
            for (int i = 0; i < n; ++i) {
                const int id = tools::engcalc::group_item_id(group, i);
                const char* label = tools::engcalc::group_item_label(group, i);
                if (id >= tools::engcalc::kMenuLifeBase) {
                    const int game = id - tools::engcalc::kMenuLifeBase;
                    entries.push_back({label, [this, game]() { view_model_.open_life_counter(game); }});
                } else {
                    entries.push_back({label, [this, id]() { view_model_.open_eng_calc_op(id); }});
                }
            }
        }
        const char* title = (group_i >= 0 && group_i < static_cast<int>(tools::engcalc::Group::Count))
                                ? tools::engcalc::group_label(group)
                                : "Calcs";
        return std::make_unique<OptionsListScreen>(view_model_, assets_, title, std::move(entries));
    }

    case model::AppPage::EngCalcWork: {
        auto op = static_cast<tools::engcalc::Op>(view_model_.eng_calc_op());
        return std::make_unique<screen::EngCalcScreen>(view_model_, assets_, op);
    }

    case model::AppPage::LifeCounter:
        return std::make_unique<screen::LifeCounterScreen>(
            view_model_, assets_, static_cast<screen::LifeGame>(view_model_.eng_calc_op()));

    case model::AppPage::Timer:
        return std::make_unique<screen::TimerScreen>(view_model_, assets_);

    case model::AppPage::ClockFace:
        return std::make_unique<screen::ClockFaceScreen>(view_model_, assets_, false);

    case model::AppPage::ChargeFace:
        return std::make_unique<screen::ClockFaceScreen>(view_model_, assets_, true);

    case model::AppPage::Camera:
        return std::make_unique<screen::CameraScreen>(view_model_, assets_);

    case model::AppPage::Gallery:
        return std::make_unique<screen::GalleryScreen>(view_model_, assets_);

    case model::AppPage::ClockSubMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Timer", [this]() { view_model_.open_timer(); }, view::ICON_MENU_TIMER});
        entries.push_back({"Exit", [this]() { view_model_.go_home(); }, view::ICON_SIGN_OUT});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Clock", std::move(entries));
    }

    case model::AppPage::Matrix:
        return std::make_unique<screen::MatrixScreen>(view_model_, assets_);

    case model::AppPage::Flashlight:
        return std::make_unique<screen::FlashlightScreen>(view_model_, assets_);

    case model::AppPage::Keys:
        return std::make_unique<screen::KeysScreen>(view_model_, assets_);

    case model::AppPage::Counter:
        return std::make_unique<screen::CounterScreen>(view_model_, assets_);

    case model::AppPage::Metronome:
        return std::make_unique<screen::MetronomeScreen>(view_model_, assets_);

    case model::AppPage::AudioAnalyzer:
        return std::make_unique<screen::AudioScreen>(view_model_, assets_);

    case model::AppPage::QrScan:
        return std::make_unique<screen::QrScanScreen>(view_model_, assets_);

    case model::AppPage::Level:
        return std::make_unique<screen::LevelScreen>(view_model_, assets_);

    case model::AppPage::Motion:
        return std::make_unique<screen::MotionScreen>(view_model_, assets_);

    case model::AppPage::SystemInfo:
        return std::make_unique<screen::SystemScreen>(view_model_, assets_);

    case model::AppPage::Passgen:
        return std::make_unique<screen::PassgenScreen>(view_model_, assets_);

    case model::AppPage::TotpMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Help", [this]() { view_model_.open_totp_help(); }});
        entries.push_back({"Show codes", [this]() { view_model_.open_totp_show(); }});
        entries.push_back({"Add account", [this]() { view_model_.open_totp_add(); }});
        {
            std::vector<tools::totp::Account> accounts;
            std::string error;
            tools::totp::load_accounts(accounts, error);
            for (const auto& acc : accounts) {
                const std::string name = acc.name;
                entries.push_back({"Del " + name, [this, name]() {
                                       std::vector<tools::totp::Account> all;
                                       std::string err;
                                       if (!tools::totp::load_accounts(all, err)) return;
                                       std::vector<tools::totp::Account> kept;
                                       for (auto& a : all) {
                                           if (a.name != name) kept.push_back(std::move(a));
                                       }
                                       tools::totp::save_accounts(kept, err);
                                       view_model_.open_totp_menu();
                                   }});
            }
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "TOTP", std::move(entries));
    }

    case model::AppPage::TotpShow:
        return std::make_unique<screen::TotpShowScreen>(view_model_, assets_);

    case model::AppPage::TotpAdd:
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, "Add TOTP", "name base32secret",
            [this](const std::string& in, bool& ok) -> std::string {
                std::string name;
                std::string secret;
                if (!split_two(in, name, secret)) {
                    ok = false;
                    return std::string("need: name secret");
                }
                std::vector<uint8_t> key;
                if (!tools::totp::base32_decode(secret, key)) {
                    ok = false;
                    return std::string("bad base32 secret");
                }
                std::vector<tools::totp::Account> accounts;
                std::string error;
                if (!tools::totp::load_accounts(accounts, error)) {
                    ok = false;
                    return error;
                }
                accounts.push_back({name, secret});
                if (!tools::totp::save_accounts(accounts, error)) {
                    ok = false;
                    return error;
                }
                ok = true;
                view_model_.open_totp_menu();
                return std::string("saved (plaintext totp.json)");
            });

    case model::AppPage::TotpHelp: {
        std::vector<MenuEntry> entries;
        entries.push_back({"RFC 6238 · 6 digits · 30s", []() {}});
        entries.push_back({"Storage: ~/.config/kvxputerzero/totp.json", []() {}});
        entries.push_back({"Plaintext secrets — not encrypted", []() {}});
        entries.push_back({"Needs correct system time", []() {}});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "TOTP Help", std::move(entries));
    }

    case model::AppPage::ComboMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Solve", [this]() { view_model_.open_combo_work(); }});
        entries.push_back({"How to use", [this]() { view_model_.open_combo_help(); }});
        entries.push_back({"About", [this]() { view_model_.open_combo_help(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Combo", std::move(entries));
    }

    case model::AppPage::ComboWork:
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, "Combo", tools::combo::input_hint(),
            [](const std::string& in, bool& ok) { return tools::combo::solve_line(in, ok); });

    case model::AppPage::ComboHelp: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Master-style lock helper", []() {}});
        entries.push_back({"Enter two stuck points 0-39", []() {}});
        entries.push_back({"and resistance 0.0-39.5", []() {}});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Combo Help", std::move(entries));
    }

    case model::AppPage::QrMenu: {
        std::vector<MenuEntry> entries;
        std::vector<tools::qr::SavedEntry> saved;
        std::string error;
        tools::qr::load_saved(saved, error);
        for (const auto& e : saved) {
            const std::string content = e.content;
            entries.push_back({e.name, [this, content]() {
                                   std::string error;
                                   open_qr(view_model_, content, error);
                               }});
        }
        entries.push_back({tools::links::kRepoLabel, [this]() {
                               std::string error;
                               open_qr(view_model_, tools::links::kRepoUrl, error);
                           }});
        entries.push_back({tools::links::kGithubLabel, [this]() {
                               std::string error;
                               open_qr(view_model_, tools::links::kGithubUrl, error);
                           }});
        entries.push_back({"Useful Links...", [this]() { view_model_.open_qr_useful_links(); }});
        entries.push_back({"PIX", [this]() { view_model_.open_qr_pix(); }});
        entries.push_back({"Custom", [this]() { view_model_.open_qr_custom(); }});
        entries.push_back({"Save&Display", [this]() { view_model_.open_qr_save(); }});
        entries.push_back({"Remove", [this]() { view_model_.open_qr_remove(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "QRCodes", std::move(entries));
    }

    case model::AppPage::QrUsefulLinks: {
        std::vector<MenuEntry> entries;
        for (size_t i = 0; i < tools::links::kUsefulCount; ++i) {
            const char* url = tools::links::kUseful[i];
            entries.push_back({tools::links::short_label(url), [this, u = std::string(url)]() {
                                   std::string error;
                                   open_qr(view_model_, u, error);
                               }});
        }
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Useful Links",
                                                   std::move(entries));
    }

    case model::AppPage::QrCustom:
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, "QR Custom", "text · Enter shows QR",
            [this](const std::string& in, bool& ok) -> std::string {
                view_model_.set_work_input(in);
                std::string error;
                ok = open_qr(view_model_, in, error);
                return ok ? std::string() : error;
            });

    case model::AppPage::QrSave:
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, "Save QR", "name content",
            [this](const std::string& in, bool& ok) -> std::string {
                std::string name;
                std::string content;
                if (!split_two(in, name, content)) {
                    ok = false;
                    return std::string("need: name content");
                }
                std::string error;
                if (!tools::qr::add_saved(name, content, error)) {
                    ok = false;
                    return error;
                }
                ok = open_qr(view_model_, content, error);
                return ok ? std::string("saved") : error;
            });

    case model::AppPage::QrPix:
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, "PIX QR", "key amount",
            [this](const std::string& in, bool& ok) -> std::string {
                std::string key;
                std::string amount;
                if (!split_two(in, key, amount)) {
                    ok = false;
                    return std::string("need: key amount");
                }
                std::string error;
                const std::string payload = tools::qr::build_pix(key, amount, error);
                if (payload.empty()) {
                    ok = false;
                    return error;
                }
                ok = open_qr(view_model_, payload, error);
                return ok ? std::string("ok") : error;
            });

    case model::AppPage::QrView:
    case model::AppPage::BarcodeView:
        return std::make_unique<screen::GlyphScreen>(view_model_, assets_);

    case model::AppPage::QrRemove: {
        std::vector<MenuEntry> entries;
        std::vector<tools::qr::SavedEntry> saved;
        std::string error;
        tools::qr::load_saved(saved, error);
        for (const auto& e : saved) {
            const std::string name = e.name;
            entries.push_back({name, [this, name]() {
                                   std::string err;
                                   tools::qr::remove_saved(name, err);
                                   view_model_.open_qr_menu();
                               }});
        }
        if (entries.empty()) entries.push_back({"Nothing to remove", [this]() { view_model_.go_back(); }});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Remove QR", std::move(entries));
    }

    case model::AppPage::BarcodeMenu: {
        std::vector<MenuEntry> entries;
        for (int i = 0; i <= static_cast<int>(tools::barcode::BarcodeType::UpcA); ++i) {
            auto type = static_cast<tools::barcode::BarcodeType>(i);
            entries.push_back(
                {tools::barcode::type_label(type), [this, i]() { view_model_.open_barcode_work(i); }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Barcode", std::move(entries));
    }

    case model::AppPage::BarcodeWork: {
        auto type = static_cast<tools::barcode::BarcodeType>(view_model_.barcode_type());
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, tools::barcode::type_label(type), "data · Enter draws",
            [this](const std::string& in, bool& ok) -> std::string {
                view_model_.set_work_input(in);
                std::string error;
                ok = open_barcode(view_model_, error);
                return ok ? std::string() : error;
            });
    }

    case model::AppPage::ConfigMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Main color",
                           [this]() { view_model_.open_color_picker(model::ColorTarget::Primary); },
                           view::ICON_MENU_MAIN_CLR,
                           view_model_.pri_color()});
        entries.push_back({"Accent color",
                           [this]() { view_model_.open_color_picker(model::ColorTarget::Accent); },
                           view::ICON_MENU_ACCENT,
                           view_model_.sec_color()});
        entries.push_back({"Background",
                           [this]() { view_model_.open_color_picker(model::ColorTarget::Background); },
                           view::ICON_MENU_BG,
                           view_model_.bg_color()});
        entries.push_back({"Reset colors", [this]() {
                               view_model_.reset_colors();
                               view_model_.go_home();
                           }, view::ICON_MENU_RESET});
        entries.push_back({"Brightness", [this]() { view_model_.open_brightness(); }, view::ICON_SUN});
        entries.push_back({"Dim time", [this]() { view_model_.open_dim_time(); }, view::ICON_MOON});
        entries.push_back({"Sound", [this]() { view_model_.open_sound_menu(); }, view::ICON_CH_FM});
        entries.push_back({"Power", [this]() { view_model_.open_power_menu(); }, view::ICON_CH_CHARGE});
        entries.push_back({"Shortcuts", [this]() { view_model_.open_shortcut_menu(); }, view::ICON_CH_CONFIG});
        entries.push_back({"About", [this]() { view_model_.open_about(); }, view::ICON_MENU_ABOUT});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Config", std::move(entries));
    }

    case model::AppPage::ColorPicker: {
        const auto target = view_model_.color_target();
        std::vector<MenuEntry> entries;
        struct Swatch {
            const char* name;
            uint32_t rgb;
        };
        const Swatch* swatches = nullptr;
        size_t swatch_count = 0;
        const char* title = "Main color";
        if (target == model::ColorTarget::Accent) {
            title = "Accent color";
            static const Swatch kAccent[] = {
                {"Green (default)", model::kSecFrom565},
                {"Purple", model::kPriFrom565},
                {"Cyan", 0x20E0E0},
                {"Orange", 0xF0A020},
                {"White", 0xE0E0E0},
                {"Red", 0xE03050},
                {"Blue", 0x2080F0},
            };
            swatches = kAccent;
            swatch_count = sizeof(kAccent) / sizeof(kAccent[0]);
        } else if (target == model::ColorTarget::Background) {
            title = "Background";
            static const Swatch kBg[] = {
                {"Black (default)", model::kBgDefault},
                {"Near black", 0x101018},
                {"Purple tint", 0x1A1020},
                {"Light gray", 0xF2F2F2},
            };
            swatches = kBg;
            swatch_count = sizeof(kBg) / sizeof(kBg[0]);
        } else {
            static const Swatch kMain[] = {
                {"White (default)", model::kPriDefault},
                {"Purple", model::kPriFrom565},
                {"Green", model::kSecFrom565},
                {"Blue", 0x2080F0},
                {"Orange", 0xF0A020},
                {"Cyan", 0x20E0E0},
                {"Red", 0xE03050},
            };
            swatches = kMain;
            swatch_count = sizeof(kMain) / sizeof(kMain[0]);
        }
        for (size_t i = 0; i < swatch_count; ++i) {
            const auto s = swatches[i];
            entries.push_back({s.name,
                               [this, target, rgb = s.rgb]() {
                                   if (target == model::ColorTarget::Accent) {
                                       view_model_.set_sec_color(rgb);
                                   } else if (target == model::ColorTarget::Background) {
                                       view_model_.set_bg_color(rgb);
                                   } else {
                                       view_model_.set_pri_color(rgb);
                                   }
                                   view_model_.persist_theme();
                                   view_model_.go_back();
                               },
                               nullptr,
                               s.rgb});
        }
        entries.push_back({"Custom hex...", [this]() { view_model_.open_color_hex(); },
                           view::ICON_MENU_CUSTOM});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, title, std::move(entries));
    }

    case model::AppPage::ColorHex: {
        const auto target = view_model_.color_target();
        const char* title = "Main hex";
        if (target == model::ColorTarget::Accent) title = "Accent hex";
        else if (target == model::ColorTarget::Background) title = "Background hex";
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, title, "RRGGBB or #RRGGBB · Enter applies",
            [this, target](const std::string& in, bool& ok) {
                uint32_t rgb = 0;
                if (!parse_hex_color(in, rgb)) {
                    ok = false;
                    return std::string("Invalid hex");
                }
                if (target == model::ColorTarget::Accent) {
                    view_model_.set_sec_color(rgb);
                } else if (target == model::ColorTarget::Background) {
                    view_model_.set_bg_color(rgb);
                } else {
                    view_model_.set_pri_color(rgb);
                }
                view_model_.persist_theme();
                ok = true;
                view_model_.show_page(model::AppPage::ColorPicker);
                return std::string("Applied");
            });
    }

    case model::AppPage::About:
        return std::make_unique<screen::AboutScreen>(view_model_, assets_);

    case model::AppPage::PdaHub:
        return std::make_unique<screen::PdaHubScreen>(view_model_, assets_);

    case model::AppPage::PdaList:
        return std::make_unique<screen::PdaListScreen>(view_model_, assets_);

    case model::AppPage::PdaEdit:
        return std::make_unique<screen::PdaEditScreen>(view_model_, assets_);

    case model::AppPage::PdaView:
        return std::make_unique<screen::PdaViewScreen>(view_model_, assets_);

    case model::AppPage::PdaCalendar:
        return std::make_unique<screen::PdaCalendarScreen>(view_model_, assets_);

    case model::AppPage::PdaDay:
        return std::make_unique<screen::PdaDayScreen>(view_model_, assets_);

    case model::AppPage::PdaWorldClock:
        return std::make_unique<screen::PdaWorldClockScreen>(view_model_, assets_);

    case model::AppPage::PdaRebind: {
        if (view_model_.model().work_output() == "__rebind_pick__") {
            const int key = view_model_.model().pda_rebind_key();
            std::vector<MenuEntry> ch;
            for (int c = 0; c < 8; ++c) {
                const int channel = c;
                ch.push_back({tools::pda::channel_label(static_cast<tools::pda::Channel>(c)), [this, key, channel]() {
                                  view_model_.model().set_pda_key_bind(key, static_cast<uint8_t>(channel));
                                  view_model_.persist_pda_config();
                                  view_model_.model().set_work_output("");
                                  view_model_.open_pda_hub();
                              }});
            }
            ch.push_back({"Cancel", [this]() {
                              view_model_.model().set_work_output("");
                              view_model_.open_pda_hub();
                          }});
            const std::string title = std::string("Key ") + std::to_string(key) + " opens";
            return std::make_unique<OptionsListScreen>(view_model_, assets_, title, std::move(ch));
        }
        std::vector<MenuEntry> entries;
        for (int k = 1; k <= 8; ++k) {
            entries.push_back({std::string("Key ") + std::to_string(k), [this, k]() {
                                   view_model_.model().set_pda_rebind_key(k);
                                   view_model_.model().set_work_output("__rebind_pick__");
                                   view_model_.show_page(model::AppPage::PdaRebind);
                               }});
        }
        entries.push_back({"Cancel", [this]() { view_model_.open_pda_hub(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Rebind", std::move(entries));
    }

    case model::AppPage::Calculator:
        return std::make_unique<screen::CalculatorScreen>(view_model_, assets_);

    case model::AppPage::FilesBrowser:
    case model::AppPage::MediaPlayer:
        return std::make_unique<screen::ChannelGridScreen>(view_model_, assets_);

    case model::AppPage::Brightness: {
        int32_t pct = 50;
        platform::read_brightness_percent(pct);
        std::vector<MenuEntry> entries;
        char cur[32];
        std::snprintf(cur, sizeof(cur), "Now %d%%", static_cast<int>(pct));
        entries.push_back({cur, []() {}});
        for (int step : {10, 20, 30, 40, 50, 60, 70, 80, 90, 100}) {
            entries.push_back({std::to_string(step) + "%", [this, step]() {
                                   platform::write_brightness_percent(step);
                                   view_model_.show_page(model::AppPage::Brightness);
                               }});
        }
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Brightness", std::move(entries));
    }

    case model::AppPage::DimTime: {
        const int cur = view_model_.model().dim_seconds();
        auto label = [](int s) -> std::string {
            if (s == 0) return "Off";
            return std::to_string(s) + "s";
        };
        std::vector<MenuEntry> entries;
        for (int s : {0, 15, 30, 60}) {
            const std::string mark = (s == cur) ? " *" : "";
            entries.push_back({label(s) + mark, [this, s]() {
                                   view_model_.model().set_dim_seconds(s);
                                   view_model_.model().save_user_config();
                                   view_model_.show_page(model::AppPage::DimTime);
                               }});
        }
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Dim time", std::move(entries));
    }

    case model::AppPage::SoundMenu: {
        const bool on = view_model_.model().sound_enabled();
        const int vol = view_model_.model().sound_volume();
        std::vector<MenuEntry> entries;
        entries.push_back({std::string("Sound: ") + (on ? "ON" : "OFF"), [this]() {
                               view_model_.model().set_sound_enabled(!view_model_.model().sound_enabled());
                               view_model_.model().save_user_config();
                               view_model_.show_page(model::AppPage::SoundMenu);
                           }});
        for (int step : {20, 40, 60, 80, 100}) {
            const std::string mark = (step == vol) ? " *" : "";
            entries.push_back({"Volume " + std::to_string(step) + "%" + mark, [this, step]() {
                                   view_model_.model().set_sound_volume(step);
                                   view_model_.model().save_user_config();
                                   view_model_.show_page(model::AppPage::SoundMenu);
                               }});
        }
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Sound", std::move(entries));
    }

    case model::AppPage::PowerMenu: {
        std::vector<MenuEntry> entries;
        if (view_model_.model().work_output() == "__confirm_reboot__") {
            entries.push_back({"Yes, reboot", [this]() {
                                   std::string err;
                                   platform::safe_reboot(err);
                                   view_model_.model().set_work_output(err.empty() ? "rebooting..." : err);
                                   view_model_.show_page(model::AppPage::PowerMenu);
                               }});
            entries.push_back({"No", [this]() {
                                   view_model_.model().set_work_output("");
                                   view_model_.show_page(model::AppPage::PowerMenu);
                               }});
            return std::make_unique<OptionsListScreen>(view_model_, assets_, "Reboot?", std::move(entries));
        }
        if (view_model_.model().work_output() == "__confirm_poweroff__") {
            entries.push_back({"Yes, power off", [this]() {
                                   std::string err;
                                   platform::safe_shutdown(err);
                                   view_model_.model().set_work_output(err.empty() ? "powering off..." : err);
                                   view_model_.show_page(model::AppPage::PowerMenu);
                               }});
            entries.push_back({"No", [this]() {
                                   view_model_.model().set_work_output("");
                                   view_model_.show_page(model::AppPage::PowerMenu);
                               }});
            return std::make_unique<OptionsListScreen>(view_model_, assets_, "Power off?", std::move(entries));
        }
        if (!view_model_.model().work_output().empty() &&
            view_model_.model().work_output().rfind("__", 0) != 0) {
            entries.push_back({view_model_.model().work_output(), []() {}});
        }
        entries.push_back({"Restart", [this]() {
                               view_model_.model().set_work_output("__confirm_reboot__");
                               view_model_.show_page(model::AppPage::PowerMenu);
                           }});
        entries.push_back({"Power Off", [this]() {
                               view_model_.model().set_work_output("__confirm_poweroff__");
                               view_model_.show_page(model::AppPage::PowerMenu);
                           }});
        entries.push_back({"Back", [this]() {
                               view_model_.model().set_work_output("");
                               view_model_.go_back();
                           }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Power", std::move(entries));
    }
    }

    return std::make_unique<screen::ChannelGridScreen>(view_model_, assets_);
}

} // namespace app

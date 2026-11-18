#include "screen_manager.h"

#include "barcode_lib.h"
#include "channel_grid_screen.h"
#include "channel_registry.h"
#include "codec_lib.h"
#include "combo_lib.h"
#include "eng_calc_lib.h"
#include "glyph_screen.h"
#include "options_list_screen.h"
#include "passgen_screen.h"
#include "pda_calendar_screen.h"
#include "pda_edit_screen.h"
#include "pda_hub_screen.h"
#include "pda_lib.h"
#include "pda_list_screen.h"
#include "pda_view_screen.h"
#include "pda_world_clock_screen.h"
#include "qr_lib.h"
#include "timer_screen.h"
#include "totp_lib.h"
#include "totp_show_screen.h"
#include "work_screen.h"

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
    requested_page_ = page;
    if (page_switch_scheduled_) return;
    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;
    if (has_loaded_page_ && requested_page_ == loaded_page_) return;
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
    current_screen_ = std::move(next_screen);
}

std::unique_ptr<screen::KvxScreen> ScreenManager::make_screen(model::AppPage page) {
    using screen::MenuEntry;
    using screen::OptionsListScreen;

    switch (page) {
    case model::AppPage::ChannelGrid:
        return std::make_unique<screen::ChannelGridScreen>(view_model_, assets_);

    case model::AppPage::ChannelMenu: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Codec / Hash", [this]() { view_model_.open_codec_menu(); }});
        entries.push_back({"Calculators", [this]() { view_model_.open_eng_calc_menu(); }});
        entries.push_back({"Passgen", [this]() { view_model_.open_passgen(); }});
        entries.push_back({"TOTP", [this]() { view_model_.open_totp_menu(); }});
        entries.push_back({"Combo", [this]() { view_model_.open_combo_menu(); }});
        entries.push_back({"QRCodes", [this]() { view_model_.open_qr_menu(); }});
        entries.push_back({"Barcode", [this]() { view_model_.open_barcode_menu(); }});
        entries.push_back({"PDA", [this]() { view_model_.open_pda_hub(); }});
        entries.push_back({"Timer", [this]() { view_model_.open_timer(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Tools", std::move(entries));
    }

    case model::AppPage::Placeholder: {
        const auto& ch = view_model_.current_channel();
        std::vector<MenuEntry> entries;
        const char* msg = ch.placeholder ? ch.placeholder : "Coming soon";
        entries.push_back({msg, [this]() { view_model_.go_back(); }});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, ch.label, std::move(entries));
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
        for (int i = 0; i < static_cast<int>(tools::engcalc::Op::Count); ++i) {
            auto op = static_cast<tools::engcalc::Op>(i);
            entries.push_back(
                {tools::engcalc::op_label(op), [this, i]() { view_model_.open_eng_calc_op(i); }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Calcs", std::move(entries));
    }

    case model::AppPage::EngCalcWork: {
        auto op = static_cast<tools::engcalc::Op>(view_model_.eng_calc_op());
        return std::make_unique<screen::WorkScreen>(
            view_model_, assets_, tools::engcalc::op_label(op), tools::engcalc::input_hint(op),
            [op](const std::string& in, bool& ok) { return tools::engcalc::run(op, in, ok); });
    }

    case model::AppPage::Timer:
        return std::make_unique<screen::TimerScreen>(view_model_, assets_);

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
        entries.push_back({"PIX", [this]() { view_model_.open_qr_pix(); }});
        entries.push_back({"Custom", [this]() { view_model_.open_qr_custom(); }});
        entries.push_back({"Save&Display", [this]() { view_model_.open_qr_save(); }});
        entries.push_back({"Remove", [this]() { view_model_.open_qr_remove(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "QRCodes", std::move(entries));
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
        entries.push_back({"UI Color", [this]() { view_model_.open_color_picker(model::ColorTarget::Primary); }});
        entries.push_back(
            {"Accent Color", [this]() { view_model_.open_color_picker(model::ColorTarget::Accent); }});
        entries.push_back({"Toggle dark mode", [this]() { view_model_.toggle_dark_mode(); }});
        entries.push_back({"About", [this]() { view_model_.open_about(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Config", std::move(entries));
    }

    case model::AppPage::ColorPicker: {
        const bool accent = view_model_.color_target() == model::ColorTarget::Accent;
        std::vector<MenuEntry> entries;
        struct Swatch {
            const char* name;
            uint32_t rgb;
        };
        const Swatch swatches[] = {
            {"Purple (default)", model::kPriFrom565},
            {"Green accent", model::kSecFrom565},
            {"Blue", 0x2080F0},
            {"Orange", 0xF0A020},
            {"Cyan", 0x20E0E0},
            {"White", 0xE0E0E0},
            {"Red", 0xE03050},
        };
        for (const auto& s : swatches) {
            entries.push_back({s.name, [this, accent, rgb = s.rgb]() {
                                   if (accent) view_model_.set_sec_color(rgb);
                                   else view_model_.set_pri_color(rgb);
                                   view_model_.persist_theme();
                                   view_model_.go_back();
                               }});
        }
        return std::make_unique<OptionsListScreen>(view_model_, assets_,
                                                   accent ? "Accent Color" : "UI Color", std::move(entries));
    }

    case model::AppPage::About: {
        std::vector<MenuEntry> entries;
        entries.push_back({"kvxputerzero 0.1.0", []() {}});
        entries.push_back({"Cardputer Zero toolkit shell", []() {}});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "About", std::move(entries));
    }

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

    case model::AppPage::PdaStockCalc: {
        std::vector<MenuEntry> entries;
        entries.push_back({"Use stock Calculator", [this]() { view_model_.go_back(); }});
        entries.push_back({"Back", [this]() { view_model_.go_back(); }});
        return std::make_unique<OptionsListScreen>(view_model_, assets_, "Calculator", std::move(entries));
    }
    }

    return std::make_unique<screen::ChannelGridScreen>(view_model_, assets_);
}

} // namespace app

#pragma once

#include "channel_registry.h"
#include "tool_registry.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

enum class AppPage {
    ChannelGrid = 0,
    ChannelMenu,
    ToolMenu,
    ToolRun,
    ToolPrompt,
    Discovery,
    Search,
    ShortcutMenu,
    ShortcutPick,
    ShortcutCapture,
    Placeholder,
    CodecMenu,
    CodecWork,
    EngCalcMenu,
    EngCalcWork,
    LifeCounter,
    Timer,
    Matrix,
    ConfigMenu,
    ColorPicker,
    ColorHex,
    About,
    Passgen,
    TotpMenu,
    TotpShow,
    TotpAdd,
    TotpHelp,
    ComboMenu,
    ComboWork,
    ComboHelp,
    QrMenu,
    QrCustom,
    QrSave,
    QrPix,
    QrView,
    QrRemove,
    QrUsefulLinks,
    BarcodeMenu,
    BarcodeWork,
    BarcodeView,
    // PDA
    PdaHub,
    PdaList,
    PdaEdit,
    PdaView,
    PdaCalendar,
    PdaDay,
    PdaWorldClock,
    PdaRebind,
    Calculator,
    ClockFace,
    ClockSubMenu,
    ChargeFace,
    Camera,
    Gallery,
    FilesBrowser,
    MediaPlayer,
    Brightness,
    DimTime,
    SoundMenu,
    PowerMenu,
};

enum class ColorTarget {
    Primary = 0,
    Accent = 1,
    Background = 2,
};

enum class GlyphKind {
    None = 0,
    Qr = 1,
    Barcode = 2,
};

enum class PdaKind {
    Notes = 0,
    Memos,
    Todo,
    Contacts,
    Alarms,
};

enum class PdaEditMode {
    New = 0,
    Edit = 1,
    PromptDate = 2,
    PromptTime = 3,
    PromptFilter = 4,
    PromptDue = 5,
    PromptField = 6,
};

class BaseModel {
public:
    BaseModel();

    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    int channel_index() const;
    void set_channel_index(int index);
    const ChannelDesc& current_channel() const;

    int menu_index() const;
    void set_menu_index(int index);

    int codec_op() const;
    void set_codec_op(int op);

    int eng_calc_op() const;
    void set_eng_calc_op(int op);

    ColorTarget color_target() const;
    void set_color_target(ColorTarget target);

    uint32_t pri_color() const;
    uint32_t sec_color() const;
    uint32_t bg_color() const;
    void set_pri_color(uint32_t rgb);
    void set_sec_color(uint32_t rgb);
    void set_bg_color(uint32_t rgb);
    void reset_colors();

    int timer_seconds() const;
    void set_timer_seconds(int seconds);

    int dim_seconds() const; // 0 = off, else 15/30/60
    void set_dim_seconds(int seconds);
    bool sound_enabled() const;
    void set_sound_enabled(bool enabled);
    int sound_volume() const; // 0-100
    void set_sound_volume(int volume);

    const std::string& work_input() const;
    void set_work_input(std::string value);
    const std::string& work_output() const;
    void set_work_output(std::string value);

    int barcode_type() const;
    void set_barcode_type(int type);

    GlyphKind glyph_kind() const;
    void set_glyph_kind(GlyphKind kind);
    const std::string& glyph_payload() const;
    void set_glyph_payload(std::string value);
    const std::string& glyph_bits() const;
    void set_glyph_bits(std::string value);
    const std::string& glyph_label() const;
    void set_glyph_label(std::string value);
    bool glyph_invert() const;
    void set_glyph_invert(bool invert);
    const std::vector<uint8_t>& qr_modules() const;
    void set_qr_modules(std::vector<uint8_t> modules, int size);
    int qr_size() const;

    int totp_index() const;
    void set_totp_index(int index);

    // PDA
    int pda_hub_index() const;
    void set_pda_hub_index(int index);
    PdaKind pda_kind() const;
    void set_pda_kind(PdaKind kind);
    int pda_item_index() const;
    void set_pda_item_index(int index);
    PdaEditMode pda_edit_mode() const;
    void set_pda_edit_mode(PdaEditMode mode);
    const std::string& pda_path() const;
    void set_pda_path(std::string path);
    int pda_edit_field() const;
    void set_pda_edit_field(int field);
    bool pda_multiline() const;
    void set_pda_multiline(bool multiline);

    int cal_year() const;
    int cal_month() const;
    int cal_day() const;
    void set_cal_date(int year, int month, int day);

    int wc_face() const;
    void set_wc_face(int face);
    int wc_city() const;
    void set_wc_city(int city);
    float tmz() const;
    void set_tmz(float value);
    bool dst() const;
    void set_dst(bool value);
    bool clock24hr() const;
    void set_clock24hr(bool value);
    const std::array<uint8_t, 8>& pda_key_bind() const;
    void set_pda_key_bind(int key1to8, uint8_t channel);
    int pda_rebind_key() const;
    void set_pda_rebind_key(int key1to8);

    ToolId active_tool_id() const;
    void set_active_tool_id(ToolId id);
    const std::string& tool_param() const;
    void set_tool_param(std::string value);
    const std::string& discovery_filter() const;
    void set_discovery_filter(std::string value);

    struct KeyShortcut {
        uint32_t key = 0;
        bool hold = false;
        std::string target_id;
    };

    const std::vector<KeyShortcut>& shortcuts() const;
    std::string shortcut_target(uint32_t key, bool hold) const;
    void set_shortcut(uint32_t key, bool hold, std::string target_id);
    void clear_shortcuts();

    const std::vector<std::string>& recent_searches() const;
    void push_recent_search(std::string id);

    bool shortcut_bind_hold() const;
    void set_shortcut_bind_hold(bool hold);
    const std::string& shortcut_bind_target() const;
    void set_shortcut_bind_target(std::string id);

    void load_user_config();
    void save_user_config() const;

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::ChannelGrid;
    int channel_index_ = 0;
    int menu_index_ = 0;
    int codec_op_ = 0;
    int eng_calc_op_ = 0;
    ColorTarget color_target_ = ColorTarget::Primary;
    uint32_t pri_color_ = kPriDefault;
    uint32_t sec_color_ = kSecFrom565;
    uint32_t bg_color_ = kBgDefault;
    int timer_seconds_ = 60;
    int dim_seconds_ = 0;
    bool sound_enabled_ = true;
    int sound_volume_ = 70;
    std::string work_input_;
    std::string work_output_;
    int barcode_type_ = 0;
    GlyphKind glyph_kind_ = GlyphKind::None;
    std::string glyph_payload_;
    std::string glyph_bits_;
    std::string glyph_label_;
    bool glyph_invert_ = true;
    std::vector<uint8_t> qr_modules_;
    int qr_size_ = 0;
    int totp_index_ = 0;

    int pda_hub_index_ = 0;
    PdaKind pda_kind_ = PdaKind::Notes;
    int pda_item_index_ = 0;
    PdaEditMode pda_edit_mode_ = PdaEditMode::New;
    std::string pda_path_;
    int pda_edit_field_ = 0;
    bool pda_multiline_ = false;
    int cal_year_ = 2025;
    int cal_month_ = 1;
    int cal_day_ = 1;
    int wc_face_ = 0;
    int wc_city_ = 0;
    float tmz_ = 0.0f;
    bool dst_ = false;
    bool clock24hr_ = true;
    std::array<uint8_t, 8> pda_key_bind_{{0, 1, 2, 3, 4, 5, 6, 7}};
    int pda_rebind_key_ = 1;

    ToolId active_tool_id_ = ToolId::None;
    std::string tool_param_;
    std::string discovery_filter_;

    std::vector<KeyShortcut> shortcuts_;
    std::vector<std::string> recent_searches_;
    bool shortcut_bind_hold_ = false;
    std::string shortcut_bind_target_;
};

} // namespace model

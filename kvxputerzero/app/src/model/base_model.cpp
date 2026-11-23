#include "base_model.h"

#include "launch_catalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace model {
namespace {

std::filesystem::path user_config_path() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "kvxputerzero" / "kvxputerzero.conf";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "kvxputerzero" / "kvxputerzero.conf";
    }
    return std::filesystem::path("kvxputerzero.conf");
}

uint32_t parse_hex_rgb(const std::string& s, uint32_t fallback) {
    if (s.empty()) return fallback;
    std::string t = s;
    if (t[0] == '#') t.erase(0, 1);
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t.erase(0, 2);
    try {
        return static_cast<uint32_t>(std::stoul(t, nullptr, 16)) & 0xFFFFFFu;
    } catch (...) {
        return fallback;
    }
}

void trim_inplace(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    s.erase(0, i);
}

} // namespace

BaseModel::BaseModel() {
    load_user_config();
}

const char* BaseModel::app_title() const {
    return "kvxputerzero";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
    menu_index_ = 0;
}

int BaseModel::channel_index() const {
    return channel_index_;
}

void BaseModel::set_channel_index(int index) {
    if (index < 0) index = 0;
    if (static_cast<size_t>(index) >= kChannelCount) index = static_cast<int>(kChannelCount) - 1;
    channel_index_ = index;
}

const ChannelDesc& BaseModel::current_channel() const {
    return kChannels[static_cast<size_t>(channel_index_)];
}

int BaseModel::menu_index() const {
    return menu_index_;
}

void BaseModel::set_menu_index(int index) {
    menu_index_ = index < 0 ? 0 : index;
}

int BaseModel::codec_op() const {
    return codec_op_;
}

void BaseModel::set_codec_op(int op) {
    codec_op_ = op;
}

int BaseModel::eng_calc_op() const {
    return eng_calc_op_;
}

void BaseModel::set_eng_calc_op(int op) {
    eng_calc_op_ = op;
}

ColorTarget BaseModel::color_target() const {
    return color_target_;
}

void BaseModel::set_color_target(ColorTarget target) {
    color_target_ = target;
}

uint32_t BaseModel::pri_color() const {
    return pri_color_;
}

uint32_t BaseModel::sec_color() const {
    return sec_color_;
}

uint32_t BaseModel::bg_color() const {
    return bg_color_;
}

void BaseModel::set_pri_color(uint32_t rgb) {
    pri_color_ = rgb & 0xFFFFFFu;
}

void BaseModel::set_sec_color(uint32_t rgb) {
    sec_color_ = rgb & 0xFFFFFFu;
}

void BaseModel::set_bg_color(uint32_t rgb) {
    bg_color_ = rgb & 0xFFFFFFu;
}

void BaseModel::reset_colors() {
    pri_color_ = kPriDefault;
    sec_color_ = kSecFrom565;
    bg_color_ = kBgDefault;
}

int BaseModel::timer_seconds() const {
    return timer_seconds_;
}

void BaseModel::set_timer_seconds(int seconds) {
    if (seconds < 1) seconds = 1;
    if (seconds > 36000) seconds = 36000;
    timer_seconds_ = seconds;
}

int BaseModel::dim_seconds() const {
    return dim_seconds_;
}

void BaseModel::set_dim_seconds(int seconds) {
    if (seconds != 0 && seconds != 15 && seconds != 30 && seconds != 60) seconds = 0;
    dim_seconds_ = seconds;
}

bool BaseModel::sound_enabled() const {
    return sound_enabled_;
}

void BaseModel::set_sound_enabled(bool enabled) {
    sound_enabled_ = enabled;
}

int BaseModel::sound_volume() const {
    return sound_volume_;
}

void BaseModel::set_sound_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    sound_volume_ = volume;
}

const std::string& BaseModel::work_input() const {
    return work_input_;
}

void BaseModel::set_work_input(std::string value) {
    work_input_ = std::move(value);
}

const std::string& BaseModel::work_output() const {
    return work_output_;
}

void BaseModel::set_work_output(std::string value) {
    work_output_ = std::move(value);
}

int BaseModel::barcode_type() const {
    return barcode_type_;
}

void BaseModel::set_barcode_type(int type) {
    barcode_type_ = type;
}

GlyphKind BaseModel::glyph_kind() const {
    return glyph_kind_;
}

void BaseModel::set_glyph_kind(GlyphKind kind) {
    glyph_kind_ = kind;
}

const std::string& BaseModel::glyph_payload() const {
    return glyph_payload_;
}

void BaseModel::set_glyph_payload(std::string value) {
    glyph_payload_ = std::move(value);
}

const std::string& BaseModel::glyph_bits() const {
    return glyph_bits_;
}

void BaseModel::set_glyph_bits(std::string value) {
    glyph_bits_ = std::move(value);
}

const std::string& BaseModel::glyph_label() const {
    return glyph_label_;
}

void BaseModel::set_glyph_label(std::string value) {
    glyph_label_ = std::move(value);
}

bool BaseModel::glyph_invert() const {
    return glyph_invert_;
}

void BaseModel::set_glyph_invert(bool invert) {
    glyph_invert_ = invert;
}

const std::vector<uint8_t>& BaseModel::qr_modules() const {
    return qr_modules_;
}

void BaseModel::set_qr_modules(std::vector<uint8_t> modules, int size) {
    qr_modules_ = std::move(modules);
    qr_size_ = size;
}

int BaseModel::qr_size() const {
    return qr_size_;
}

int BaseModel::totp_index() const {
    return totp_index_;
}

void BaseModel::set_totp_index(int index) {
    totp_index_ = index < 0 ? 0 : index;
}

int BaseModel::pda_hub_index() const {
    return pda_hub_index_;
}

void BaseModel::set_pda_hub_index(int index) {
    if (index < 0) index = 0;
    if (index > 7) index = 7;
    pda_hub_index_ = index;
}

PdaKind BaseModel::pda_kind() const {
    return pda_kind_;
}

void BaseModel::set_pda_kind(PdaKind kind) {
    pda_kind_ = kind;
}

int BaseModel::pda_item_index() const {
    return pda_item_index_;
}

void BaseModel::set_pda_item_index(int index) {
    pda_item_index_ = index < 0 ? 0 : index;
}

PdaEditMode BaseModel::pda_edit_mode() const {
    return pda_edit_mode_;
}

void BaseModel::set_pda_edit_mode(PdaEditMode mode) {
    pda_edit_mode_ = mode;
}

const std::string& BaseModel::pda_path() const {
    return pda_path_;
}

void BaseModel::set_pda_path(std::string path) {
    pda_path_ = std::move(path);
}

int BaseModel::pda_edit_field() const {
    return pda_edit_field_;
}

void BaseModel::set_pda_edit_field(int field) {
    pda_edit_field_ = field;
}

bool BaseModel::pda_multiline() const {
    return pda_multiline_;
}

void BaseModel::set_pda_multiline(bool multiline) {
    pda_multiline_ = multiline;
}

int BaseModel::cal_year() const {
    return cal_year_;
}

int BaseModel::cal_month() const {
    return cal_month_;
}

int BaseModel::cal_day() const {
    return cal_day_;
}

void BaseModel::set_cal_date(int year, int month, int day) {
    cal_year_ = year;
    cal_month_ = month;
    cal_day_ = day;
}

int BaseModel::wc_face() const {
    return wc_face_;
}

void BaseModel::set_wc_face(int face) {
    if (face < 0) face = 0;
    if (face > 2) face = 2;
    wc_face_ = face;
}

int BaseModel::wc_city() const {
    return wc_city_;
}

void BaseModel::set_wc_city(int city) {
    if (city < 0) city = 0;
    wc_city_ = city;
}

float BaseModel::tmz() const {
    return tmz_;
}

void BaseModel::set_tmz(float value) {
    tmz_ = value;
}

bool BaseModel::dst() const {
    return dst_;
}

void BaseModel::set_dst(bool value) {
    dst_ = value;
}

bool BaseModel::clock24hr() const {
    return clock24hr_;
}

void BaseModel::set_clock24hr(bool value) {
    clock24hr_ = value;
}

const std::array<uint8_t, 8>& BaseModel::pda_key_bind() const {
    return pda_key_bind_;
}

void BaseModel::set_pda_key_bind(int key1to8, uint8_t channel) {
    if (key1to8 < 1 || key1to8 > 8) return;
    if (channel > 7) channel = 7;
    pda_key_bind_[static_cast<size_t>(key1to8 - 1)] = channel;
}

int BaseModel::pda_rebind_key() const {
    return pda_rebind_key_;
}

void BaseModel::set_pda_rebind_key(int key1to8) {
    if (key1to8 < 1) key1to8 = 1;
    if (key1to8 > 8) key1to8 = 8;
    pda_rebind_key_ = key1to8;
}

void BaseModel::load_user_config() {
    const auto path = user_config_path();
    std::ifstream in(path);
    if (!in) {
        dark_mode_ = true;
        pri_color_ = kPriDefault;
        sec_color_ = kSecFrom565;
        bg_color_ = kBgDefault;
        return;
    }
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        trim_inplace(key);
        trim_inplace(val);
        if (section == "application" || section == "theme" || section.empty()) {
            if (key == "dark_mode") {
                dark_mode_ = (val == "yes" || val == "true" || val == "1");
            } else if (key == "pri_color") {
                pri_color_ = parse_hex_rgb(val, kPriDefault);
            } else if (key == "sec_color") {
                sec_color_ = parse_hex_rgb(val, kSecFrom565);
            } else if (key == "bg_color") {
                bg_color_ = parse_hex_rgb(val, kBgDefault);
            } else if (key == "dim_seconds") {
                try {
                    set_dim_seconds(std::stoi(val));
                } catch (...) {
                }
            } else if (key == "sound_enabled") {
                sound_enabled_ = (val == "yes" || val == "true" || val == "1");
            } else if (key == "sound_volume") {
                try {
                    set_sound_volume(std::stoi(val));
                } catch (...) {
                }
            }
        }
        if (section == "shortcuts") {
            const bool hold = key.rfind("hold.", 0) == 0;
            const bool tap = key.rfind("tap.", 0) == 0;
            const bool sized = (tap && key.size() == 5) || (hold && key.size() == 6);
            if (sized && !val.empty()) {
                const auto code = static_cast<uint32_t>(static_cast<unsigned char>(key.back()));
                if (find_launch_target(val)) set_shortcut(code, hold, val);
            }
        }
        if (section == "search" && key == "recent") {
            recent_searches_.clear();
            std::stringstream ss(val);
            std::string part;
            while (std::getline(ss, part, ',')) {
                trim_inplace(part);
                if (part.empty() || !find_launch_target(part)) continue;
                recent_searches_.push_back(part);
                if (recent_searches_.size() >= 5) break;
            }
        }
        if (section == "pda" || section.empty()) {
            if (key == "tmz") {
                try {
                    tmz_ = std::stof(val);
                } catch (...) {
                }
            } else if (key == "dst") {
                dst_ = (val == "yes" || val == "true" || val == "1");
            } else if (key == "clock24hr") {
                clock24hr_ = (val == "yes" || val == "true" || val == "1");
            } else if (key == "pdaWcFace") {
                try {
                    set_wc_face(std::stoi(val));
                } catch (...) {
                }
            } else if (key == "pdaKeyBind") {
                // comma-separated 8 ints
                std::stringstream ss(val);
                std::string part;
                int i = 0;
                while (i < 8 && std::getline(ss, part, ',')) {
                    try {
                        int v = std::stoi(part);
                        if (v < 0 || v > 7) v = i;
                        pda_key_bind_[static_cast<size_t>(i)] = static_cast<uint8_t>(v);
                    } catch (...) {
                        pda_key_bind_[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
                    }
                    ++i;
                }
            }
        }
    }
}

void BaseModel::save_user_config() const {
    const auto path = user_config_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out) return;
    out << "[application]\n";
    out << "[theme]\n";
    out << "dark_mode=" << (dark_mode_ ? "yes" : "no") << "\n";
    out << "pri_color=0x" << std::hex << pri_color_ << "\n";
    out << "sec_color=0x" << sec_color_ << "\n";
    out << "bg_color=0x" << bg_color_ << "\n";
    out << std::dec;
    out << "dim_seconds=" << dim_seconds_ << "\n";
    out << "sound_enabled=" << (sound_enabled_ ? "yes" : "no") << "\n";
    out << "sound_volume=" << sound_volume_ << "\n";
    out << "[pda]\n";
    out << "tmz=" << tmz_ << "\n";
    out << "dst=" << (dst_ ? "yes" : "no") << "\n";
    out << "clock24hr=" << (clock24hr_ ? "yes" : "no") << "\n";
    out << "pdaWcFace=" << wc_face_ << "\n";
    out << "pdaKeyBind=";
    for (int i = 0; i < 8; ++i) {
        if (i) out << ',';
        out << static_cast<int>(pda_key_bind_[static_cast<size_t>(i)]);
    }
    out << "\n";
    out << "[shortcuts]\n";
    for (const auto& s : shortcuts_) {
        if (s.target_id.empty() || s.key < 32 || s.key >= 127) continue;
        out << (s.hold ? "hold." : "tap.") << static_cast<char>(s.key) << '=' << s.target_id << "\n";
    }
    out << "[search]\n";
    out << "recent=";
    for (size_t i = 0; i < recent_searches_.size(); ++i) {
        if (i) out << ',';
        out << recent_searches_[i];
    }
    out << "\n";
}

const std::vector<BaseModel::KeyShortcut>& BaseModel::shortcuts() const {
    return shortcuts_;
}

std::string BaseModel::shortcut_target(uint32_t key, bool hold) const {
    for (const auto& s : shortcuts_) {
        if (s.key == key && s.hold == hold) return s.target_id;
    }
    return {};
}

void BaseModel::set_shortcut(uint32_t key, bool hold, std::string target_id) {
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it) {
        if (it->key == key && it->hold == hold) {
            if (target_id.empty()) shortcuts_.erase(it);
            else it->target_id = std::move(target_id);
            return;
        }
    }
    if (!target_id.empty()) shortcuts_.push_back(KeyShortcut{key, hold, std::move(target_id)});
}

void BaseModel::clear_shortcuts() {
    shortcuts_.clear();
}

const std::vector<std::string>& BaseModel::recent_searches() const {
    return recent_searches_;
}

void BaseModel::push_recent_search(std::string id) {
    if (id.empty()) return;
    for (auto it = recent_searches_.begin(); it != recent_searches_.end(); ++it) {
        if (*it == id) {
            recent_searches_.erase(it);
            break;
        }
    }
    recent_searches_.insert(recent_searches_.begin(), std::move(id));
    constexpr size_t kMaxRecent = 5;
    if (recent_searches_.size() > kMaxRecent) recent_searches_.resize(kMaxRecent);
}

bool BaseModel::shortcut_bind_hold() const {
    return shortcut_bind_hold_;
}

void BaseModel::set_shortcut_bind_hold(bool hold) {
    shortcut_bind_hold_ = hold;
}

const std::string& BaseModel::shortcut_bind_target() const {
    return shortcut_bind_target_;
}

void BaseModel::set_shortcut_bind_target(std::string id) {
    shortcut_bind_target_ = std::move(id);
}

ToolId BaseModel::active_tool_id() const {
    return active_tool_id_;
}

void BaseModel::set_active_tool_id(ToolId id) {
    active_tool_id_ = id;
}

const std::string& BaseModel::tool_param() const {
    return tool_param_;
}

void BaseModel::set_tool_param(std::string value) {
    tool_param_ = std::move(value);
}

const std::string& BaseModel::discovery_filter() const {
    return discovery_filter_;
}

void BaseModel::set_discovery_filter(std::string value) {
    discovery_filter_ = std::move(value);
}

} // namespace model
